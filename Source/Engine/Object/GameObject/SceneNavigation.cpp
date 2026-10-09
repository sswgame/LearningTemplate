#include "pch.h"

#include "Engine/Object/GameObject/SceneNavigation.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshModifierComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshObstacleComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshSurfaceComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigation" );

    namespace
    {
        struct SceneNavigationInternal
        {
            /** @brief 바깥이 오브젝트를 이보다 멀리 옮기면 순간이동으로 봅니다(미터). */
            static constexpr float32 kTeleportDistance = 0.5f;
            /** @brief 장애물 회전이 이보다 바뀌면 다시 뚫습니다(라디안). */
            static constexpr float32 kObstacleTurnThreshold = 0.1f;
            /** @brief 몸을 돌리기 시작하는 속도(m/s)입니다. */
            static constexpr float32 kTurnSpeed = 0.05f;

            /** @brief 오브젝트 하나를 베이크에 넣을지 · 영역입니다. */
            struct ObjectVerdict
            {
                uint8 _area{ 0 };
                bool  _bIncluded{ false };
            };

            static float32 computeDistance2D( const float3& from, const float3& to )
            {
                const float32 deltaX = to._x - from._x;
                const float32 deltaZ = to._z - from._z;
                return MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
            }

            /** @brief 에이전트가 속도를 넘길 캐릭터 컨트롤러입니다 — 방식이 `CharacterController` 이고 오브젝트에 있을 때만, 아니면 nullptr 입니다. */
            static CharacterControllerComponent* findDrivenController( const NavMeshAgentComponent& agent )
            {
                if ( agent.getDriveMode() != NavAgentDriveMode::CharacterController )
                    return nullptr;
                return agent.getOwner()->getComponent<CharacterControllerComponent>();
            }

            /**
             * @brief 오브젝트와 그 조상을 거슬러 올라가며 빼는 규칙 · 영역 수정자를 봅니다(가장 가까운 수정자가 이긴다).
             * @details 움직이는 것(에이전트 · 캐릭터 컨트롤러 · 스킨드 메시 · Static 이 아닌 강체)이 든 계층은 통째로 뺀다 — 베이크하는 순간 그 자리에 있던
             *          캐릭터 · 손에 든 무기가 바닥에 구멍을 내지 않게.
             */
            static ObjectVerdict evaluateObject( const GameObject& object, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings )
            {
                ObjectVerdict verdict{};
                bool          bAreaSet = false;
                for ( const GameObject* pCurrent = &object; pCurrent != nullptr; pCurrent = pCurrent->getParent() )
                {
                    const NavMeshModifierComponent* pModifier = pCurrent->getComponent<NavMeshModifierComponent>();
                    if ( pModifier != nullptr && pModifier->isActive() )
                    {
                        if ( pModifier->isIgnoredFromBuild() )
                            return verdict;
                        if ( bAreaSet == false && pModifier->getAreaName().empty() == false )
                        {
                            bAreaSet = true;
                            if ( settings.findAreaIndex( pModifier->getAreaName(), verdict._area ) == false )
                            {
                                SW_LOG_ERROR( "NavMesh modifier on '%#' names unknown area '%#' - baked as plain ground", pCurrent->getName().c_str(),
                                              pModifier->getAreaName().c_str() );
                                verdict._area = 0;
                            }
                        }
                    }
                    if ( pSurface != nullptr )
                    {
                        for ( const TagID& tag : pSurface->getExcludeTags() )
                        {
                            if ( tag.isValid() && pCurrent->hasTag( tag ) )
                                return verdict;
                        }
                    }
                    const bool bMover = pCurrent->getComponent<NavMeshAgentComponent>() != nullptr ||
                                        pCurrent->getComponent<CharacterControllerComponent>() != nullptr ||
                                        pCurrent->getComponent<SkeletalMeshComponent>() != nullptr;
                    if ( bMover )
                        return verdict;
                    const RigidBodyComponent* pBody = pCurrent->getComponent<RigidBodyComponent>();
                    if ( pBody != nullptr && pBody->getBodyType() != PhysicsBodyType::Static )
                        return verdict;
                }
                verdict._bIncluded = true;
                return verdict;
            }

            /** @brief 표면의 경계 상자입니다. 정하지 않았으면 false 입니다. */
            static bool findSurfaceBox( const NavMeshSurfaceComponent* pSurface, AABB& outBox )
            {
                if ( pSurface == nullptr )
                    return false;
                const float3&         half   = pSurface->getBoundsHalfExtents();
                const bool            bBound = half._x > 0.0f && half._y > 0.0f && half._z > 0.0f;
                const GameObject*     pOwner = pSurface->getOwner();
                const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
                if ( bBound == false || pScene == nullptr )
                    return false;
                const float3 center = pScene->getWorldPosition();
                outBox              = AABB{ center - half, center + half };
                return true;
            }

            static bool isLive( const Component& component )
            {
                const GameObject* pOwner = component.getOwner();
                return component.hasBegunPlay() && component.isActive() && component.isPendingDestroy() == false && pOwner != nullptr &&
                       pOwner->isActiveInHierarchy();
            }

            /** @brief 디버그 메시를 걷는 면 위로 띄우는 높이입니다(바닥과 겹쳐 깜빡이지 않게). */
            static constexpr float32 kViewLift = 0.04f;

            static float4 makeAreaColor( uint8 area, size_t triangleIndex )
            {
                const float4 arrColor[4] = {
                    float4{0.10f, 0.75f, 0.85f, 1.0f},
                    float4{0.35f, 0.80f, 0.25f, 1.0f},
                    float4{0.55f, 0.40f, 0.20f, 1.0f},
                    float4{0.15f, 0.30f, 0.90f, 1.0f},
                };
                const float4& base  = arrColor[area % 4];
                const float32 shade = ( triangleIndex / 2 ) % 2 == 0 ? 1.0f : 0.8f;
                return float4{ base._x * shade, base._y * shade, base._z * shade, 1.0f };
            }

            static RHIVertex makeVertex( const float3& position, const float4& color )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = position._x;
                vertex._arrPosition[1] = position._y;
                vertex._arrPosition[2] = position._z;
                vertex._arrNormal[1]   = 1.0f;
                vertex._arrColor[0]    = color._x;
                vertex._arrColor[1]    = color._y;
                vertex._arrColor[2]    = color._z;
                vertex._arrColor[3]    = color._w;
                return vertex;
            }

            /** @brief 두 점을 잇는 폭 0.12 m 띠(위를 보는 사각형 둘 — 양면)를 더합니다. */
            static void appendRibbon( const float3& from, const float3& to, const float4& color, vector<RHIVertex>& outListVertex )
            {
                const float3  delta  = to - from;
                const float32 length = computeDistance2D( from, to );
                if ( length < 1.0e-3f )
                    return;
                const float3 side{ -delta._z / length * 0.06f, 0.0f, delta._x / length * 0.06f };
                const float3 lift{ 0.0f, 0.1f, 0.0f };
                const float3 arrCorner[4] = { from - side + lift, from + side + lift, to + side + lift, to - side + lift };
                const uint32 arrIndex[12] = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };
                for ( const uint32 index : arrIndex )
                {
                    outListVertex.push_back( makeVertex( arrCorner[index], color ) );
                }
            }

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
    /** @brief 타일 하나를 워커에서 다시 베이크하는 일입니다. 입력은 공유 사본이라 그 사이 게임 스레드가 새 입력을 만들어도 이 일은 옛 것을 끝까지 읽는다. */
    struct NavTileBakeJob
    {
        const INavMesh*                           _pNavMesh{ nullptr };
        shared_ptr<const NavMeshGeometry>         _pGeometry;
        shared_ptr<const vector<NavConvexVolume>> _pVolume;
        NavTileData                               _result;
        TaskHandle                                _task;
        int32                                     _tileX{ 0 };
        int32                                     _tileZ{ 0 };
        atomic<bool>                              _bDone{ false };
        bool                                      _bSucceeded{ false };

        void run()
        {
            SW_MEMORY_SCOPE( Navigation );
            _bSucceeded = _pNavMesh->bakeTile( *_pGeometry, _pVolume.get(), _tileX, _tileZ, _result );
            _bDone.store( true, std::memory_order_release );
        }

        void waitDone() const
        {
            while ( _bDone.load( std::memory_order_acquire ) == false )
            {
                std::this_thread::yield();
            }
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 에이전트 종류 하나의 내비메시 · 군중 · 입력 · 재베이크 줄입니다. */
    struct NavMeshRuntime
    {
        hashed_string                             _agentType{};
        const NavMeshSurfaceComponent*            _pSurface{ nullptr };
        unique_ptr<INavMesh>                      _pNavMesh;
        unique_ptr<INavCrowd>                     _pCrowd;
        shared_ptr<const NavMeshGeometry>         _pGeometry;
        shared_ptr<const vector<NavConvexVolume>> _pVolume;
        vector<int2>                              _listDirtyTile;
        vector<unique_ptr<NavTileBakeJob>>        _listJob;
        NavQueryFilter                            _filter{};
        NavMeshBakeStats                          _bakeStats{};
        AABB                                      _bounds{};
        float3                                    _searchExtent{ 1.0f, 2.0f, 1.0f };
        bool                                      _bFromCooked{ false };

        NavMeshRuntime()                                   = default;
        NavMeshRuntime( const NavMeshRuntime& )            = delete;
        NavMeshRuntime& operator=( const NavMeshRuntime& ) = delete;
        ~NavMeshRuntime()
        {
            // 일이 이 내비메시 · 입력을 읽고 있다 — 끝날 때까지 놓지 않는다.
            for ( const unique_ptr<NavTileBakeJob>& pJob : _listJob )
            {
                pJob->waitDone();
            }
        }

        bool isTileInFlight( const int2& tile ) const
        {
            for ( const unique_ptr<NavTileBakeJob>& pJob : _listJob )
            {
                if ( pJob->_tileX == tile._x && pJob->_tileZ == tile._y )
                    return true;
            }
            return false;
        }

        void addDirtyTile( const int2& tile )
        {
            for ( const int2& existing : _listDirtyTile )
            {
                if ( existing._x == tile._x && existing._y == tile._y )
                    return;
            }
            _listDirtyTile.push_back( tile );
        }
    };
} // namespace sw

namespace sw
{
    SceneNavigation::SceneNavigation()
        : _settings{}
        , _sourcePath{}
        , _pCookedAsset{}
        , _listRuntime{}
        , _listAgent{}
        , _listSurface{}
        , _listObstacle{}
        , _listGeometrySource{}
        , _listDirtyArea{}
        , _listFailedAgentType{}
        , _pManager{ nullptr }
        , _debugView{}
        , _rebakedTileCount{ 0 }
        , _dirtyLock{}
        , _bSettingsLoaded{ false }
        , _bGeometryDirty{ false }
        , _bObstacleVolumeDirty{ false }
    {
    }

    SceneNavigation::~SceneNavigation()
    {
        shutdown();
    }

    void SceneNavigation::shutdown()
    {
        waitForAllBakes();
        for ( NavMeshAgentComponent* pAgent : _listAgent )
        {
            pAgent->_crowdAgentId = NavigationConstant::kInvalidAgentId;
            pAgent->_crowdIndex   = kNotRegistered;
            pAgent->_bOnNavMesh   = SW_FALSE;
        }
        for ( NavMeshObstacleComponent* pObstacle : _listObstacle )
        {
            pObstacle->_bCarved = SW_FALSE;
        }
        _listRuntime.clear();
        _dirtyLock.lock();
        _listDirtyArea.clear();
        _bGeometryDirty = false;
        _dirtyLock.unlock();
        _bObstacleVolumeDirty = false;
    }

    void SceneNavigation::setSettings( const NavMeshSettings& settings )
    {
        shutdown();
        _settings        = settings;
        _bSettingsLoaded = true;
        _listFailedAgentType.clear();
    }

    const NavMeshSettings& SceneNavigation::getSettings()
    {
        if ( _bSettingsLoaded == false )
        {
            _bSettingsLoaded = true;
            if ( ResourceUtil::hasResource( NavMeshSettings::kResourcePath ) == false || _settings.loadFromResource( NavMeshSettings::kResourcePath ) == false )
            {
                _settings = NavMeshSettings{};
                _settings.ensureDefaults();
            }
        }
        return _settings;
    }

    void SceneNavigation::registerAgent( NavMeshAgentComponent* pAgent )
    {
        if ( pAgent == nullptr || pAgent->_navIndex != kNotRegistered )
            return;
        pAgent->_navIndex = static_cast<uint32>( _listAgent.size() );
        _listAgent.push_back( pAgent );
    }

    void SceneNavigation::unregisterAgent( NavMeshAgentComponent* pAgent )
    {
        if ( pAgent == nullptr || pAgent->_navIndex >= _listAgent.size() || _listAgent[pAgent->_navIndex] != pAgent )
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
        if ( pAgent == nullptr || pAgent->_crowdAgentId == NavigationConstant::kInvalidAgentId )
            return;
        if ( pAgent->_crowdIndex < _listRuntime.size() && _listRuntime[pAgent->_crowdIndex]->_pCrowd != nullptr )
            _listRuntime[pAgent->_crowdIndex]->_pCrowd->removeAgent( pAgent->_crowdAgentId );
        pAgent->_crowdAgentId = NavigationConstant::kInvalidAgentId;
        pAgent->_crowdIndex   = kNotRegistered;
        pAgent->_bOnNavMesh   = SW_FALSE;
        pAgent->_velocity     = float3{};
    }

    void SceneNavigation::registerSurface( NavMeshSurfaceComponent* pSurface )
    {
        if ( pSurface != nullptr )
            _listSurface.push_back( pSurface );
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

    void SceneNavigation::registerObstacle( NavMeshObstacleComponent* pObstacle )
    {
        if ( pObstacle == nullptr || pObstacle->_navIndex != kNotRegistered )
            return;
        pObstacle->_navIndex = static_cast<uint32>( _listObstacle.size() );
        _listObstacle.push_back( pObstacle );
    }

    void SceneNavigation::unregisterObstacle( NavMeshObstacleComponent* pObstacle )
    {
        if ( pObstacle == nullptr || pObstacle->_navIndex >= _listObstacle.size() || _listObstacle[pObstacle->_navIndex] != pObstacle )
            return;
        if ( pObstacle->_bCarved == SW_TRUE )
        {
            invalidateArea( pObstacle->_carvedBounds, false );
            _bObstacleVolumeDirty = true;
        }
        NavMeshObstacleComponent* pMoved    = _listObstacle.back();
        _listObstacle[pObstacle->_navIndex] = pMoved;
        pMoved->_navIndex                   = pObstacle->_navIndex;
        _listObstacle.pop_back();
        pObstacle->_navIndex = kNotRegistered;
        pObstacle->_bCarved  = SW_FALSE;
    }

    void SceneNavigation::registerGeometrySource( INavGeometrySource* pSource )
    {
        if ( pSource != nullptr )
            _listGeometrySource.push_back( pSource );
    }

    void SceneNavigation::unregisterGeometrySource( INavGeometrySource* pSource )
    {
        SceneNavigationInternal::removeFromList( _listGeometrySource, pSource );
    }

    void SceneNavigation::invalidateArea( const AABB& bounds, bool bGeometryChanged )
    {
        if ( bounds.isValid() == false )
            return;
        _dirtyLock.lock();
        _listDirtyArea.push_back( bounds );
        _bGeometryDirty = _bGeometryDirty || bGeometryChanged;
        _dirtyLock.unlock();
    }

    const NavAgentTypeDef* SceneNavigation::resolveAgentType( const hashed_string& agentType )
    {
        const NavAgentTypeDef* pType = getSettings().findAgentType( agentType );
        if ( pType != nullptr )
            return pType;
        for ( const hashed_string& failed : _listFailedAgentType )
        {
            if ( failed == agentType )
                return nullptr;
        }
        _listFailedAgentType.push_back( agentType );
        SW_LOG_ERROR( "Navigation agent type '%#' is not in the navigation settings", agentType.c_str() );
        return nullptr;
    }

    NavMeshRuntime* SceneNavigation::findRuntime( const hashed_string& agentType ) const
    {
        // 빈 이름은 기본 종류 — 표의 `_defaultAgentType`, 그것도 비면 첫 종류(`NavMeshSettings::findAgentType` 과 같은 규칙).
        const NavAgentTypeDef* pType  = _settings.findAgentType( agentType );
        const hashed_string&   wanted = pType != nullptr ? pType->_name : agentType;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            if ( pRuntime->_agentType == wanted )
                return pRuntime.get();
        }
        return nullptr;
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

    void SceneNavigation::collectObstacleVolumes( vector<NavConvexVolume>& outListVolume ) const
    {
        outListVolume.clear();
        for ( const NavMeshObstacleComponent* pObstacle : _listObstacle )
        {
            NavConvexVolume volume;
            if ( pObstacle->_bCarved == SW_TRUE && pObstacle->makeVolume( volume ) )
                outListVolume.push_back( std::move( volume ) );
        }
    }

    bool SceneNavigation::installCooked( NavMeshRuntime& runtime, const NavAgentTypeDef& agentType, uint64 inputHash )
    {
        const string                   cookedPath = _pCookedAsset != nullptr ? string{ "(given)" } : NavMeshAsset::makeCookedPath( _sourcePath );
        shared_ptr<const NavMeshAsset> pAsset     = _pCookedAsset;
        if ( pAsset == nullptr )
        {
            if ( cookedPath.empty() || ResourceUtil::hasResource( cookedPath ) == false )
                return false;
            shared_ptr<NavMeshAsset> pLoaded = make_shared<NavMeshAsset>();
            if ( pLoaded->loadFromResource( cookedPath ) == false )
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
        runtime._bakeStats._milliseconds    = static_cast<float64>( stopwatch.getElapsedMicroseconds() ) / 1000.0;
        runtime._bakeStats._tileCount       = static_cast<uint32>( runtime._pNavMesh->getTileGrid().getTileCount() );
        runtime._bakeStats._filledTileCount = runtime._pNavMesh->getLoadedTileCount();
        runtime._bakeStats._polygonCount    = runtime._pNavMesh->getPolygonCount();
        return true;
    }

    NavMeshRuntime* SceneNavigation::createRuntime( const hashed_string& agentType )
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

        shared_ptr<NavMeshGeometry> pGeometry = make_shared<NavMeshGeometry>();
        collectGeometry( *_pManager, pRuntime->_pSurface, _settings, _listGeometrySource, *pGeometry );
        shared_ptr<vector<NavConvexVolume>> pVolume = make_shared<vector<NavConvexVolume>>();
        collectObstacleVolumes( *pVolume );
        pRuntime->_pNavMesh = NavMeshBackend::createNavMesh();

        const uint64 inputHash = pGeometry->computeHash();
        if ( installCooked( *pRuntime, *pType, inputHash ) )
        {
            // 쿠킹본에는 장애물이 없다 — 장애물 자리의 타일만 다시 베이크한다.
            pGeometry->buildSpatialIndex( MathUtil::max( 1.0f, pRuntime->_pNavMesh->getTileGrid()._tileWorldSize * 0.25f ) );
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
            const AABB bounds = computeBakeBounds( pRuntime->_pSurface, *pGeometry );
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
        pRuntime->_pGeometry = std::move( pGeometry );
        pRuntime->_pVolume   = std::move( pVolume );
        pRuntime->_pCrowd    = pRuntime->_pNavMesh->createCrowd( pType->_maxCrowdAgentCount, MathUtil::max( 1.0f, pType->_radius * 2.0f ) );
        if ( pRuntime->_pCrowd != nullptr )
            pRuntime->_pCrowd->setQueryFilter( pRuntime->_filter );
        _listRuntime.push_back( std::move( pRuntime ) );
        return _listRuntime.back().get();
    }

    INavMesh* SceneNavigation::ensureNavMesh( const hashed_string& agentType )
    {
        NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pRuntime == nullptr )
            pRuntime = createRuntime( agentType );
        return pRuntime != nullptr ? pRuntime->_pNavMesh.get() : nullptr;
    }

    bool SceneNavigation::makeCookedEntry( const hashed_string& agentType, NavMeshAssetEntry& outEntry )
    {
        outEntry                 = NavMeshAssetEntry{};
        const INavMesh* pNavMesh = ensureNavMesh( agentType );
        NavMeshRuntime* pRuntime = findRuntime( agentType );
        if ( pNavMesh == nullptr || pRuntime == nullptr || pRuntime->_pGeometry == nullptr )
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
        _dirtyLock.lock();
        count += static_cast<uint32>( _listDirtyArea.size() );
        _dirtyLock.unlock();
        return count;
    }

    void SceneNavigation::tick( float32 deltaTime )
    {
        const bool bAnything = _listAgent.empty() == false || _listObstacle.empty() == false || _listSurface.empty() == false || _listRuntime.empty() == false;
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
                if ( pSurface->getAgentTypes().empty() )
                {
                    (void)ensureNavMesh( hashed_string{} );
                    continue;
                }
                for ( const hashed_string& agentType : pSurface->getAgentTypes() )
                {
                    (void)ensureNavMesh( agentType );
                }
            }
        }
        updateObstacles();
        processDirtyAreas();
        applyFinishedBakes();
        launchTileBakes();
        updateAgents( deltaTime );
    }

    void SceneNavigation::updateObstacles()
    {
        using Internal              = SceneNavigationInternal;
        const float32 moveThreshold = getSettings()._obstacleMoveThreshold;
        for ( NavMeshObstacleComponent* pObstacle : _listObstacle )
        {
            if ( Internal::isLive( *pObstacle ) == false )
            {
                if ( pObstacle->_bCarved == SW_TRUE )
                {
                    invalidateArea( pObstacle->_carvedBounds, false );
                    pObstacle->_bCarved   = SW_FALSE;
                    _bObstacleVolumeDirty = true;
                }
                continue;
            }
            NavConvexVolume volume;
            if ( pObstacle->makeVolume( volume ) == false || volume._listPoint.size() < 2 )
                continue;
            const AABB    bounds   = volume.computeBounds();
            const float3  center   = ( bounds._min + bounds._max ) * 0.5f;
            const float3& first    = volume._listPoint[0];
            const float3& second   = volume._listPoint[1];
            const float32 yaw      = MathUtil::atan2( second._x - first._x, second._z - first._z );
            const bool    bMoved   = ( center - pObstacle->_carvedPosition ).getLength() > moveThreshold;
            const bool    bTurned  = MathUtil::abs( MathUtil::wrapAngle( yaw - pObstacle->_carvedYaw ) ) > Internal::kObstacleTurnThreshold;
            const bool    bChanged = pObstacle->_bCarved == SW_FALSE || pObstacle->_bShapeDirty == SW_TRUE || bMoved || bTurned;
            if ( bChanged == false )
                continue;
            if ( pObstacle->_bCarved == SW_TRUE )
                invalidateArea( pObstacle->_carvedBounds, false );
            invalidateArea( bounds, false );
            pObstacle->_carvedBounds   = bounds;
            pObstacle->_carvedPosition = center;
            pObstacle->_carvedYaw      = yaw;
            pObstacle->_bCarved        = SW_TRUE;
            pObstacle->_bShapeDirty    = SW_FALSE;
            _bObstacleVolumeDirty      = true;
        }
    }

    void SceneNavigation::processDirtyAreas()
    {
        vector<AABB> listArea;
        _dirtyLock.lock();
        listArea.swap( _listDirtyArea );
        const bool bGeometryDirty = _bGeometryDirty;
        _bGeometryDirty           = false;
        _dirtyLock.unlock();
        if ( _listRuntime.empty() )
        {
            _bObstacleVolumeDirty = false;
            return;
        }
        if ( listArea.empty() && _bObstacleVolumeDirty == false )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.invalidate" );
        if ( _bObstacleVolumeDirty )
        {
            shared_ptr<vector<NavConvexVolume>> pVolume = make_shared<vector<NavConvexVolume>>();
            collectObstacleVolumes( *pVolume );
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
            {
                pRuntime->_pVolume = pVolume;
            }
            _bObstacleVolumeDirty = false;
        }
        if ( bGeometryDirty )
        {
            // 정적 기하가 바뀌었다(파괴 · 수정자) — 새 사본을 모은다. 베이크하는 중인 일은 옛 사본을 끝까지 읽는다.
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
            {
                shared_ptr<NavMeshGeometry> pGeometry = make_shared<NavMeshGeometry>();
                collectGeometry( *_pManager, pRuntime->_pSurface, _settings, _listGeometrySource, *pGeometry );
                pGeometry->buildSpatialIndex( MathUtil::max( 1.0f, pRuntime->_pNavMesh->getTileGrid()._tileWorldSize * 0.25f ) );
                pRuntime->_pGeometry = std::move( pGeometry );
            }
        }
        vector<int2> listTile;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            for ( const AABB& area : listArea )
            {
                pRuntime->_pNavMesh->collectTilesOverlapping( area, listTile );
                for ( const int2& tile : listTile )
                {
                    pRuntime->addDirtyTile( tile );
                }
            }
        }
    }

    void SceneNavigation::applyFinishedBakes()
    {
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            vector<unique_ptr<NavTileBakeJob>>& listJob = pRuntime->_listJob;
            for ( size_t jobIndex = 0; jobIndex < listJob.size(); )
            {
                NavTileBakeJob& job = *listJob[jobIndex];
                if ( job._bDone.load( std::memory_order_acquire ) == false )
                {
                    ++jobIndex;
                    continue;
                }
                if ( job._bSucceeded && pRuntime->_pNavMesh->replaceTile( job._result ) )
                    ++_rebakedTileCount;
                else
                    SW_LOG_WARNING( "Navmesh '%#' tile (%#, %#) failed to rebake", pRuntime->_agentType.c_str(), job._tileX, job._tileZ );
                listJob[jobIndex] = std::move( listJob.back() );
                listJob.pop_back();
            }
        }
    }

    void SceneNavigation::launchTileBakes()
    {
        const uint32 maxConcurrent = MathUtil::max( 1u, _settings._maxConcurrentTileBakeCount );
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            vector<int2>& listDirty = pRuntime->_listDirtyTile;
            for ( size_t dirtyIndex = 0; dirtyIndex < listDirty.size() && pRuntime->_listJob.size() < maxConcurrent; )
            {
                const int2 tile = listDirty[dirtyIndex];
                if ( pRuntime->isTileInFlight( tile ) )
                {
                    ++dirtyIndex;
                    continue;
                }
                listDirty.erase( listDirty.begin() + static_cast<ptrdiff_t>( dirtyIndex ) );
                unique_ptr<NavTileBakeJob> pJob = make_unique<NavTileBakeJob>();
                pJob->_pNavMesh                 = pRuntime->_pNavMesh.get();
                pJob->_pGeometry                = pRuntime->_pGeometry;
                pJob->_pVolume                  = pRuntime->_pVolume;
                pJob->_tileX                    = tile._x;
                pJob->_tileZ                    = tile._y;
                NavTileBakeJob* pRaw            = pJob.get();
                pRuntime->_listJob.push_back( std::move( pJob ) );
                // 워커가 없으면(시험 · 도구) 지금 베이크한다 — 끼우기는 그래도 다음 갱신이다.
                if ( engine::areEngineServicesBound() )
                {
                    pRaw->_task = engine::getTaskManager().emplaceTask( "NavTileBake", SW_DELEGATE_METHOD( TaskDelegate, &NavTileBakeJob::run, pRaw ) );
                    pRaw->_task.submit();
                }
                else
                {
                    pRaw->run();
                }
            }
        }
    }

    void SceneNavigation::waitForAllBakes()
    {
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            for ( const unique_ptr<NavTileBakeJob>& pJob : pRuntime->_listJob )
            {
                pJob->waitDone();
            }
        }
    }

    void SceneNavigation::flushTileBakes()
    {
        updateObstacles();
        processDirtyAreas();
        while ( getPendingTileCount() > 0 )
        {
            launchTileBakes();
            waitForAllBakes();
            applyFinishedBakes();
            processDirtyAreas();
        }
    }

    void SceneNavigation::updateAgents( float32 deltaTime )
    {
        using Internal = SceneNavigationInternal;
        if ( _listAgent.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.agents" );
        for ( size_t agentIndex = 0; agentIndex < _listAgent.size(); ++agentIndex )
        {
            NavMeshAgentComponent& agent = *_listAgent[agentIndex];
            if ( Internal::isLive( agent ) == false )
            {
                releaseAgent( &agent );
                continue;
            }
            NavMeshRuntime* pRuntime = agent._crowdIndex < _listRuntime.size() ? _listRuntime[agent._crowdIndex].get() : nullptr;
            if ( pRuntime == nullptr )
            {
                pRuntime = findRuntime( agent._agentType );
                if ( pRuntime == nullptr )
                    pRuntime = createRuntime( agent._agentType );
            }
            if ( pRuntime == nullptr || pRuntime->_pCrowd == nullptr )
            {
                if ( agent._bHasDestination == SW_TRUE )
                    agent._moveStatus = NavMoveStatus::Failed;
                continue;
            }
            syncAgentIntoCrowd( agent, *pRuntime );
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
            if ( pAgent->_crowdAgentId != NavigationConstant::kInvalidAgentId && pAgent->_crowdIndex < _listRuntime.size() )
                writeAgentResult( *pAgent, *_listRuntime[pAgent->_crowdIndex], deltaTime );
        }
    }

    void SceneNavigation::syncAgentIntoCrowd( NavMeshAgentComponent& agent, NavMeshRuntime& runtime )
    {
        using Internal                                = SceneNavigationInternal;
        GameObject*                   pOwner          = agent.getOwner();
        CharacterControllerComponent* pController     = Internal::findDrivenController( agent );
        SceneComponent*               pScene          = pOwner->getPrimarySceneComponent();
        const float3                  currentPosition = pController != nullptr ? pController->getWorldPosition() : ( pScene != nullptr ? pScene->getWorldPosition() : agent._agentPosition );
        INavCrowd&                    crowd           = *runtime._pCrowd;
        if ( agent._crowdAgentId == NavigationConstant::kInvalidAgentId )
        {
            if ( agent._driveMode == NavAgentDriveMode::CharacterController && pController == nullptr )
                SW_LOG_WARNING( "NavMesh agent '%#' drives a character controller but the object has none - it does not move", pOwner->getName().c_str() );
            NavCrowdAgentParams    params = agent.makeCrowdParams();
            const NavAgentTypeDef& type   = runtime._pNavMesh->getAgentType();
            if ( params._radius <= 0.0f )
            {
                params._radius                = type._radius;
                params._collisionQueryRange   = type._radius * 12.0f;
                params._pathOptimizationRange = type._radius * 30.0f;
            }
            params._height      = type._height;
            agent._crowdAgentId = crowd.addAgent( currentPosition, params );
            if ( agent._crowdAgentId == NavigationConstant::kInvalidAgentId )
            {
                SW_LOG_WARNING( "Navmesh '%#' crowd is full (%# agents) - '%#' does not move", runtime._agentType.c_str(), crowd.getMaxAgentCount(),
                                pOwner->getName().c_str() );
                agent._moveStatus = NavMoveStatus::Failed;
                return;
            }
            for ( size_t runtimeIndex = 0; runtimeIndex < _listRuntime.size(); ++runtimeIndex )
            {
                if ( _listRuntime[runtimeIndex].get() == &runtime )
                    agent._crowdIndex = static_cast<uint32>( runtimeIndex );
            }
            agent._writtenPosition   = currentPosition;
            agent._bHasWritten       = SW_TRUE;
            agent._bDestinationDirty = agent._bHasDestination;
            agent._bParamsDirty      = SW_FALSE;
        }
        else if ( agent._bWarpPending == SW_TRUE )
        {
            crowd.teleportAgent( agent._crowdAgentId, agent._pendingWarp );
            if ( pController == nullptr && pScene != nullptr )
                pScene->setWorldPosition( agent._pendingWarp );
            else if ( pController != nullptr )
                pController->setWorldPosition( agent._pendingWarp );
            agent._writtenPosition   = agent._pendingWarp;
            agent._bDestinationDirty = agent._bHasDestination;
        }
        else if ( agent._driveMode != NavAgentDriveMode::Transform )
        {
            // 컨트롤러 · 폰 이동이 옮긴 자리를 받아들인다(SteerOnly 는 군중이 낸 속도를 조종자가 의도로 바꿔 폰이 걸었다).
            crowd.syncAgentPosition( agent._crowdAgentId, currentPosition );
        }
        else if ( agent._bHasWritten == SW_TRUE && Internal::computeDistance2D( currentPosition, agent._writtenPosition ) > Internal::kTeleportDistance )
        {
            crowd.teleportAgent( agent._crowdAgentId, currentPosition );
            agent._bDestinationDirty = agent._bHasDestination;
        }
        agent._bWarpPending = SW_FALSE;
        if ( agent._bParamsDirty == SW_TRUE )
        {
            NavCrowdAgentParams params = agent.makeCrowdParams();
            if ( params._radius <= 0.0f )
                params._radius = runtime._pNavMesh->getAgentType()._radius;
            params._height = runtime._pNavMesh->getAgentType()._height;
            crowd.updateAgentParams( agent._crowdAgentId, params );
            agent._bParamsDirty = SW_FALSE;
        }
        if ( agent._bDestinationDirty == SW_TRUE )
        {
            agent._bDestinationDirty = SW_FALSE;
            if ( agent._bHasDestination == SW_TRUE )
                agent._moveStatus = crowd.requestMoveTarget( agent._crowdAgentId, agent._destination ) ? NavMoveStatus::Moving : NavMoveStatus::Failed;
            else
                crowd.resetMoveTarget( agent._crowdAgentId );
        }
    }

    void SceneNavigation::writeAgentResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime )
    {
        using Internal = SceneNavigationInternal;
        NavCrowdAgentState state;
        if ( runtime._pCrowd->findAgentState( agent._crowdAgentId, state ) == false )
            return;
        agent._agentPosition   = state._position;
        agent._velocity        = state._velocity;
        agent._desiredVelocity = state._desiredVelocity;
        agent._nextCorner      = state._nextCorner;
        agent._bOnNavMesh      = state._bOnNavMesh ? SW_TRUE : SW_FALSE;
        if ( agent._bHasDestination == SW_TRUE && agent._moveStatus != NavMoveStatus::Arrived )
        {
            const bool bWithinStop = Internal::computeDistance2D( state._position, agent._destination ) <= agent._stoppingDistance;
            switch ( state._moveState )
            {
                case NavCrowdMoveState::Failed:
                {
                    agent._moveStatus = NavMoveStatus::Failed;
                    break;
                }
                case NavCrowdMoveState::Arrived:
                {
                    agent._moveStatus = NavMoveStatus::Arrived;
                    break;
                }
                case NavCrowdMoveState::Idle:
                case NavCrowdMoveState::Pending:
                case NavCrowdMoveState::Moving:
                {
                    agent._moveStatus = bWithinStop ? NavMoveStatus::Arrived : NavMoveStatus::Moving;
                    break;
                }
            }
            if ( agent._moveStatus == NavMoveStatus::Arrived )
                runtime._pCrowd->resetMoveTarget( agent._crowdAgentId );
        }

        GameObject*                   pOwner      = agent.getOwner();
        CharacterControllerComponent* pController = Internal::findDrivenController( agent );
        SceneComponent*               pScene      = pOwner->getPrimarySceneComponent();
        if ( pController != nullptr )
        {
            pController->setMoveVelocity( float3{ state._velocity._x, 0.0f, state._velocity._z } );
        }
        else if ( agent._driveMode == NavAgentDriveMode::Transform && pScene != nullptr )
        {
            pScene->setWorldPosition( state._position );
            agent._writtenPosition = state._position;
            agent._bHasWritten     = SW_TRUE;
        }
        const float32 speed = MathUtil::sqrt( state._velocity._x * state._velocity._x + state._velocity._z * state._velocity._z );
        if ( agent._bUpdateRotation && pScene != nullptr && speed > Internal::kTurnSpeed )
        {
            float3        rotation = pScene->getLocalRotation();
            const float32 wantYaw  = MathUtil::atan2( state._velocity._x, state._velocity._z );
            const float32 delta    = MathUtil::wrapAngle( wantYaw - rotation._y );
            const float32 maxTurn  = agent._turnRate * deltaTime;
            rotation._y            = MathUtil::wrapAngle( rotation._y + MathUtil::clamp( delta, -maxTurn, maxTurn ) );
            pScene->setLocalRotation( rotation );
        }
    }

    void SceneNavigation::drawDebug( IPhysicsDebugRenderer& renderer, uint32 flags ) const
    {
        const float4 meshColor{ 0.1f, 0.85f, 0.9f, 1.0f };
        const float4 obstacleColor{ 1.0f, 0.25f, 0.2f, 1.0f };
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            if ( ( flags & NavDebugDrawFlag::kMesh ) != 0 )
                pRuntime->_pNavMesh->drawDebug( renderer, meshColor );
            if ( pRuntime->_pCrowd != nullptr && ( flags & ( NavDebugDrawFlag::kPath | NavDebugDrawFlag::kVelocity ) ) != 0 )
                pRuntime->_pCrowd->drawDebug( renderer, ( flags & NavDebugDrawFlag::kPath ) != 0, ( flags & NavDebugDrawFlag::kVelocity ) != 0 );
        }
        if ( ( flags & NavDebugDrawFlag::kObstacle ) == 0 )
            return;
        NavConvexVolume volume;
        for ( const NavMeshObstacleComponent* pObstacle : _listObstacle )
        {
            if ( pObstacle->_bCarved == SW_FALSE || pObstacle->makeVolume( volume ) == false )
                continue;
            for ( size_t pointIndex = 0; pointIndex < volume._listPoint.size(); ++pointIndex )
            {
                const float3& from = volume._listPoint[pointIndex];
                const float3& to   = volume._listPoint[( pointIndex + 1 ) % volume._listPoint.size()];
                renderer.drawLine( float3{ from._x, volume._minY, from._z }, float3{ to._x, volume._minY, to._z }, obstacleColor );
                renderer.drawLine( float3{ from._x, volume._maxY, from._z }, float3{ to._x, volume._maxY, to._z }, obstacleColor );
            }
        }
    }

    void SceneNavigation::updateDebugView( uint32 flags )
    {
        using Internal = SceneNavigationInternal;
        if ( _pManager == nullptr )
            return;
        GameObject* pView   = _pManager->resolveGameObject( _debugView );
        const bool  bWanted = ( flags & NavDebugDrawFlag::kSolidView ) != 0 && _listRuntime.empty() == false;
        if ( bWanted == false )
        {
            if ( pView != nullptr )
                _pManager->destroyObject( pView );
            _debugView = GameObjectHandle{};
            return;
        }
        if ( pView == nullptr )
        {
            pView = _pManager->createGameObject( hashed_string( "NavMeshDebugView" ) );
            pView->addComponent<NavMeshModifierComponent>()->setIgnoreFromBuild( true );
            (void)pView->addComponent<MeshComponent>();
            _debugView = pView->getHandle();
        }
        MeshComponent* pMeshComponent = pView->getComponent<MeshComponent>();
        if ( pMeshComponent == nullptr )
            return;
        vector<RHIVertex> listVertex;
        vector<float3>    listCorner;
        vector<uint8>     listArea;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : _listRuntime )
        {
            pRuntime->_pNavMesh->collectDebugTriangles( listCorner, listArea );
            for ( size_t triangleIndex = 0; triangleIndex < listArea.size(); ++triangleIndex )
            {
                const float4 color = Internal::makeAreaColor( listArea[triangleIndex], triangleIndex );
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    listVertex.push_back( Internal::makeVertex( listCorner[triangleIndex * 3 + corner] + float3{ 0.0f, Internal::kViewLift, 0.0f }, color ) );
                }
            }
        }
        // 에이전트 경로 띠 — 자리 → 다음 모퉁이 → 목적지.
        const float4 pathColor{ 1.0f, 0.55f, 0.05f, 1.0f };
        for ( const NavMeshAgentComponent* pAgent : _listAgent )
        {
            if ( pAgent->_crowdAgentId == NavigationConstant::kInvalidAgentId || pAgent->_bHasDestination == SW_FALSE ||
                 pAgent->_moveStatus != NavMoveStatus::Moving )
                continue;
            Internal::appendRibbon( pAgent->_agentPosition, pAgent->_nextCorner, pathColor, listVertex );
            Internal::appendRibbon( pAgent->_nextCorner, pAgent->_destination, pathColor, listVertex );
        }
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        pMeshComponent->setMesh( std::move( mesh ) );
    }

    AABB SceneNavigation::computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry )
    {
        AABB bounds = NavMeshBakeUtil::computeBakeBounds( geometry );
        AABB surfaceBox{};
        if ( SceneNavigationInternal::findSurfaceBox( pSurface, surfaceBox ) )
        {
            const AABB clipped{
                float3{MathUtil::max( bounds._min._x, surfaceBox._min._x ), MathUtil::max( bounds._min._y, surfaceBox._min._y ),
                       MathUtil::max( bounds._min._z, surfaceBox._min._z )},
                float3{MathUtil::min( bounds._max._x, surfaceBox._max._x ), MathUtil::min( bounds._max._y, surfaceBox._max._y ),
                       MathUtil::min( bounds._max._z, surfaceBox._max._z )}
            };
            bounds = clipped.isValid() ? clipped : surfaceBox;
        }
        return bounds;
    }

    void SceneNavigation::collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                           const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry )
    {
        using Internal = SceneNavigationInternal;
        SW_PROFILE_SCOPE( "GT.Navigation.collectGeometry" );
        outGeometry.clear();
        const NavGeometrySource source     = pSurface != nullptr ? pSurface->getGeometrySource() : NavGeometrySource::Both;
        const bool              bMeshes    = source == NavGeometrySource::RenderMeshes || source == NavGeometrySource::Both;
        const bool              bColliders = source == NavGeometrySource::PhysicsColliders || source == NavGeometrySource::Both;
        AABB                    surfaceBox{};
        const bool              bSurfaceBox  = Internal::findSurfaceBox( pSurface, surfaceBox );
        const TypeInfo*         pMeshType    = findStaticType<MeshComponent>();
        const TypeInfo*         pBodyType    = findStaticType<RigidBodyComponent>();
        const TypeInfo*         pSkinnedType = findStaticType<SkeletalMeshComponent>();
        manager.forEachGameObject( [&]( GameObject* pObject )
        {
            if ( pObject->isActiveInHierarchy() == false )
                return;
            AABB objectBox{};
            if ( bSurfaceBox && ( pObject->getWorldBox( objectBox ) == false || objectBox.intersects( surfaceBox ) == false ) )
                return;
            const Internal::ObjectVerdict verdict = Internal::evaluateObject( *pObject, pSurface, settings );
            if ( verdict._bIncluded == false )
                return;
            // 기하를 대신 내는 소스(파괴 오브젝트)가 이 오브젝트를 맡으면 그것만 쓴다.
            for ( const INavGeometrySource* pSource : listSource )
            {
                if ( pSource->getNavGeometryOwner() == pObject && pSource->overridesOwnerGeometry() )
                {
                    pSource->collectNavGeometry( outGeometry, verdict._area );
                    return;
                }
            }
            if ( bMeshes )
            {
                pObject->forEachComponentOfType<MeshComponent>( pMeshType, [&]( MeshComponent* pMeshComponent )
                {
                    if ( pMeshComponent->isActive() == false || pMeshComponent->isVisible() == false ||
                         castTo<SkeletalMeshComponent>( pMeshComponent, pSkinnedType ) != nullptr )
                        return;
                    shared_ptr<Mesh> mesh = pMeshComponent->getMesh();
                    if ( mesh == nullptr && pMeshComponent->getMeshId().empty() == false )
                    {
                        const string& meshId = pMeshComponent->getMeshId();
                        mesh                 = StringUtil::endsWith( meshId, MeshAssetFormat::kExtension, true ) ? MeshCache::acquire( meshId ) : MeshUtil::acquirePrimitive( meshId );
                    }
                    if ( mesh == nullptr || mesh->getVertexCount() < 3 )
                        return;
                    const vector<RHIVertex>& listVertex = mesh->getVertices();
                    outGeometry.addTriangleList( &listVertex.front()._arrPosition[0], static_cast<uint32>( listVertex.size() ), sizeof( RHIVertex ),
                                                 pMeshComponent->getWorldMatrix(), verdict._area );
                } );
            }
            if ( bColliders )
            {
                pObject->forEachComponentOfType<RigidBodyComponent>( pBodyType, [&]( RigidBodyComponent* pBody )
                {
                    if ( pBody->isActive() == false || pBody->getBodyType() != PhysicsBodyType::Static )
                        return;
                    float3     position{};
                    quaternion rotation{};
                    float3     scale{};
                    PhysicsComponentUtil::readWorldPose( *pBody, position, rotation, scale );
                    const float4x4 world = float4x4::createTrs( position, rotation, float3{ 1.0f, 1.0f, 1.0f } );
                    for ( const PhysicsShapeDesc3D& shape : pBody->getShapes() )
                    {
                        outGeometry.addPhysicsShape( PhysicsComponentUtil::makeScaledShape( shape, scale ), world, verdict._area );
                    }
                } );
            }
        } );
    }
} // namespace sw
