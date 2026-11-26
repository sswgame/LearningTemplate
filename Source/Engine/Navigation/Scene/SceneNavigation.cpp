#include "pch.h"

#include "Engine/Navigation/Scene/SceneNavigation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Component/NavMeshLinkComponent.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/Scene/NavAgentCrowdSync.h"
#include "Engine/Navigation/Scene/NavGeometryCollector.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Navigation/Scene/NavStreamRoute.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

namespace sw
{
    namespace
    {
        constexpr float32 kNavIndexCellPerTile = 0.25f; ///< 기하 색인 셀은 타일보다 잘게 나눈다 — 타일 하나가 몇 셀만 훑게
    } // namespace
} // namespace sw

namespace sw
{
    // 군중 진행(⑤)은 `Navigation` 단계 끝에 워커로 가고 `FlushPostAnimation` 본문 앞에서 마친다(⑥) — 그 사이 `Animation` 단계와 겹친다.
    // 그 일이 만지는 씬 데이터는 내비게이션(군중 · 에이전트 이동 상태)뿐이다. 오브젝트 자리 · 컨트롤러 속도는 ⑥ 이 게임 스레드에서 쓴다.
    SW_SCENE_SUBSYSTEM_WITH_FRAME_JOB( SceneNavigation, Navigation, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Navigation, SceneDataKind::Transform }, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ), FlushPostAnimation, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Navigation }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    SceneNavigation::SceneNavigation( GameObjectManager& manager )
        : _settings{}
        , _sourcePath{}
        , _pCookedAsset{}
        , _listRuntime{}
        , _listAgent{}
        , _listSurface{}
        , _listObstacle{}
        , _listLink{}
        , _listGeometrySource{ "SceneNavigation.GeometrySource", SW_DELEGATE_METHOD( RegistrationList<INavGeometrySource>::ReleaseFunction, &SceneNavigation::releaseGeometrySource, this ) }
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

    void SceneNavigation::onWorldOriginShifted( const float3& delta )
    {
        for ( NavMeshAgentComponent* pAgent : _listAgent )
        {
            pAgent->shiftOrigin( delta );
        }
        for ( NavMeshLinkComponent* pLink : _listLink )
        {
            if ( pLink->_reportedBounds.isValid() )
                pLink->_reportedBounds = AABB{ pLink->_reportedBounds._min + delta, pLink->_reportedBounds._max + delta };
        }
    }

    void SceneNavigation::makePathLocal( NavPath& inoutPath ) const
    {
        const WorldOrigin& origin = _pManager->getWorldOrigin();
        if ( origin.isZero() )
            return;
        for ( vector<float3>* pListPoint : { &inoutPath._listPoint, &inoutPath._listCoarsePoint } )
        {
            for ( float3& point : *pListPoint )
            {
                point = origin.toLocal( point );
            }
        }
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
        (void)VectorUtil::removeSingleSwap( _listSurface, pSurface ); // 등록한 적 없는 표면이면 지울 것이 없다
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
        _tileRebaker.invalidateArea( _pManager->getWorldOrigin().toAbsolute( bounds ), bGeometryChanged );
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
        const optional<uint8>  foundArea = settings.findAreaIndex( areaName );
        if ( pType == nullptr || foundArea.has_value() == false )
        {
            SW_LOG_ERROR_ID( "SceneNavigation.AreaCostUnknown", "setAgentAreaCost: agent type '%#' or area '%#' is not in the navigation settings", agentType.c_str(), areaName.c_str() );
            return false;
        }
        const uint8       areaIndex = *foundArea;
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
        const optional<uint8>  foundArea = settings.findAreaIndex( areaName );
        if ( pType == nullptr || foundArea.has_value() == false )
            return false;
        const uint8    areaIndex = *foundArea;
        NavQueryFilter filter    = settings.makeDefaultFilter();
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
            if ( pSurface->isActive() && pSurface->supportsAgentType( agentType, defaultName ) )
                return pSurface;
        }
        return nullptr;
    }

    bool SceneNavigation::installCooked( NavMeshRuntime& runtime, const NavAgentTypeDef& agentType, uint64 inputHash )
    {
        // 스트리밍 씬은 셀이 반경 안에서만 오르므로 지금 모은 기하가 씬 전체가 아니다 — 전체 입력 해시 대신 타일마다 그 영역의 입력 해시를 타일을 올릴 때 견준다
        // (`updateTileStreaming`). 머리만 읽어 타일을 원점 둘레로 올리고 내린다(언리얼 World Partition 의 내비 데이터 청크).
        const bool                     bStreamTiles = _pManager != nullptr && _pManager->getSubsystem<SceneStreamingSubsystem>().isStreaming();
        const string                   cookedPath   = _pCookedAsset != nullptr ? string{ "(given)" } : NavMeshAsset::makeCookedPath( _sourcePath );
        shared_ptr<const NavMeshAsset> pAsset       = _pCookedAsset;
        if ( pAsset == nullptr )
        {
            if ( cookedPath.empty() || AssetIO::hasAsset( AssetID{ cookedPath } ) == false )
                return false;
            shared_ptr<NavMeshAsset> pLoaded = make_shared<NavMeshAsset>();
            const bool               bLoaded = bStreamTiles ? pLoaded->loadHeadFromAsset( AssetID{ cookedPath } ) : pLoaded->loadFromAsset( AssetID{ cookedPath } );
            if ( bLoaded == false )
                return false;
            pAsset = std::move( pLoaded );
        }
        const NavMeshAssetEntry* pEntry = pAsset->findEntry( agentType._name );
        if ( pEntry == nullptr )
            return false;
        if ( pEntry->_settingsHash != NavMeshSettings::computeAgentTypeHash( agentType ) || ( bStreamTiles == false && pEntry->_inputHash != inputHash ) )
        {
            SW_LOG_WARNING_ID( "SceneNavigation.CookedNavmeshDoesNot", "Cooked navmesh '%#' (%#) does not match the scene or the settings - baking at runtime", cookedPath.c_str(), agentType._name.c_str() );
            return false;
        }
        const Stopwatch stopwatch;
        if ( bStreamTiles )
        {
            if ( runtime._pNavMesh->initialize( agentType, _settings, pEntry->_bounds ) == false )
            {
                SW_LOG_WARNING_ID( "SceneNavigation.CookedNavmeshCouldNot", "Cooked navmesh '%#' (%#) could not be installed - baking at runtime", cookedPath.c_str(), agentType._name.c_str() );
                return false;
            }
            // 걸을 곳이 있는 타일만 자리를 둔다 — 빈 타일은 끼울 것이 없다. 타일은 다음 갱신부터 원점 둘레로 오른다.
            runtime._listStreamTile.clear();
            for ( size_t tileIndex = 0; tileIndex < pEntry->_listTile.size(); ++tileIndex )
            {
                const NavTileData&  tile      = pEntry->_listTile[tileIndex];
                const NavTileRange& range     = pEntry->_listTileRange.size() == pEntry->_listTile.size() ? pEntry->_listTileRange[tileIndex] : NavTileRange{ 0, tile._bytes.size() };
                const bool          bWalkable = pAsset->hasTileBytes() ? tile._bytes.empty() == false : range._size != 0;
                if ( bWalkable == false )
                    continue;
                NavStreamTile streamTile{};
                streamTile._tileX           = tile._tileX;
                streamTile._tileZ           = tile._tileZ;
                streamTile._range           = range;
                streamTile._pMemoryBytes    = pAsset->hasTileBytes() ? &tile._bytes : nullptr;
                streamTile._cookedInputHash = tileIndex < pEntry->_listTileInputHash.size() ? pEntry->_listTileInputHash[tileIndex] : 0; // 0 — 모른다(그 타일은 다시 굽는다)
                runtime._listStreamTile.push_back( std::move( streamTile ) );
            }
            // (Z, X) 순 — 타일 자리 찾기(`NavMeshRuntime::findStreamTile`)가 이분 찾기다. 장애물 캐시의 묶음 순서는 쿠킹본이 약속하지 않는다.
            VectorUtil::sort( runtime._listStreamTile, []( const NavStreamTile& lhs, const NavStreamTile& rhs )
            {
                return lhs._tileZ != rhs._tileZ ? lhs._tileZ < rhs._tileZ : lhs._tileX < rhs._tileX;
            } );
            runtime._clusterGraph         = pEntry->_clusterGraph;
            runtime._pStreamAsset         = pAsset;
            runtime._streamPath           = cookedPath;
            runtime._bounds               = pEntry->_bounds;
            runtime._bFromCooked          = SW_TRUE;
            runtime._bakeStats            = NavMeshBakeStats{};
            runtime._bakeStats._tileCount = static_cast<uint32>( runtime._pNavMesh->getTileGrid().getTileCount() );
            return true;
        }
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
        const ScopedMemoryTag memoryScope{ kSubsystemMemoryTag }; // 틱 밖(에디터 굽기 · 경로 질의의 `ensureNavMesh`)에서도 부른다 — 내비메시는 이 서비스의 몫이다
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
        const uint32                pendingMeshCount = NavGeometryCollector::collectGeometry( SceneIterationKey{}, *_pManager, pRuntime->_pSurface, _settings, _listGeometrySource.getItems(), arrival, *pGeometry );
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
            pGeometry->rebuildSpatialIndex( MathUtil::max( 1.0f, pRuntime->_pNavMesh->getTileGrid()._tileWorldSize * kNavIndexCellPerTile ) );
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

    NavMeshRuntime* SceneNavigation::getOrCreateRuntime( const hashed_string& agentType, NavMeshArrival arrival )
    {
        NavMeshRuntime* pRuntime = findRuntime( agentType );
        return pRuntime != nullptr ? pRuntime : createRuntime( agentType, arrival );
    }

    const INavMeshTileStore* SceneNavigation::ensureNavMesh( const hashed_string& agentType )
    {
        // 도구 · 테스트 · 쿠킹이 "지금" 마련하는 길이다 — 결과를 그 자리에서 쓰므로 메시를 그 자리에서 읽는다(갱신은 `Defer` 로 기다리지 않는다).
        NavMeshRuntime* pRuntime = getOrCreateRuntime( agentType, NavMeshArrival::Wait );
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
        // 타일마다 그 베이크 영역의 입력 해시 — 스트리밍 씬이 타일을 올릴 때 런타임 기하와 견주어 다른 타일만 다시 굽는다.
        outEntry._listTileInputHash.reserve( outEntry._listTile.size() );
        for ( const NavTileData& tile : outEntry._listTile )
        {
            outEntry._listTileInputHash.push_back( pRuntime->_pGeometry->computeRegionHash( pNavMesh->computeTileBakeRegion( tile._tileX, tile._tileZ ) ) );
        }
        // 모든 타일이 끼워진 지금의 대략 그래프 — 스트리밍 씬이 오르지 않은 구역을 지나는 먼 경로를 이것으로 정한다(`NavStreamRoute`).
        pNavMesh->collectClusterGraph( outEntry._clusterGraph );
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
        return findPath( agentType, start, end, pRuntime->_filter, outPath );
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
        const WorldOrigin&  origin = _pManager->getWorldOrigin();
        const NavPathStatus status = pRuntime->_pNavMesh->findPath( origin.toAbsolute( start ), origin.toAbsolute( end ), pRuntime->_searchExtent, filter, outPath );
        makePathLocal( outPath );
        return status;
    }

    NavPathStatus SceneNavigation::findStreamingPath( const hashed_string& agentType, const float3& start, const float3& end, NavPath& outPath ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
        {
            outPath = NavPath{};
            return NavPathStatus::Failed;
        }
        const WorldOrigin&  origin = _pManager->getWorldOrigin();
        const NavPathStatus status = NavStreamRoute::findPath( *pRuntime, origin.toAbsolute( start ), origin.toAbsolute( end ), pRuntime->_searchExtent, pRuntime->_filter, outPath );
        makePathLocal( outPath );
        return status;
    }

    void SceneNavigation::collectStreamStats( vector<NavStreamStats>& outListStats ) const
    {
        outListStats.clear();
        for ( size_t runtimeIndex = 0; runtimeIndex < _listRuntime.size(); ++runtimeIndex )
        {
            const NavMeshRuntime& runtime = *_listRuntime[runtimeIndex];
            NavStreamStats        stats{};
            stats._agentType        = runtime._agentType;
            stats._clusterCount     = static_cast<uint32>( runtime._clusterGraph.getClusterList().size() );
            stats._clusterEdgeCount = static_cast<uint32>( runtime._clusterGraph.getEdges().size() );
            stats._streamTileCount  = static_cast<uint32>( runtime._listStreamTile.size() );
            stats._replanCount      = runtime._routeReplanCount;
            for ( const NavStreamTile& tile : runtime._listStreamTile )
            {
                stats._loadedTileCount += tile._state == NavStreamTile::State::Loaded ? 1u : 0u;
                stats._loadingTileCount += tile._state == NavStreamTile::State::Loading ? 1u : 0u;
                stats._failedTileCount += tile._state == NavStreamTile::State::Failed ? 1u : 0u;
            }
            for ( const NavMeshAgentComponent* pAgent : _listAgent )
            {
                if ( pAgent->_crowdIndex != runtimeIndex )
                    continue;
                stats._coarseRouteAgentCount += pAgent->isFollowingCoarseRoute() ? 1u : 0u;
                stats._virtualAgentCount += pAgent->isInVirtualMove() ? 1u : 0u;
            }
            outListStats.push_back( stats );
        }
    }

    NavPathTicket SceneNavigation::requestPath( const hashed_string& agentType, const float3& start, const float3& end )
    {
        NavPathJob job;
        job._agentType = agentType;
        job._start     = _pManager->getWorldOrigin().toAbsolute( start ); // 풀이는 절대 자리에서 한다 — 원점이 그 사이 옮겨져도 같은 길이다
        job._end       = _pManager->getWorldOrigin().toAbsolute( end );
        return _pathQueue.request( std::move( job ) );
    }

    NavPathTicket SceneNavigation::requestPath( const hashed_string& agentType, const float3& start, const float3& end, const NavQueryFilter& filter )
    {
        NavPathJob job;
        job._agentType     = agentType;
        job._start         = _pManager->getWorldOrigin().toAbsolute( start );
        job._end           = _pManager->getWorldOrigin().toAbsolute( end );
        job._filter        = filter;
        job._bCustomFilter = true;
        return _pathQueue.request( std::move( job ) );
    }

    PathRequestState SceneNavigation::takePathResult( NavPathTicket& inoutTicket, NavPath& outPath )
    {
        NavPathJob             job;
        const PathRequestState state = _pathQueue.takeResult( inoutTicket, job );
        if ( state == PathRequestState::Ready )
        {
            outPath = std::move( job._path );
            makePathLocal( outPath );
        }
        return state;
    }

    bool SceneNavigation::findNearestPoint( const hashed_string& agentType, const float3& position, NavLocation& outLocation ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
            return false;
        const WorldOrigin& origin = _pManager->getWorldOrigin();
        if ( pRuntime->_pNavMesh->findNearestPoint( origin.toAbsolute( position ), pRuntime->_searchExtent, pRuntime->_filter, outLocation ) == false )
            return false;
        outLocation._position = origin.toLocal( outLocation._position );
        return true;
    }

    bool SceneNavigation::raycast( const hashed_string& agentType, const float3& start, const float3& end, NavRaycastHit& outHit ) const
    {
        const NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
            return false;
        const WorldOrigin& origin = _pManager->getWorldOrigin();
        if ( pRuntime->_pNavMesh->raycast( origin.toAbsolute( start ), origin.toAbsolute( end ), pRuntime->_searchExtent, pRuntime->_filter, outHit ) == false )
            return false;
        outHit._position = origin.toLocal( outHit._position );
        return true;
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
        SW_PROFILE_SCOPE( "GT.Navigation.tick" );
        // ⓪ 지난 프레임에 넘긴 경로 질의를 수확한다 — 내비메시가 없어도 돈다(요청자는 `Failed` 결과를 받아야 기다림이 끝난다).
        const NavMeshWriteAccess access = completeNavMeshReads();
        if ( bAnything == false || _pManager == nullptr )
        {
            launchPathRequests();
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
                    (void)getOrCreateRuntime( hashed_string{}, NavMeshArrival::Defer );
                    continue;
                }
                for ( const hashed_string& agentType : pSurface->getAgentTypes() )
                {
                    (void)getOrCreateRuntime( agentType, NavMeshArrival::Defer );
                }
            }
        }
        // 순서에 뜻이 있다 — 끝난 타일 끼우기는 읽는 워커가 없는 이 자리(⓪ 의 증표)에서만, 새 일(베이크 · 경로 질의 · 군중)은 끼운 뒤에 넘긴다.
        _tileRebaker.updateObstacles( _listRuntime, _listObstacle, getSettings()._obstacleMoveThreshold );
        processDirtyAreas( NavMeshArrival::Defer );
        updateTileStreaming( access );
        _tileRebaker.applyFinishedBakes( _listRuntime, access );
        NavTileRebaker::updateObstacleCaches( _listRuntime, deltaTime, access ); // 캐시 종류는 장애물 요청을 층에서 바로 짓는다(끼우기와 같은 자리)
        applyLinkStates( access );
        NavTileRebaker::launchTileBakes( _listRuntime, _settings._maxConcurrentTileBakeCount );
        syncAgentsIntoCrowds();
        launchPathRequests();
        launchCrowdUpdates( deltaTime );
    }

    void SceneNavigation::launchPathRequests()
    {
        // 한 갱신의 경로 일은 설정의 상한까지다 — 몰린 요청이 한 프레임을 붙들지 않고 다음 갱신으로 넘어간다(요청 순서는 그대로).
        _pathQueue.launch( "NavPathQuery", engine::areEngineSubsystemsBound() ? &engine::getSubsystem<TaskManager>() : nullptr, getSettings()._maxPathRequestsPerUpdate );
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

    void SceneNavigation::updateTileStreaming( const NavMeshWriteAccess& /*access*/ )
    {
        if ( _pManager == nullptr )
            return;
        const SceneStreamingSubsystem& streaming = _pManager->getSubsystem<SceneStreamingSubsystem>();
        if ( streaming.isStreaming() == false )
            return;
        const span<const StreamingSource> listSource       = streaming.getActiveSources();
        const float32                     unloadMargin     = streaming.getUnloadMargin();
        const bool                        bFinishEachFrame = engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_TRUE;

        // 셀이 오르내렸으면 그 구역의 입력이 바뀌었다 — 기하를 다시 모으고 닿은 타일의 입력 해시를 다시 잰다(언리얼 내비 타일 무효화).
        vector<StreamingCellBounds> listChangedBounds;
        const uint64                changeSerial = streaming.getChangeSerial();
        if ( changeSerial != _streamingChangeSerial )
            streaming.collectBoundsChangedSince( _streamingChangeSerial, listChangedBounds );
        bool       bChangeConsumed     = true;
        const auto overlapsChangedCell = [&listChangedBounds]( const AABB& region )
        {
            for ( const StreamingCellBounds& bounds : listChangedBounds )
            {
                if ( bounds._minX <= region._max._x && bounds._maxX >= region._min._x && bounds._minZ <= region._max._z && bounds._maxZ >= region._min._z )
                    return true;
            }
            return false;
        };

        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            NavMeshRuntime& runtime = *pRuntime;
            if ( runtime._listStreamTile.empty() )
                continue;
            const NavTileGrid& grid = runtime._pNavMesh->getTileGrid();

            bool bGeometryRefreshed = false;
            if ( listChangedBounds.empty() == false )
            {
                shared_ptr<NavMeshGeometry> pGeometry        = make_shared<NavMeshGeometry>();
                const uint32                pendingMeshCount = NavGeometryCollector::collectGeometry( SceneIterationKey{}, *_pManager, runtime._pSurface, _settings, _listGeometrySource.getItems(),
                                                                                                      NavMeshArrival::Defer, *pGeometry );
                if ( pendingMeshCount > 0 )
                {
                    bChangeConsumed = false; // 오른 셀의 메시를 아직 읽는 중이다 — 덜 모인 기하로 재지 않고 다음 갱신에 다시 모은다
                }
                else
                {
                    pGeometry->rebuildSpatialIndex( MathUtil::max( 1.0f, grid._tileWorldSize * kNavIndexCellPerTile ) );
                    runtime._pGeometry = std::move( pGeometry ); // 다시 베이크는 이 지금 기하를 읽는다(옛 일은 옛 사본을 끝까지 읽는다)
                    bGeometryRefreshed = true;
                }
            }

            // 쿠킹본 타일을 구간으로 읽기 시작한다 — 메모리 쿠킹본이면 그 자리에서 결과를 채운다. IO 우선순위는 셀 읽기와 같은 값이다(그 자리 셀의 우선순위 → `toAsyncIOPriority`).
            const auto startCookedRead = [&runtime]( NavStreamTile& tile, StreamingSourcePriority priority ) -> bool
            {
                if ( tile._pMemoryBytes != nullptr )
                {
                    tile._pRead         = make_shared<NavStreamTile::Read>();
                    tile._pRead->_bytes = *tile._pMemoryBytes;
                    tile._pRead->_state.store( 1, std::memory_order_release );
                    return true;
                }
                const shared_ptr<NavStreamTile::Read> pRead = make_shared<NavStreamTile::Read>();
                tile._readHandle                            = AssetIO::readBinaryAssetRangeAsync( AssetID{ runtime._streamPath }, tile._range._offset, tile._range._size, toAsyncIOPriority( priority ),
                                                                                                  SW_DELEGATE_LAMBDA( AssetReadCompleteDelegate, [pRead]( bool bSuccess, vector<uint8>& bytes )
                                           {
                    if ( bSuccess )
                        pRead->_bytes.swap( bytes );
                    pRead->_state.store( static_cast<uint8>( bSuccess ? 1 : 2 ), std::memory_order_release );
                } ) );
                if ( tile._readHandle.isValid() == false )
                {
                    SW_LOG_ERROR_ID( "SceneNavigation.NavTileReadNotStarted", "Navmesh '%#' could not start reading tile (%#, %#) from '%#' - it stays out", runtime._agentType.c_str(), tile._tileX,
                                     tile._tileZ, runtime._streamPath.c_str() );
                    return false;
                }
                tile._pRead = pRead;
                return true;
            };
            // 타일의 입력이 쿠킹 때와 같으면 쿠킹본 타일을 읽고, 다르면 그 타일만 지금 기하로 다시 굽는다(이웃 타일은 쿠킹본 그대로).
            const WorldOrigin& worldOrigin      = _pManager->getWorldOrigin();
            const auto         resolveTileInput = [&runtime, &startCookedRead, &streaming, &worldOrigin]( NavStreamTile& tile, const AABB& region )
            {
                const StreamingSourcePriority priority = streaming.computePriorityAt( worldOrigin.toLocal( region.getCenter() ) ); // 타일은 절대, 묻는 자리는 로컬이다
                tile._geometryHash                     = runtime._pGeometry != nullptr ? runtime._pGeometry->computeRegionHash( region ) : 0;
                if ( tile._cookedInputHash != 0 && tile._geometryHash == tile._cookedInputHash )
                {
                    tile._state = startCookedRead( tile, priority ) ? NavStreamTile::State::Loading : NavStreamTile::State::Failed;
                    return;
                }
                tile._state = NavStreamTile::State::Loaded; // 자리는 끼운 것으로 친다 — 다시 베이크 결과를 받는다
                runtime.addDirtyTile( int2{ tile._tileX, tile._tileZ } );
            };

            for ( NavStreamTile& tile : runtime._listStreamTile )
            {
                // 타일의 XZ 사각형과 원점 사이 거리를 원점마다 그 반경과 잰다 — 셀과 같은 재기(`StreamingCellGrid::computeCellDistanceSquared` · 원점 반경)다.
                const AABB tileBounds = grid.computeTileBounds( tile._tileX, tile._tileZ, 0.0f, 0.0f );
                bool       bInLoad    = false;
                bool       bInKeep    = false; // 어느 원점이든 (반경 + 여유) 안이면 내리지 않는다
                for ( const StreamingSource& source : listSource )
                {
                    const float32 dx              = MathUtil::max( MathUtil::max( tileBounds._min._x - source._position._x, 0.0f ), source._position._x - tileBounds._max._x );
                    const float32 dz              = MathUtil::max( MathUtil::max( tileBounds._min._z - source._position._z, 0.0f ), source._position._z - tileBounds._max._z );
                    const float32 distanceSquared = dx * dx + dz * dz;
                    const float32 keepRadius      = source._loadingRadius + unloadMargin;
                    bInLoad                       = bInLoad || distanceSquared <= source._loadingRadius * source._loadingRadius;
                    bInKeep                       = bInKeep || distanceSquared <= keepRadius * keepRadius;
                }
                const int2 tileCoord{ tile._tileX, tile._tileZ };
                if ( bInKeep == false )
                {
                    if ( tile._state == NavStreamTile::State::Loading || tile._state == NavStreamTile::State::Loaded )
                    {
                        if ( tile._state == NavStreamTile::State::Loading )
                            (void)tile._readHandle.cancel(); // 결과는 완료 콜백이 쥔 사본과 함께 사라진다
                        tile._pRead.reset();
                        tile._readHandle = AsyncReadHandle{};
                        // 바이트가 빈 타일은 그 자리를 뺀다(`dtNavMesh::removeTile`) — 그 구역의 질의 · 군중은 "경로 없음" 이 된다. 끼운 적 없는 자리면 할 일이 없다.
                        NavTileData removal{};
                        removal._tileX = tile._tileX;
                        removal._tileZ = tile._tileZ;
                        (void)runtime._pNavMesh->replaceTile( removal );
                        tile._geometryHash = 0;
                        tile._state        = NavStreamTile::State::Unloaded;
                    }
                    continue;
                }
                const AABB region = runtime._pNavMesh->computeTileBakeRegion( tile._tileX, tile._tileZ );
                if ( tile._state == NavStreamTile::State::Unloaded && bInLoad )
                {
                    resolveTileInput( tile, region );
                }
                else if ( tile._state == NavStreamTile::State::Loaded && bGeometryRefreshed && overlapsChangedCell( region ) )
                {
                    // 오르내린 셀이 이 타일 영역에 닿았다 — 입력이 바뀌었으면 다시 정한다(같으면 그대로 둔다).
                    if ( runtime._pGeometry->computeRegionHash( region ) != tile._geometryHash )
                        resolveTileInput( tile, region );
                }
                if ( tile._state != NavStreamTile::State::Loading )
                    continue;
                if ( bFinishEachFrame && tile._readHandle.isValid() )
                    tile._readHandle.wait();
                const uint8 readState = tile._pRead->_state.load( std::memory_order_acquire );
                if ( readState == 0 )
                    continue;
                NavTileData install{};
                install._tileX = tile._tileX;
                install._tileZ = tile._tileZ;
                install._bytes = std::move( tile._pRead->_bytes );
                tile._pRead.reset();
                tile._readHandle = AsyncReadHandle{};
                if ( readState != 1 || runtime._pNavMesh->replaceTile( install ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneNavigation.NavTileStreamFailed", "Navmesh '%#' could not stream in tile (%#, %#) - it stays out", runtime._agentType.c_str(), tile._tileX, tile._tileZ );
                    tile._state = NavStreamTile::State::Failed;
                    continue;
                }
                tile._state = NavStreamTile::State::Loaded;
                // 쿠킹본에는 장애물이 없다 — 끼운 타일에 닿는 장애물이 있으면 그 타일을 다시 베이크한다(쿠킹본을 처음 끼울 때와 같다).
                if ( runtime._pVolume == nullptr )
                    continue;
                vector<int2> listTouched;
                for ( const NavConvexVolume& volume : *runtime._pVolume )
                {
                    runtime._pNavMesh->collectTilesOverlapping( volume.computeBounds(), listTouched );
                    for ( const int2& touched : listTouched )
                    {
                        if ( touched._x == tileCoord._x && touched._y == tileCoord._y )
                            runtime.addDirtyTile( tileCoord );
                    }
                }
            }
        }
        if ( bChangeConsumed )
            _streamingChangeSerial = changeSerial;
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
                pRuntime = getOrCreateRuntime( agent._agentType, NavMeshArrival::Defer );
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
        const bool bWorkers   = engine::areEngineSubsystemsBound();
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
        NavDebugView::drawLines( renderer, flags, _pManager->getWorldOrigin(), _listRuntime, _listObstacle, _listAgent );
    }

    void SceneNavigation::updateDebugView( bool bShown )
    {
        if ( _pManager == nullptr )
            return;
        _debugView.updateSolidView( *_pManager, bShown, _listRuntime, _listAgent );
    }

    AABB SceneNavigation::computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry )
    {
        return NavGeometryCollector::computeBakeBounds( pSurface, geometry );
    }

    void SceneNavigation::collectGeometry( SceneIterationKey key, GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                           const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry )
    {
        // 쿠킹이 쓰는 길이다 — 런타임 베이크와 같은 기하(입력 해시)를 그 자리에서 내야 하므로 메시를 기다린다(`Wait` 는 늘 0 을 돌려준다).
        (void)NavGeometryCollector::collectGeometry( key, manager, pSurface, settings, listSource, NavMeshArrival::Wait, outGeometry );
    }
} // namespace sw
