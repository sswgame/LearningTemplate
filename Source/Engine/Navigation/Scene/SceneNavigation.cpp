#include "pch.h"

#include "Engine/Navigation/Scene/SceneNavigation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Component/NavMeshLinkComponent.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/Scene/NavAgentCrowdSync.h"
#include "Engine/Navigation/Scene/NavGeometryCollector.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct SceneNavigationInternal
        {
            template <typename T>
            static void removeFromList( vector<T*>& inoutList, T* pItem )
            {
                for ( size_t index = 0; index < inoutList.size(); ++index )
                {
                    if ( inoutList[index] == pItem )
                    {
                        inoutList[index] = inoutList.back();
                        inoutList.pop_back();
                        return;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // 군중 진행(⑤)은 `Navigation` 단계 끝에 워커로 가고 `FlushPostAnimation` 본문 앞에서 마친다(⑥) — 그 사이 `Animation` 단계와 겹친다.
    // 그 일이 만지는 씬 데이터는 내비게이션(군중 · 에이전트 이동 상태)뿐이다. 오브젝트 자리 · 컨트롤러 속도는 ⑥ 이 게임 스레드에서 쓴다.
    SW_SCENE_SERVICE_WITH_FRAME_JOB( SceneNavigation, Navigation, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Navigation, SceneDataKind::Transform }, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ), FlushPostAnimation, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Navigation }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    SW_GLOBAL_VARIABLE( int32, gv_navDebugDraw, 0, "내비메시 디버그 — 선(편집기 뷰포트): 1 폴리곤 테두리 · 2 에이전트 경로 · 4 에이전트 속도 · 8 장애물, 16 걷는 면 · 경로를 게임 화면의 메시로 (31 = 모두, 0 = 끔)" );

    void SceneNavigation::drawDebug( DebugDrawQueue& queue )
    {
        // 선은 물리 디버그와 같은 출구로(편집기 뷰포트가 그린다), 비트 16 은 게임 화면에 보이는 메시로.
        updateDebugView( static_cast<uint32>( gv_navDebugDraw ) );
        if ( gv_navDebugDraw == 0 )
            return;
        PhysicsDebugQueueRenderer renderer{ queue };
        drawDebug( renderer, static_cast<uint32>( gv_navDebugDraw ) );
    }

    SceneNavigation::SceneNavigation( GameObjectManager& manager )
        : _settings{}
        , _sourcePath{}
        , _pCookedAsset{}
        , _listRuntime{}
        , _listAgent{}
        , _listSurface{}
        , _listObstacle{}
        , _listLink{}
        , _listGeometrySource{ "SceneNavigation.GeometrySource", RegistrationList<INavGeometrySource>::ReleaseFunction::create<&SceneNavigation::releaseGeometrySource>( this ) }
        , _listFailedAgentType{}
        , _listAreaCostOverride{}
        , _tileRebaker{}
        , _debugView{}
        , _pathSolver{ *this }
        , _pathQueue{ _pathSolver }
        , _crowdStage{}
        , _listAgentTransformWrite{}
        , _listWrittenAgent{}
        , _pManager{ &manager }
        , _waitingMeshCount{ 0 }
        , _crowdDeltaTime{ 0.0f }
        , _bSettingsLoaded{ SW_FALSE }
        , _bIgnoreCooked{ SW_FALSE }
        , _bCrowdUpdateLaunched{ SW_FALSE }
        , _bCrowdTasksInFlight{ SW_FALSE }
    {
    }

    SceneNavigation::~SceneNavigation()
    {
        onSceneCleared();
    }

    void SceneNavigation::onSceneCleared()
    {
        // 워커가 군중 · 내비메시를 읽고 있다 — 놓기 전에 기다린다. 넘긴 군중 결과(⑥)와 경로 요청은 씬과 함께 버린다(옛 핸들은 `Unknown`).
        waitCrowdUpdates();
        _bCrowdUpdateLaunched = SW_FALSE;
        (void)_pathQueue.clear(); // 내비메시를 놓는 것은 아래 줄이다 — 증표 없이 런타임을 통째로 지운다
        NavTileRebaker::waitForAllBakes( _listRuntime );
        _waitingMeshCount = 0; // 미룬 마련은 씬과 함께 사라진다 — 남기면 자동화 사진이 영영 기다린다
        for ( NavMeshAgentComponent* pAgent : _listAgent )
        {
            pAgent->_crowdAgentID   = NavigationConstant::kInvalidAgentID;
            pAgent->_crowdIndex     = kNotRegistered;
            pAgent->_bOnNavMesh     = SW_FALSE;
            pAgent->_bOnOffMeshLink = SW_FALSE;
        }
        for ( NavMeshObstacleComponent* pObstacle : _listObstacle )
        {
            pObstacle->_bCarved = SW_FALSE;
        }
        _listRuntime.clear();
        _tileRebaker.clear();
        _listAreaCostOverride.clear(); // 지역 프로필이 정한 비용은 그 씬의 것이다
    }

    void SceneNavigation::setSettings( const NavMeshSettings& settings )
    {
        onSceneCleared();
        _settings        = settings;
        _bSettingsLoaded = SW_TRUE;
        _listFailedAgentType.clear();
    }

    const NavMeshSettings& SceneNavigation::getSettings()
    {
        if ( _bSettingsLoaded == false )
        {
            _bSettingsLoaded = SW_TRUE;
            const AssetID settingsID{ engine::getSubsystem<EngineDefaultAssets>()._navMeshSettings.get() };
            if ( AssetIO::hasAsset( settingsID ) == false || _settings.loadFromAsset( settingsID ) == false )
                _settings = NavMeshSettings{}; // 코드가 표를 채우지 않는다 - 빈 표는 굽기 전에 validate 가 거절한다(DataDriven)
        }
        return _settings;
    }

    Subscription SceneNavigation::registerAgent( NavMeshAgentComponent* pAgent )
    {
        if ( pAgent == nullptr || pAgent->_navIndex != kNotRegistered )
            return {};
        pAgent->_navIndex = static_cast<uint32>( _listAgent.size() );
        _listAgent.push_back( pAgent );
        return _agentSubscriptions.issue( pAgent );
    }

    void SceneNavigation::unregisterAgent( NavMeshAgentComponent* pAgent )
    {
        const bool bNotRegisteredAgent = pAgent == nullptr || pAgent->_navIndex >= _listAgent.size() || _listAgent[pAgent->_navIndex] != pAgent;
        if ( bNotRegisteredAgent )
            return;
        releaseAgent( pAgent );
        NavMeshAgentComponent* pMoved = _listAgent.back();
        _listAgent[pAgent->_navIndex] = pMoved;
        pMoved->_navIndex             = pAgent->_navIndex;
        _listAgent.pop_back();
        pAgent->_navIndex = kNotRegistered;
    }

    void SceneNavigation::releaseAgent( NavMeshAgentComponent* pAgent )
    {
        if ( pAgent == nullptr || pAgent->_crowdAgentID == NavigationConstant::kInvalidAgentID )
            return;
        waitCrowdUpdates(); // 군중 진행(⑤)이 워커에서 도는 중이면 끝난 뒤에 뺀다
        if ( pAgent->_crowdIndex < _listRuntime.size() && _listRuntime[pAgent->_crowdIndex]->_pCrowd != nullptr )
            _listRuntime[pAgent->_crowdIndex]->_pCrowd->removeAgent( pAgent->_crowdAgentID );
        pAgent->_crowdAgentID   = NavigationConstant::kInvalidAgentID;
        pAgent->_crowdIndex     = kNotRegistered;
        pAgent->_bOnNavMesh     = SW_FALSE;
        pAgent->_bOnOffMeshLink = SW_FALSE;
        pAgent->_velocity       = float3{};
    }

    Subscription SceneNavigation::registerSurface( NavMeshSurfaceComponent* pSurface )
    {
        // 표면은 내비 메시 하나에 하나라 씬에 많아야 수십 개이고 해제(선형)는 언로드 때만 온다 — 색인 구조를 두지 않는다. 등록은 컴포넌트의 onRegister 에서 한 번만 온다.
        if ( pSurface == nullptr )
            return {};
        _listSurface.push_back( pSurface );
        return _surfaceSubscriptions.issue( pSurface );
    }

    void SceneNavigation::unregisterSurface( NavMeshSurfaceComponent* pSurface )
    {
        SceneNavigationInternal::removeFromList( _listSurface, pSurface );
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            if ( pRuntime->_pSurface == pSurface )
                pRuntime->_pSurface = nullptr;
        }
    }

    Subscription SceneNavigation::registerObstacle( NavMeshObstacleComponent* pObstacle )
    {
        if ( pObstacle == nullptr || pObstacle->_navIndex != kNotRegistered )
            return {};
        pObstacle->_navIndex = static_cast<uint32>( _listObstacle.size() );
        _listObstacle.push_back( pObstacle );
        return _obstacleSubscriptions.issue( pObstacle );
    }

    void SceneNavigation::unregisterObstacle( NavMeshObstacleComponent* pObstacle )
    {
        const bool bNotRegisteredObstacle = pObstacle == nullptr || pObstacle->_navIndex >= _listObstacle.size() || _listObstacle[pObstacle->_navIndex] != pObstacle;
        if ( bNotRegisteredObstacle )
            return;
        _tileRebaker.untrackObstacle( *pObstacle, _listRuntime ); // 다시 베이크 종류는 그 자리를 알리고, 캐시 종류는 캐시 장애물을 뺀다
        NavMeshObstacleComponent* pMoved    = _listObstacle.back();
        _listObstacle[pObstacle->_navIndex] = pMoved;
        pMoved->_navIndex                   = pObstacle->_navIndex;
        _listObstacle.pop_back();
        pObstacle->_navIndex = kNotRegistered;
        pObstacle->_bCarved  = SW_FALSE;
    }

    Subscription SceneNavigation::registerGeometrySource( INavGeometrySource* pSource )
    {
        // nullptr · 이미 등록한 소스는 목록이 거절한다(같은 소스가 두 번 들면 빼기가 하나만 지워 죽은 포인터가 남는다).
        return _listGeometrySource.addSubscribed( pSource, "Navigation geometry source" );
    }

    void SceneNavigation::releaseGeometrySource( INavGeometrySource* pSource )
    {
        (void)_listGeometrySource.remove( pSource ); // 구독 하나가 등록 하나라 늘 있다
    }

    Subscription SceneNavigation::registerLink( NavMeshLinkComponent* pLink )
    {
        if ( pLink == nullptr || pLink->_navIndex != kNotRegistered )
            return {};
        pLink->_navIndex = static_cast<uint32>( _listLink.size() );
        // 막힌 채 들어온 링크(씬 데이터)는 다음 갱신에 막는다.
        pLink->_bEnabledDirty = pLink->_bLinkEnabled ? SW_FALSE : SW_TRUE;
        _listLink.push_back( pLink );
        return _linkSubscriptions.issue( pLink );
    }

    void SceneNavigation::unregisterLink( NavMeshLinkComponent* pLink )
    {
        const bool bNotRegisteredLink = pLink == nullptr || pLink->_navIndex >= _listLink.size() || _listLink[pLink->_navIndex] != pLink;
        if ( bNotRegisteredLink )
            return;
        NavMeshLinkComponent* pMoved = _listLink.back();
        _listLink[pLink->_navIndex]  = pMoved;
        pMoved->_navIndex            = pLink->_navIndex;
        _listLink.pop_back();
        pLink->_navIndex = kNotRegistered;
    }

    const NavMeshLinkComponent* SceneNavigation::findLink( uint32 linkID ) const
    {
        if ( linkID == 0 )
            return nullptr;
        return VectorUtil::findBy<&NavMeshLinkComponent::computeCurrentLinkID>( _listLink, linkID );
    }

    void SceneNavigation::applyLinkStates( const NavMeshWriteAccess& /*access*/ )
    {
        for ( NavMeshLinkComponent* pLink : _listLink )
        {
            if ( pLink->_bEnabledDirty == SW_FALSE )
                continue;
            pLink->_bEnabledDirty = SW_FALSE;
            const uint32 linkID   = pLink->computeCurrentLinkID();
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
            {
                pRuntime->_pNavMesh->setOffMeshLinkEnabled( linkID, pLink->_bLinkEnabled );
            }
        }
    }

    void SceneNavigation::invalidateArea( const AABB& bounds, bool bGeometryChanged )
    {
        _tileRebaker.invalidateArea( bounds, bGeometryChanged );
    }

    const NavAgentTypeDef* SceneNavigation::resolveAgentType( const hashed_string& agentType )
    {
        const NavAgentTypeDef* pType = getSettings().findAgentType( agentType );
        if ( pType != nullptr )
            return pType;
        if ( VectorUtil::contains( _listFailedAgentType, agentType ) )
            return nullptr;
        _listFailedAgentType.push_back( agentType );
        SW_LOG_ERROR_ID( "SceneNavigation.NavigationAgentTypeNot", "Navigation agent type '%#' is not in the navigation settings", agentType.c_str() );
        return nullptr;
    }

    bool SceneNavigation::setAgentAreaCost( const hashed_string& agentType, const hashed_string& areaName, float32 cost, bool bAllowed )
    {
        const NavMeshSettings& settings  = getSettings();
        const NavAgentTypeDef* pType     = settings.findAgentType( agentType );
        uint8                  areaIndex = 0;
        if ( pType == nullptr || settings.findAreaIndex( areaName, areaIndex ) == false )
        {
            SW_LOG_ERROR_ID( "SceneNavigation.AreaCostUnknown", "setAgentAreaCost: agent type '%#' or area '%#' is not in the navigation settings", agentType.c_str(), areaName.c_str() );
            return false;
        }
        AreaCostOverride* pExisting = nullptr;
        for ( AreaCostOverride& entry : _listAreaCostOverride )
        {
            if ( entry._agentType == pType->_name && entry._areaIndex == areaIndex )
                pExisting = &entry;
        }
        if ( pExisting == nullptr )
        {
            _listAreaCostOverride.push_back( AreaCostOverride{ pType->_name, areaIndex, SW_TRUE, 1.0f } );
            pExisting = &_listAreaCostOverride.back();
        }
        pExisting->_cost     = MathUtil::max( cost, 1.0f ); // Recast 비용은 1 이상이다(보통 땅이 1)
        pExisting->_bAllowed = bAllowed ? SW_TRUE : SW_FALSE;
        if ( NavMeshRuntime* pRuntime = findRuntime( pType->_name ) )
        {
            // 이미 넘긴 경로 요청은 넘길 때의 거름 사본으로 끝난다(`NavPathJob::_filter`). 군중은 진행 중이면 끝난 뒤에 바꾼다.
            applyAreaCostOverrides( pType->_name, pRuntime->_filter );
            waitCrowdUpdates();
            if ( pRuntime->_pCrowd != nullptr )
                pRuntime->_pCrowd->setQueryFilter( pRuntime->_filter );
        }
        return true;
    }

    bool SceneNavigation::findAgentAreaCost( const hashed_string& agentType, const hashed_string& areaName, float32& outCost, bool& outbAllowed ) const
    {
        if ( _bSettingsLoaded == SW_FALSE )
            return false;
        const NavMeshSettings& settings  = _settings;
        const NavAgentTypeDef* pType     = settings.findAgentType( agentType );
        uint8                  areaIndex = 0;
        if ( pType == nullptr || settings.findAreaIndex( areaName, areaIndex ) == false )
            return false;
        NavQueryFilter filter = settings.makeDefaultFilter();
        applyAreaCostOverrides( pType->_name, filter );
        outCost     = filter._arrAreaCost[areaIndex];
        outbAllowed = ( filter._excludeAreaMask & static_cast<uint16>( 1u << areaIndex ) ) == 0;
        return true;
    }

    void SceneNavigation::applyAreaCostOverrides( const hashed_string& agentType, NavQueryFilter& inoutFilter ) const
    {
        for ( const AreaCostOverride& entry : _listAreaCostOverride )
        {
            if ( entry._agentType != agentType )
                continue;
            inoutFilter._arrAreaCost[entry._areaIndex] = entry._cost;
            inoutFilter.setAreaAllowed( entry._areaIndex, entry._bAllowed == SW_TRUE );
        }
    }

    NavMeshRuntime* SceneNavigation::findRuntime( const hashed_string& agentType ) const
    {
        // 빈 이름은 기본 종류 — 표의 `_defaultAgentType`, 그것도 비면 첫 종류(`NavMeshSettings::findAgentType` 과 같은 규칙).
        const NavAgentTypeDef* pType  = _settings.findAgentType( agentType );
        const hashed_string&   wanted = pType != nullptr ? pType->_name : agentType;
        return VectorUtil::findBy<&NavMeshRuntime::_agentType>( _listRuntime, wanted );
    }

    const NavMeshSurfaceComponent* SceneNavigation::findSurface( const hashed_string& agentType )
    {
        const NavMeshSettings& settings    = getSettings();
        const NavAgentTypeDef* pDefault    = settings.findAgentType( hashed_string{} );
        const hashed_string    defaultName = pDefault != nullptr ? pDefault->_name : hashed_string{};
        for ( const NavMeshSurfaceComponent* pSurface : _listSurface )
        {
            if ( pSurface->isActive() && pSurface->coversAgentType( agentType, defaultName ) )
                return pSurface;
        }
        return nullptr;
    }

    bool SceneNavigation::installCooked( NavMeshRuntime& runtime, const NavAgentTypeDef& agentType, uint64 inputHash )
    {
        const string                   cookedPath = _pCookedAsset != nullptr ? string{ "(given)" } : NavMeshAsset::makeCookedPath( _sourcePath );
        shared_ptr<const NavMeshAsset> pAsset     = _pCookedAsset;
        if ( pAsset == nullptr )
        {
            if ( cookedPath.empty() || AssetIO::hasAsset( AssetID{ cookedPath } ) == false )
                return false;
            shared_ptr<NavMeshAsset> pLoaded = make_shared<NavMeshAsset>();
            if ( pLoaded->loadFromAsset( AssetID{ cookedPath } ) == false )
                return false;
            pAsset = std::move( pLoaded );
        }
        const NavMeshAssetEntry* pEntry = pAsset->findEntry( agentType._name );
        if ( pEntry == nullptr )
            return false;
        if ( pEntry->_settingsHash != NavMeshSettings::computeAgentTypeHash( agentType ) || pEntry->_inputHash != inputHash )
        {
            SW_LOG_WARNING_ID( "SceneNavigation.CookedNavmeshDoesNot", "Cooked navmesh '%#' (%#) does not match the scene or the settings - baking at runtime", cookedPath.c_str(), agentType._name.c_str() );
            return false;
        }
        const Stopwatch stopwatch;
        if ( runtime._pNavMesh->initialize( agentType, _settings, pEntry->_bounds ) == false ||
             NavMeshAsset::installTiles( *runtime._pNavMesh, pEntry->_listTile ) == false )
        {
            SW_LOG_WARNING_ID( "SceneNavigation.CookedNavmeshCouldNot", "Cooked navmesh '%#' (%#) could not be installed - baking at runtime", cookedPath.c_str(), agentType._name.c_str() );
            return false;
        }
        runtime._bounds                     = pEntry->_bounds;
        runtime._bFromCooked                = SW_TRUE;
        runtime._bakeStats                  = NavMeshBakeStats{};
        runtime._bakeStats._milliseconds    = static_cast<float64>( stopwatch.getElapsedMicroseconds() ) / constant::time::kMicrosecondsPerMillisecond;
        runtime._bakeStats._tileCount       = static_cast<uint32>( runtime._pNavMesh->getTileGrid().getTileCount() );
        runtime._bakeStats._filledTileCount = runtime._pNavMesh->getLoadedTileCount();
        runtime._bakeStats._polygonCount    = runtime._pNavMesh->getPolygonCount();
        return true;
    }

    NavMeshRuntime* SceneNavigation::createRuntime( const hashed_string& agentType, NavMeshArrival arrival )
    {
        SW_MEMORY_SCOPE( Navigation );
        SW_PROFILE_SCOPE( "GT.Navigation.createNavMesh" );
        const NavAgentTypeDef* pType = resolveAgentType( agentType );
        if ( pType == nullptr || _pManager == nullptr )
            return nullptr;
        NavMeshRuntime* pExisting = findRuntime( pType->_name );
        if ( pExisting != nullptr )
            return pExisting;
        // 마련하다 실패한 종류는 다시 해 보지 않는다 — 갱신마다 기하를 모으고 오류를 남기게 된다.
        for ( const hashed_string& failed : _listFailedAgentType )
        {
            if ( failed == pType->_name )
                return nullptr;
        }

        unique_ptr<NavMeshRuntime> pRuntime = make_unique<NavMeshRuntime>();
        pRuntime->_agentType                = pType->_name;
        pRuntime->_pSurface                 = findSurface( pType->_name );
        pRuntime->_filter                   = _settings.makeDefaultFilter();
        applyAreaCostOverrides( pType->_name, pRuntime->_filter );
        const float32 reach     = MathUtil::max( 1.0f, pType->_radius * 4.0f );
        pRuntime->_searchExtent = float3{ reach, MathUtil::max( 1.0f, pType->_height ), reach };

        shared_ptr<NavMeshGeometry> pGeometry        = make_shared<NavMeshGeometry>();
        const uint32                pendingMeshCount = NavGeometryCollector::collectGeometry( *_pManager, pRuntime->_pSurface, _settings, _listGeometrySource.getItems(), arrival, *pGeometry );
        // 메시를 아직 읽는 중이다 — 덜 모인 기하로 굽지 않는다(쿠킹본의 입력 해시도 맞지 않는다). 실패가 아니라 다음 갱신이 다시 마련한다.
        if ( pendingMeshCount > 0 )
        {
            _waitingMeshCount += pendingMeshCount;
            return nullptr;
        }
        // 장애물은 종류의 방식(`_obstacleMode`)대로 — 다시 베이크는 덧칠 부피로 타일에 베이크하고, 캐시는 베이크하지 않고 끼운 뒤 캐시 장애물로 넣는다.
        const bool                          bObstacleCache = pType->_obstacleMode == NavObstacleMode::TileCache;
        shared_ptr<vector<NavConvexVolume>> pVolume        = make_shared<vector<NavConvexVolume>>();
        if ( bObstacleCache == false )
            NavTileRebaker::collectObstacleVolumes( _listObstacle, *pVolume );
        pRuntime->_pNavMesh = NavMeshBackend::createNavMesh();

        const uint64 inputHash = pGeometry->computeHash();
        if ( _bIgnoreCooked == false && installCooked( *pRuntime, *pType, inputHash ) )
        {
            // 쿠킹본에는 장애물이 없다 — 장애물 자리의 타일만 다시 베이크한다.
            pGeometry->rebuildSpatialIndex( MathUtil::max( 1.0f, pRuntime->_pNavMesh->getTileGrid()._tileWorldSize * 0.25f ) );
            vector<int2> listTile;
            for ( const NavConvexVolume& volume : *pVolume )
            {
                pRuntime->_pNavMesh->collectTilesOverlapping( volume.computeBounds(), listTile );
                for ( const int2& tile : listTile )
                {
                    pRuntime->addDirtyTile( tile );
                }
            }
            SW_LOG_INFO_ID( "SceneNavigation.NavmeshLoadedCookedFile", "Navmesh '%#' loaded from the cooked file: %# tiles, %# polygons, %# ms", pType->_name.c_str(), pRuntime->_bakeStats._filledTileCount,
                            pRuntime->_bakeStats._polygonCount, pRuntime->_bakeStats._milliseconds );
        }
        else
        {
            if ( pGeometry->isEmpty() )
                SW_LOG_WARNING_ID( "SceneNavigation.NavmeshNoGeometryBake", "Navmesh '%#' has no geometry to bake (no visible static meshes or static bodies)", pType->_name.c_str() );
            const AABB bounds = NavGeometryCollector::computeBakeBounds( pRuntime->_pSurface, *pGeometry );
            if ( pRuntime->_pNavMesh->initialize( *pType, _settings, bounds ) == false )
            {
                _listFailedAgentType.push_back( pType->_name );
                return nullptr;
            }
            pRuntime->_bounds = bounds;
            (void)NavMeshAsset::bakeAllTiles( *pRuntime->_pNavMesh, *pGeometry, pVolume.get(), &pRuntime->_bakeStats );
            SW_LOG_INFO_ID( "SceneNavigation.NavmeshBakedTilesPolygons", "Navmesh '%#' baked: %# of %# tiles, %# polygons, %# triangles in, %# ms", pType->_name.c_str(), pRuntime->_bakeStats._filledTileCount,
                            pRuntime->_bakeStats._tileCount, pRuntime->_bakeStats._polygonCount, pGeometry->getTriangleCount(), pRuntime->_bakeStats._milliseconds );
        }
        // 캐시 종류 — 지금 뚫고 있는 장애물을 캐시에 넣는다(쿠킹본 · 런타임 베이크 모두 장애물 없는 층이다). 다음 갱신이 닿은 타일을 층에서 짓는다.
        if ( bObstacleCache )
            NavTileRebaker::addCarvedObstaclesToCache( *pRuntime, _listObstacle );
        // 막힌 링크(닫힌 문)는 새 내비메시에도 막는다.
        for ( const NavMeshLinkComponent* pLink : _listLink )
        {
            if ( pLink->_bLinkEnabled == false )
                pRuntime->_pNavMesh->setOffMeshLinkEnabled( pLink->computeCurrentLinkID(), false );
        }
        pRuntime->_pGeometry = std::move( pGeometry );
        pRuntime->_pVolume   = std::move( pVolume );
        pRuntime->_pCrowd    = pRuntime->_pNavMesh->createCrowd( pType->_maxCrowdAgentCount, MathUtil::max( 1.0f, pType->_radius * 2.0f ) );
        if ( pRuntime->_pCrowd != nullptr )
            pRuntime->_pCrowd->setQueryFilter( pRuntime->_filter );
        _listRuntime.push_back( std::move( pRuntime ) );
        return _listRuntime.back().get();
    }

    NavMeshRuntime* SceneNavigation::findOrCreateRuntime( const hashed_string& agentType, NavMeshArrival arrival )
    {
        NavMeshRuntime* pRuntime = findRuntime( agentType );
        return pRuntime != nullptr ? pRuntime : createRuntime( agentType, arrival );
    }

    const INavMeshTileStore* SceneNavigation::ensureNavMesh( const hashed_string& agentType )
    {
        // 도구 · 테스트 · 쿠킹이 "지금" 마련하는 길이다 — 결과를 그 자리에서 쓰므로 메시를 그 자리에서 읽는다(갱신은 `Defer` 로 기다리지 않는다).
        NavMeshRuntime* pRuntime = findOrCreateRuntime( agentType, NavMeshArrival::Wait );
        return pRuntime != nullptr ? pRuntime->_pNavMesh.get() : nullptr;
    }

    void SceneNavigation::collectBakedAgentTypes( vector<hashed_string>& outListAgentType ) const
    {
        outListAgentType.clear();
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            outListAgentType.push_back( pRuntime->_agentType );
        }
    }

    uint32 SceneNavigation::rebakeAllNavMeshes()
    {
        SW_PROFILE_SCOPE( "GT.Navigation.rebakeAll" );
        vector<hashed_string> listAgentType;
        collectBakedAgentTypes( listAgentType );
        for ( const NavMeshSurfaceComponent* pSurface : _listSurface )
        {
            if ( pSurface->isActive() == false )
                continue;
            if ( pSurface->getAgentTypes().empty() )
                listAgentType.push_back( hashed_string{} );
            for ( const hashed_string& agentType : pSurface->getAgentTypes() )
            {
                listAgentType.push_back( agentType );
            }
        }
        onSceneCleared();
        _listFailedAgentType.clear();
        _bIgnoreCooked = SW_TRUE;
        for ( const hashed_string& agentType : listAgentType )
        {
            (void)ensureNavMesh( agentType ); // 모르는 종류 · 기하 없음은 ensureNavMesh 가 이미 남긴다 — 센 수가 말해 준다
        }
        _bIgnoreCooked = SW_FALSE;
        return static_cast<uint32>( _listRuntime.size() );
    }

    bool SceneNavigation::makeCookedEntry( const hashed_string& agentType, NavMeshAssetEntry& outEntry )
    {
        outEntry                                     = NavMeshAssetEntry{};
        const INavMeshTileStore* pNavMesh            = ensureNavMesh( agentType );
        NavMeshRuntime*          pRuntime            = findRuntime( agentType );
        const bool               bRuntimeUnavailable = pNavMesh == nullptr || pRuntime == nullptr || pRuntime->_pGeometry == nullptr;
        if ( bRuntimeUnavailable )
            return false;
        outEntry._agentType    = pRuntime->_agentType;
        outEntry._settingsHash = NavMeshSettings::computeAgentTypeHash( pNavMesh->getAgentType() );
        outEntry._inputHash    = pRuntime->_pGeometry->computeHash();
        outEntry._bounds       = pRuntime->_bounds;
        pNavMesh->collectTiles( outEntry._listTile );
        return true;
    }

    const INavMeshTileStore* SceneNavigation::findNavMesh( const hashed_string& agentType ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        return pRuntime != nullptr ? pRuntime->_pNavMesh.get() : nullptr;
    }

    INavCrowd* SceneNavigation::findCrowd( const hashed_string& agentType ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        return pRuntime != nullptr ? pRuntime->_pCrowd.get() : nullptr;
    }

    const NavMeshBakeStats* SceneNavigation::findBakeStats( const hashed_string& agentType ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        return pRuntime != nullptr ? &pRuntime->_bakeStats : nullptr;
    }

    bool SceneNavigation::isLoadedFromCooked( const hashed_string& agentType ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        return pRuntime != nullptr && pRuntime->_bFromCooked;
    }

    NavPathStatus SceneNavigation::findPath( const hashed_string& agentType, const float3& start, const float3& end, NavPath& outPath ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
        {
            outPath = NavPath{};
            return NavPathStatus::Failed;
        }
        return pRuntime->_pNavMesh->findPath( start, end, pRuntime->_searchExtent, pRuntime->_filter, outPath );
    }

    NavPathStatus SceneNavigation::findPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter,
                                             NavPath& outPath ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
        {
            outPath = NavPath{};
            return NavPathStatus::Failed;
        }
        return pRuntime->_pNavMesh->findPath( start, end, pRuntime->_searchExtent, filter, outPath );
    }

    PathRequestHandle SceneNavigation::requestPath( const hashed_string& agentType, const float3& start, const float3& end )
    {
        NavPathJob job;
        job._agentType = agentType;
        job._start     = start;
        job._end       = end;
        return _pathQueue.request( std::move( job ) );
    }

    PathRequestHandle SceneNavigation::requestPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter )
    {
        NavPathJob job;
        job._agentType     = agentType;
        job._start         = start;
        job._end           = end;
        job._filter        = filter;
        job._bCustomFilter = true;
        return _pathQueue.request( std::move( job ) );
    }

    void SceneNavigation::cancelPathRequest( const PathRequestHandle& handle )
    {
        _pathQueue.cancel( handle );
    }

    PathRequestState SceneNavigation::takePathResult( const PathRequestHandle& handle, NavPath& outPath )
    {
        NavPathJob             job;
        const PathRequestState state = _pathQueue.takeResult( handle, job );
        if ( state == PathRequestState::Ready )
            outPath = std::move( job._path );
        return state;
    }

    bool SceneNavigation::findNearestPoint( const hashed_string& agentType, const float3& position, NavLocation& outLocation ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
            return false;
        return pRuntime->_pNavMesh->findNearestPoint( position, pRuntime->_searchExtent, pRuntime->_filter, outLocation );
    }

    bool SceneNavigation::raycast( const hashed_string& agentType, const float3& start, const float3& end, NavRaycastHit& outHit ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
            return false;
        return pRuntime->_pNavMesh->raycast( start, end, pRuntime->_searchExtent, pRuntime->_filter, outHit );
    }

    uint32 SceneNavigation::getPendingTileCount() const
    {
        uint32 count = 0;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            count += static_cast<uint32>( pRuntime->_listDirtyTile.size() + pRuntime->_listJob.size() );
        }
        return count + _tileRebaker.getPendingAreaCount();
    }

    void SceneNavigation::update( float32 deltaTime )
    {
        const bool bAnything = _listAgent.empty() == false || _listObstacle.empty() == false || _listSurface.empty() == false || _listRuntime.empty() == false ||
                               _listLink.empty() == false;
        _waitingMeshCount = 0; // 이번 갱신에 미룬 마련만 센다(사라진 표면 · 에이전트의 옛 기다림이 남지 않게)
        SW_MEMORY_SCOPE( Navigation );
        SW_PROFILE_SCOPE( "GT.Navigation.tick" );
        // ⓪ 지난 프레임에 넘긴 경로 질의를 수확한다 — 내비메시가 없어도 돈다(요청자는 `Failed` 결과를 받아야 기다림이 끝난다).
        const NavMeshWriteAccess access = completeNavMeshReads();
        if ( bAnything == false || _pManager == nullptr )
        {
            _pathQueue.launch( "NavPathQuery", engine::areEngineServicesBound() ? &engine::getSubsystem<TaskManager>() : nullptr );
            return;
        }
        // 플레이 중이면 표면이 맡은 종류를 마련한다(처음 쓸 때 — 쿠킹본이 맞으면 읽고 아니면 베이크한다).
        if ( _pManager->hasBegunPlay() )
        {
            for ( size_t surfaceIndex = 0; surfaceIndex < _listSurface.size(); ++surfaceIndex )
            {
                const NavMeshSurfaceComponent* pSurface = _listSurface[surfaceIndex];
                if ( pSurface->isActive() == false )
                    continue;
                // 있게만 한다 — 모르는 종류 · 기하 없음은 createRuntime 이 이미 남긴다. 메시를 읽는 중이면 다음 갱신이 다시 마련한다(게임 스레드가 디스크를 기다리지 않는다).
                if ( pSurface->getAgentTypes().empty() )
                {
                    (void)findOrCreateRuntime( hashed_string{}, NavMeshArrival::Defer );
                    continue;
                }
                for ( const hashed_string& agentType : pSurface->getAgentTypes() )
                {
                    (void)findOrCreateRuntime( agentType, NavMeshArrival::Defer );
                }
            }
        }
        // 순서에 뜻이 있다 — 끝난 타일 끼우기는 읽는 워커가 없는 이 자리(⓪ 의 증표)에서만, 새 일(베이크 · 경로 질의 · 군중)은 끼운 뒤에 넘긴다.
        _tileRebaker.updateObstacles( _listRuntime, _listObstacle, getSettings()._obstacleMoveThreshold );
        processDirtyAreas( NavMeshArrival::Defer );
        _tileRebaker.applyFinishedBakes( _listRuntime, access );
        NavTileRebaker::updateObstacleCaches( _listRuntime, deltaTime, access ); // 캐시 종류는 장애물 요청을 층에서 바로 짓는다(끼우기와 같은 자리)
        applyLinkStates( access );
        NavTileRebaker::launchTileBakes( _listRuntime, _settings._maxConcurrentTileBakeCount );
        syncAgentsIntoCrowds();
        _pathQueue.launch( "NavPathQuery", engine::areEngineServicesBound() ? &engine::getSubsystem<TaskManager>() : nullptr );
        launchCrowdUpdates( deltaTime );
    }

    NavMeshWriteAccess SceneNavigation::completeNavMeshReads()
    {
        waitCrowdUpdates();
        return NavMeshWriteAccess{ _pathQueue.complete() };
    }

    void SceneNavigation::processDirtyAreas( NavMeshArrival arrival )
    {
        _tileRebaker.processDirtyAreas( _listRuntime, _listObstacle, *_pManager, _settings, _listGeometrySource.getItems(), arrival );
    }

    void SceneNavigation::flushTileBakes()
    {
        const NavMeshWriteAccess access = completeNavMeshReads();
        _tileRebaker.updateObstacles( _listRuntime, _listObstacle, getSettings()._obstacleMoveThreshold );
        // 그 자리에서 끝내는 길이다(테스트 · 쿠킹) — 메시를 기다린다. `Defer` 면 넘긴 영역이 남아 아래 반복이 끝나지 않는다.
        processDirtyAreas( NavMeshArrival::Wait );
        while ( getPendingTileCount() > 0 )
        {
            NavTileRebaker::launchTileBakes( _listRuntime, _settings._maxConcurrentTileBakeCount );
            NavTileRebaker::waitForAllBakes( _listRuntime );
            _tileRebaker.applyFinishedBakes( _listRuntime, access );
            processDirtyAreas( NavMeshArrival::Wait );
        }
        // 장애물 캐시는 갱신마다 타일 하나를 짓는다(Detour 가 비용을 프레임에 나눈다) — 다 지을 때까지 돈다. 상한은 그 종류의 타일 자리 수다.
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            if ( pRuntime->usesObstacleCache() == false )
                continue;
            const int32 maxBuildCount = pRuntime->_pNavMesh->getTileGrid().getTileCount() * static_cast<int32>( NavigationConstant::kMaxCacheLayerCount ) + 1;
            int32       buildCount    = 0;
            while ( buildCount < maxBuildCount && pRuntime->_pNavMesh->updateCacheObstacles( 0.0f ) )
            {
                ++buildCount;
            }
        }
    }

    void SceneNavigation::syncAgentsIntoCrowds()
    {
        if ( _listAgent.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.agents" );
        for ( size_t agentIndex = 0; agentIndex < _listAgent.size(); ++agentIndex )
        {
            NavMeshAgentComponent& agent = *_listAgent[agentIndex];
            if ( NavMeshRuntime::isLive( agent ) == false )
            {
                releaseAgent( &agent );
                continue;
            }
            NavMeshRuntime* pRuntime = agent._crowdIndex < _listRuntime.size() ? _listRuntime[agent._crowdIndex].get() : nullptr;
            if ( pRuntime == nullptr )
                pRuntime = findOrCreateRuntime( agent._agentType, NavMeshArrival::Defer );
            // 기하의 메시를 아직 읽는 중이라 마련을 미뤘다 — 실패로 두지 않고 다음 갱신에 다시 마련한다(목적지는 그대로 기다린다).
            if ( pRuntime == nullptr && _waitingMeshCount > 0 )
                continue;
            if ( pRuntime == nullptr || pRuntime->_pCrowd == nullptr )
            {
                if ( agent._bHasDestination == SW_TRUE )
                    agent._moveStatus = NavMoveStatus::Failed;
                continue;
            }
            NavAgentCrowdSync::syncAgentIntoCrowd( agent, *pRuntime, _listRuntime );
        }
    }

    void SceneNavigation::launchCrowdUpdates( float32 deltaTime )
    {
        if ( _listAgent.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.launchCrowd" );
        _crowdDeltaTime       = deltaTime;
        _bCrowdUpdateLaunched = SW_TRUE;
        const bool bWorkers   = engine::areEngineServicesBound();
        // 종류 하나의 군중은 일 하나가 통째로 진행한다(Detour 군중은 안에서 나누지 않는다) — 종류끼리는 서로 모르므로 같이 돈다. 결과는 스레드 수와 상관없다.
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            if ( pRuntime->_pCrowd == nullptr || pRuntime->_pCrowd->getActiveAgentCount() == 0 )
                continue;
            pRuntime->_crowdDeltaTime = deltaTime;
            if ( bWorkers == false )
            {
                pRuntime->updateCrowd(); // 워커가 없으면(테스트 · 도구) 지금 진행한다 — 쓰기는 그래도 ⑥ 이다
                continue;
            }
            TaskManager& taskManager = engine::getSubsystem<TaskManager>();
            TaskHandle   task        = taskManager.emplaceTask( "NavCrowdUpdate", SW_DELEGATE_METHOD( TaskDelegate, &NavMeshRuntime::updateCrowd, pRuntime.get() ) );
            taskManager.addToStage( _crowdStage, task, "NavCrowdUpdate" );
            task.submit();
            _bCrowdTasksInFlight = SW_TRUE;
        }
    }

    void SceneNavigation::waitCrowdUpdates()
    {
        if ( _bCrowdTasksInFlight == SW_FALSE )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.waitCrowd" );
        engine::getSubsystem<TaskManager>().waitStage( _crowdStage, "SceneNavigation" );
        _bCrowdTasksInFlight = SW_FALSE;
    }

    void SceneNavigation::completeFrameJob()
    {
        if ( _bCrowdUpdateLaunched == SW_FALSE )
            return;
        SW_MEMORY_SCOPE( Navigation );
        SW_PROFILE_SCOPE( "GT.Navigation.writeAgents" );
        waitCrowdUpdates();
        _bCrowdUpdateLaunched = SW_FALSE;
        _listAgentTransformWrite.clear();
        _listWrittenAgent.clear();
        for ( NavMeshAgentComponent* pAgent : _listAgent )
        {
            const bool bWritten = pAgent->_crowdAgentID != NavigationConstant::kInvalidAgentID && pAgent->_crowdIndex < _listRuntime.size() &&
                                  NavAgentCrowdSync::writeAgentResult( *pAgent, *_listRuntime[pAgent->_crowdIndex], _crowdDeltaTime, _listAgentTransformWrite );
            if ( bWritten )
                _listWrittenAgent.push_back( pAgent );
        }
        // 부모 없는 에이전트의 자리 · 회전은 한 번에 적용하고 펼친다(물리 `ScenePhysics::applyPoseWrites` 와 같은 길) — 쓴 번호를 펼친 뒤 읽어 에이전트마다 월드를 따로 합성하지 않는다.
        if ( _listAgentTransformWrite.empty() == false )
            (void)_pManager->applyTransformBatch( _listAgentTransformWrite.data(), static_cast<uint32>( _listAgentTransformWrite.size() ) );
        if ( _listWrittenAgent.empty() == false && _pManager->hasDirtySceneTransforms() )
            _pManager->flushSceneTransforms();
        for ( NavMeshAgentComponent* pAgent : _listWrittenAgent )
        {
            NavAgentCrowdSync::recordWrittenTransform( *pAgent );
        }
    }

    void SceneNavigation::drawDebug( IPhysicsDebugRenderer& renderer, uint32 flags ) const
    {
        NavDebugView::drawLines( renderer, flags, _listRuntime, _listObstacle );
    }

    void SceneNavigation::updateDebugView( uint32 flags )
    {
        if ( _pManager == nullptr )
            return;
        _debugView.updateSolidView( *_pManager, flags, _listRuntime, _listAgent );
    }

    AABB SceneNavigation::computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry )
    {
        return NavGeometryCollector::computeBakeBounds( pSurface, geometry );
    }

    void SceneNavigation::collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                           const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry )
    {
        // 쿠킹이 쓰는 길이다 — 런타임 베이크와 같은 기하(입력 해시)를 그 자리에서 내야 하므로 메시를 기다린다(`Wait` 는 늘 0 을 돌려준다).
        (void)NavGeometryCollector::collectGeometry( manager, pSurface, settings, listSource, NavMeshArrival::Wait, outGeometry );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct SceneNavigationWatchesInternal
        {
            /**
             * @brief 질의 대상(시작)에서 `key` 이름의 오브젝트(끝)까지 첫 베이크된 에이전트 종류의 경로를 찾습니다. 못 찾으면 까닭을 적고 false 입니다.
             * @details 시나리오가 "이 장애물이 길을 막아 돌아가는지 · 이 링크로 건너는지" 를 값으로 본다(언리얼 Navigation Test Actor 의 자리).
             */
            static bool findQueryPath( const AutomationWatchQuery& query, AutomationWatchValue& outValue, NavPath& outPath )
            {
                const GameObject*     pStart  = query.findTargetObject();
                const GameObject*     pEnd    = query._pManager != nullptr ? query._pManager->findGameObjectByName( hashed_string{ query._key } ) : nullptr;
                const SceneComponent* pFrom   = pStart != nullptr ? pStart->getPrimarySceneComponent() : nullptr;
                const SceneComponent* pTo     = pEnd != nullptr ? pEnd->getPrimarySceneComponent() : nullptr;
                const bool            bNoEnds = pFrom == nullptr || pTo == nullptr;
                if ( bNoEnds )
                {
                    outValue.setText( pStart == nullptr ? query.getTargetProblem() : "no end object named by key (with a scene component)" );
                    return false;
                }
                const SceneNavigation& navigation = query._pManager->getSubsystem<SceneNavigation>();
                vector<hashed_string>  listAgentType;
                navigation.collectBakedAgentTypes( listAgentType );
                if ( listAgentType.empty() )
                {
                    outValue.setText( "no navmesh baked yet" );
                    return false;
                }
                // 대상이 내비 에이전트면 그 종류의 경로다(행위자 종류마다 영역 비용이 다르다 — 화염 정령은 용암을 지난다). 아니면 첫 베이크된 종류다.
                const NavMeshAgentComponent* pAgent    = pStart->getComponent<NavMeshAgentComponent>();
                const hashed_string          agentType = pAgent != nullptr ? pAgent->getAgentType() : listAgentType.front();
                if ( pAgent != nullptr && navigation.findNavMesh( agentType ) == nullptr )
                {
                    outValue.setText( "the target's agent type has no navmesh baked yet" );
                    return false;
                }
                // 동기 질의를 남긴다 — 감시 값은 읽는 자리에서 값을 내야 하고 단계 하나에 질의 하나다(대상 수에 비례하지 않는다).
                if ( navigation.findPath( agentType, pFrom->getWorldPosition(), pTo->getWorldPosition(), outPath ) != NavPathStatus::Complete )
                {
                    outValue.setText( "no complete path" );
                    return false;
                }
                return true;
            }

            /** @brief 감시 값 `Nav.PathLength` 입니다. 대상에서 `key` 오브젝트까지 다듬은 경로의 길이(m)이고, 끝까지 가는 경로가 없으면 값이 없습니다. */
            [[nodiscard]] static bool readPathLength( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                NavPath path;
                if ( findQueryPath( query, outValue, path ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( path.computeLength() ) );
                return true;
            }

            /** @brief 감시 값 `Nav.AreaCost` 입니다. key `<에이전트 종류>:<영역>` 의 비용 배율이고, 통행 금지면 -1 입니다(지역 프로필의 내비 비용 모듈이 정한 값). */
            [[nodiscard]] static bool readAreaCost( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const size_t colon = query._key.find( ':' );
                if ( query._pManager == nullptr || colon == string_view::npos )
                {
                    outValue.setText( "key must be <agent type>:<area>" );
                    return false;
                }
                float32             cost     = 0.0f;
                bool                bAllowed = false;
                const hashed_string agentType{ query._key.substr( 0, colon ) };
                const hashed_string area{ query._key.substr( colon + 1 ) };
                if ( query._pManager->getSubsystem<SceneNavigation>().findAgentAreaCost( agentType, area, cost, bAllowed ) == false )
                {
                    outValue.setText( "navigation settings not read yet, or unknown agent type or area" );
                    return false;
                }
                outValue.setNumber( bAllowed ? static_cast<float64>( cost ) : -1.0 );
                return true;
            }

            /** @brief 감시 값 `Nav.CompletedPathRequests` — 수확한 비동기 경로 요청 수(누적)입니다. */
            [[nodiscard]] static bool readCompletedPathRequests( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SceneNavigation>().getCompletedPathRequestCount() );
                return true;
            }

            /** @brief 감시 값 `Nav.AgentCount` — 씬에 등록된 내비메시 에이전트 수입니다. */
            [[nodiscard]] static bool readAgentCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SceneNavigation>().getAgentCount() );
                return true;
            }

            /** @brief 감시 값 `Nav.AgentStatus` — 대상의 내비메시 에이전트 이동 상태 글(`Idle` · `Moving` · `Arrived` · `Failed`)입니다. 에이전트가 없으면 값이 없습니다. */
            [[nodiscard]] static bool readAgentStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pObject = query.findTargetObject();
                if ( pObject == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return false;
                }
                const NavMeshAgentComponent* pAgent = pObject->getComponent<NavMeshAgentComponent>();
                if ( pAgent == nullptr )
                {
                    outValue.setText( query.describeMissing( "navmesh agent" ) );
                    return false;
                }
                switch ( pAgent->getMoveStatus() )
                {
                    case NavMoveStatus::Idle:
                    {
                        outValue.setText( "Idle" );
                        break;
                    }
                    case NavMoveStatus::Moving:
                    {
                        outValue.setText( "Moving" );
                        break;
                    }
                    case NavMoveStatus::Arrived:
                    {
                        outValue.setText( "Arrived" );
                        break;
                    }
                    case NavMoveStatus::Failed:
                    {
                        outValue.setText( "Failed" );
                        break;
                    }
                }
                return true;
            }

            /** @brief 감시 값 `Nav.PathLinkCount` 입니다. 그 경로가 건너는 오프 메시 링크 수입니다. */
            [[nodiscard]] static bool readPathLinkCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                NavPath path;
                if ( findQueryPath( query, outValue, path ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( path._listLinkCrossing.size() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( navCompletedPathRequests, "Nav.CompletedPathRequests", "Async navmesh path requests harvested so far (cancelled ones are not counted)",
                         &SceneNavigationWatchesInternal::readCompletedPathRequests );
    SW_AUTOMATION_WATCH( navAgentCount, "Nav.AgentCount", "NavMesh agents registered in the scene", &SceneNavigationWatchesInternal::readAgentCount );
    SW_AUTOMATION_QUERY_WATCH( navAgentStatus, "Nav.AgentStatus", "Move status of the target's navmesh agent: Idle, Moving, Arrived or Failed", &SceneNavigationWatchesInternal::readAgentStatus, true, false );
    SW_AUTOMATION_QUERY_WATCH( navAreaCost, "Nav.AreaCost", "Path cost multiplier of a navigation area for an agent type (key = <agent type>:<area>; empty agent = default); -1 when the area is closed to it",
                               &SceneNavigationWatchesInternal::readAreaCost, false, true );
    SW_AUTOMATION_QUERY_WATCH( navPathLength, "Nav.PathLength", "Length (m) of the complete navmesh path from the target to the object named by key; absent without one",
                               &SceneNavigationWatchesInternal::readPathLength, true, true );
    SW_AUTOMATION_QUERY_WATCH( navPathLinkCount, "Nav.PathLinkCount", "Off-mesh links the navmesh path from the target to the object named by key crosses",
                               &SceneNavigationWatchesInternal::readPathLinkCount, true, true );
} // namespace sw
