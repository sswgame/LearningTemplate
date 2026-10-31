#include "pch.h"

#include "Engine/Navigation/Component/SceneNavigation.h"

#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Navigation/Component/NavAgentCrowdSync.h"
#include "Engine/Navigation/Component/NavGeometryCollector.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Component/NavMeshLinkComponent.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Navigation/Component/NavMeshRuntime.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigation" );

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
    SW_SCENE_SERVICE( SceneNavigation, Navigation )

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
        , _tileRebaker{}
        , _debugView{}
        , _pManager{ &manager }
        , _waitingMeshCount{ 0 }
        , _bSettingsLoaded{ false }
        , _bIgnoreCooked{ false }
    {
    }

    SceneNavigation::~SceneNavigation()
    {
        onSceneCleared();
    }

    void SceneNavigation::onSceneCleared()
    {
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
    }

    void SceneNavigation::setSettings( const NavMeshSettings& settings )
    {
        onSceneCleared();
        _settings        = settings;
        _bSettingsLoaded = true;
        _listFailedAgentType.clear();
    }

    const NavMeshSettings& SceneNavigation::getSettings()
    {
        if ( _bSettingsLoaded == false )
        {
            _bSettingsLoaded = true;
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
        _tileRebaker.forgetObstacle( *pObstacle, _listRuntime ); // 다시 베이크 종류는 그 자리를 알리고, 캐시 종류는 캐시 장애물을 뺀다
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

    void SceneNavigation::applyLinkStates()
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
        SW_LOG_ERROR( "Navigation agent type '%#' is not in the navigation settings", agentType.c_str() );
        return nullptr;
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
            SW_LOG_WARNING( "Cooked navmesh '%#' (%#) does not match the scene or the settings - baking at runtime", cookedPath.c_str(), agentType._name.c_str() );
            return false;
        }
        const Stopwatch stopwatch;
        if ( runtime._pNavMesh->initialize( agentType, _settings, pEntry->_bounds ) == false ||
             NavMeshBakeUtil::installTiles( *runtime._pNavMesh, pEntry->_listTile ) == false )
        {
            SW_LOG_WARNING( "Cooked navmesh '%#' (%#) could not be installed - baking at runtime", cookedPath.c_str(), agentType._name.c_str() );
            return false;
        }
        runtime._bounds                     = pEntry->_bounds;
        runtime._bFromCooked                = true;
        runtime._bakeStats                  = NavMeshBakeStats{};
        runtime._bakeStats._milliseconds    = static_cast<float64>( stopwatch.getElapsedMicroseconds() ) / constant::kMicrosecondsPerMillisecond;
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
        const float32 reach                 = MathUtil::max( 1.0f, pType->_radius * 4.0f );
        pRuntime->_searchExtent             = float3{ reach, MathUtil::max( 1.0f, pType->_height ), reach };

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
            SW_LOG_INFO( "Navmesh '%#' loaded from the cooked file: %# tiles, %# polygons, %# ms", pType->_name.c_str(), pRuntime->_bakeStats._filledTileCount,
                         pRuntime->_bakeStats._polygonCount, pRuntime->_bakeStats._milliseconds );
        }
        else
        {
            if ( pGeometry->isEmpty() )
                SW_LOG_WARNING( "Navmesh '%#' has no geometry to bake (no visible static meshes or static bodies)", pType->_name.c_str() );
            const AABB bounds = NavGeometryCollector::computeBakeBounds( pRuntime->_pSurface, *pGeometry );
            if ( pRuntime->_pNavMesh->initialize( *pType, _settings, bounds ) == false )
            {
                _listFailedAgentType.push_back( pType->_name );
                return nullptr;
            }
            pRuntime->_bounds = bounds;
            (void)NavMeshBakeUtil::bakeAllTiles( *pRuntime->_pNavMesh, *pGeometry, pVolume.get(), &pRuntime->_bakeStats );
            SW_LOG_INFO( "Navmesh '%#' baked: %# of %# tiles, %# polygons, %# triangles in, %# ms", pType->_name.c_str(), pRuntime->_bakeStats._filledTileCount,
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

    INavMesh* SceneNavigation::ensureNavMesh( const hashed_string& agentType )
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
        _bIgnoreCooked = true;
        for ( const hashed_string& agentType : listAgentType )
        {
            (void)ensureNavMesh( agentType ); // 모르는 종류 · 기하 없음은 ensureNavMesh 가 이미 남긴다 — 센 수가 말해 준다
        }
        _bIgnoreCooked = false;
        return static_cast<uint32>( _listRuntime.size() );
    }

    bool SceneNavigation::makeCookedEntry( const hashed_string& agentType, NavMeshAssetEntry& outEntry )
    {
        outEntry                            = NavMeshAssetEntry{};
        const INavMesh* pNavMesh            = ensureNavMesh( agentType );
        NavMeshRuntime* pRuntime            = findRuntime( agentType );
        const bool      bRuntimeUnavailable = pNavMesh == nullptr || pRuntime == nullptr || pRuntime->_pGeometry == nullptr;
        if ( bRuntimeUnavailable )
            return false;
        outEntry._agentType    = pRuntime->_agentType;
        outEntry._settingsHash = NavMeshSettings::computeAgentTypeHash( pNavMesh->getAgentType() );
        outEntry._inputHash    = pRuntime->_pGeometry->computeHash();
        outEntry._bounds       = pRuntime->_bounds;
        pNavMesh->collectTiles( outEntry._listTile );
        return true;
    }

    const INavMesh* SceneNavigation::findNavMesh( const hashed_string& agentType ) const
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
        if ( bAnything == false || _pManager == nullptr )
            return;
        SW_MEMORY_SCOPE( Navigation );
        SW_PROFILE_SCOPE( "GT.Navigation.tick" );
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
        // 순서에 뜻이 있다 — 끝난 타일 끼우기는 질의가 없는 이 자리(컴포넌트 틱 뒤)에서만, 새 일은 끼운 뒤에 넘긴다.
        _tileRebaker.updateObstacles( _listRuntime, _listObstacle, getSettings()._obstacleMoveThreshold );
        processDirtyAreas( NavMeshArrival::Defer );
        _tileRebaker.applyFinishedBakes( _listRuntime );
        NavTileRebaker::updateObstacleCaches( _listRuntime, deltaTime ); // 캐시 종류는 장애물 요청을 층에서 바로 짓는다(끼우기와 같은 자리)
        applyLinkStates();
        NavTileRebaker::launchTileBakes( _listRuntime, _settings._maxConcurrentTileBakeCount );
        updateAgents( deltaTime );
    }

    void SceneNavigation::processDirtyAreas( NavMeshArrival arrival )
    {
        _tileRebaker.processDirtyAreas( _listRuntime, _listObstacle, *_pManager, _settings, _listGeometrySource.getItems(), arrival );
    }

    void SceneNavigation::flushTileBakes()
    {
        _tileRebaker.updateObstacles( _listRuntime, _listObstacle, getSettings()._obstacleMoveThreshold );
        // 그 자리에서 끝내는 길이다(테스트 · 쿠킹) — 메시를 기다린다. `Defer` 면 넘긴 영역이 남아 아래 반복이 끝나지 않는다.
        processDirtyAreas( NavMeshArrival::Wait );
        while ( getPendingTileCount() > 0 )
        {
            NavTileRebaker::launchTileBakes( _listRuntime, _settings._maxConcurrentTileBakeCount );
            NavTileRebaker::waitForAllBakes( _listRuntime );
            _tileRebaker.applyFinishedBakes( _listRuntime );
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

    void SceneNavigation::updateAgents( float32 deltaTime )
    {
        if ( _listAgent.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.agents" );
        for ( size_t agentIndex = 0; agentIndex < _listAgent.size(); ++agentIndex )
        {
            NavMeshAgentComponent& agent = *_listAgent[agentIndex];
            if ( SceneNavigationUtil::isLive( agent ) == false )
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
        {
            SW_PROFILE_SCOPE( "GT.Navigation.crowd" );
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
            {
                if ( pRuntime->_pCrowd != nullptr && pRuntime->_pCrowd->getActiveAgentCount() > 0 )
                    pRuntime->_pCrowd->update( deltaTime );
            }
        }
        for ( NavMeshAgentComponent* pAgent : _listAgent )
        {
            if ( pAgent->_crowdAgentID != NavigationConstant::kInvalidAgentID && pAgent->_crowdIndex < _listRuntime.size() )
                NavAgentCrowdSync::writeAgentResult( *pAgent, *_listRuntime[pAgent->_crowdIndex], deltaTime );
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
