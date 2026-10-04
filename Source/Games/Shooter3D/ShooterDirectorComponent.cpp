#include "pch.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"

#include "Games/Shooter3D/ShooterDroneComponent.h"
#include "Games/Shooter3D/ShooterEffectComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterDirector" );

    namespace
    {
        struct ShooterDirectorComponentInternal
        {
            static constexpr float32 kStatusInterval = 5.0f;
            /** @brief 이 거리(m) 안의 드론이 "가까운 드론" 신호입니다. */
            static constexpr float32 kNearDistance = 7.0f;
            /** @brief 수리 보상의 scale 1 이 채우는 체력입니다. */
            static constexpr float32 kRepairPerScale = 10.0f;
            /** @brief 이 게임이 아는 조우 · 보상 · 스폰 id 입니다 — 프로필이 다른 이름을 쓰면 시작할 때 알린다. */
            static constexpr const utf8* kArrKnownEncounter[] = { "swarm", "elite", "ammo", "repair" };
            static constexpr const utf8* kArrKnownSpawn[]     = { "drone" };
            static constexpr float4      kBurstColor{ 1.0f, 0.6f, 0.15f, 1.0f };
            static constexpr const utf8* kSoundDroneDown = "game/shooter3d/sounds/impact_metal_medium_000.ogg";

            /** @brief 원(XZ) 가운데에서 가장 가까운 상자 위 점입니다. */
            static float3 closestPointXz( const float3& point, const float3& boxMin, const float3& boxMax )
            {
                return float3{ MathUtil::clamp( point._x, boxMin._x, boxMax._x ), point._y, MathUtil::clamp( point._z, boxMin._z, boxMax._z ) };
            }

            static bool isSameColor( const float4& lhs, const float4& rhs )
            {
                return lhs._x == rhs._x && lhs._y == rhs._y && lhs._z == rhs._z && lhs._w == rhs._w;
            }

            template <size_t Count>
            static bool isKnownId( const hashed_string& id, const utf8* const ( &arrKnown )[Count] )
            {
                for ( const utf8* pKnown : arrKnown )
                {
                    if ( id == hashed_string( pKnown ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    /** @brief `-gv_shooterAutoPlay=1` — 디렉터의 자동 플레이를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 조준 · 사격 · 이동을 AI 가 한다. */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_shooterAutoPlay, 0, "Shooter3D: 조준 · 사격도 AI 가 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    float3 ShooterArenaMath::resolveCircle( const vector<ShooterArenaBox>& listBox, const float3& position, float32 radius )
    {
        float3 resolved = position;
        // 두 번 — 모서리에서 두 상자에 동시에 닿아도 빠져나온다.
        for ( int32 iteration = 0; iteration < 2; ++iteration )
        {
            for ( const ShooterArenaBox& box : listBox )
            {
                if ( resolved._y >= box._max._y )
                    continue; // 상자 위 — 뛰어 오른 것은 막지 않는다(높이가 낮은 상자)
                const float3  closest = ShooterDirectorComponentInternal::closestPointXz( resolved, box._min, box._max );
                float3        offset  = float3{ resolved._x - closest._x, 0.0f, resolved._z - closest._z };
                const float32 length  = offset.getLength();
                if ( length >= radius )
                    continue;
                if ( length < 1.0e-5f )
                {
                    // 가운데가 상자 안 — 가장 얕은 면으로 민다.
                    const float32 arrPush[4] = { resolved._x - box._min._x, box._max._x - resolved._x, resolved._z - box._min._z, box._max._z - resolved._z };
                    int32         shallow    = 0;
                    for ( int32 sideIndex = 1; sideIndex < 4; ++sideIndex )
                        shallow = arrPush[sideIndex] < arrPush[shallow] ? sideIndex : shallow;
                    if ( shallow == 0 )
                        resolved._x = box._min._x - radius;
                    else if ( shallow == 1 )
                        resolved._x = box._max._x + radius;
                    else if ( shallow == 2 )
                        resolved._z = box._min._z - radius;
                    else
                        resolved._z = box._max._z + radius;
                    continue;
                }
                offset   = offset * ( radius / length );
                resolved = float3{ closest._x + offset._x, resolved._y, closest._z + offset._z };
            }
        }
        return resolved;
    }

    ShooterDirectorComponent::ShooterDirectorComponent()
        : _dronePrefab{}
        , _effectPrefab{}
        , _player{}
        , _listSpawnPoint{}
        , _arenaHalfSize{ 20.0f }
        , _droneHeight{ 1.4f }
        , _pacingProfile{}
        , _spawnTable{}
        , _pacingSeed{ 1 }
        , _effectPoolSize{ 32 }
        , _droneFlashTint{ 1.0f, 0.35f, 0.3f, 1.0f }
        , _bAutoPlay{ false }
        , _listBox{}
        , _listDroneView{}
        , _profile{}
        , _table{}
        , _director{}
        , _listDirectorEvent{}
        , _listDrone{}
        , _listEffect{}
        , _listPendingDrone{}
        , _listPendingEffect{}
        , _listPendingSound{}
        , _listColorLook{}
        , _droneLook{}
        , _droneFlashLook{}
        , _playerEye{ 0.0f, 1.6f, -16.0f }
        , _statusTimer{ 0.0f }
        , _pendingHeal{ 0.0f }
        , _spawnCursor{ 0 }
        , _killCount{ 0 }
        , _bStarted{ SW_FALSE }
        , _bPoolSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bAmmoPending{ SW_FALSE }
        , _bPacingReady{ SW_FALSE }
        , _bPacingRestart{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ShooterDirectorComponent::~ShooterDirectorComponent() = default;

    void ShooterDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 판의 규칙은 앞 그룹 — 플레이어 · 드론이 같은 프레임에 이 결과를 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        collectBoxes();
        startPacing();
        _bStarted  = SW_TRUE;
        _killCount = 0;
        updatePlayerView();
        scheduleFlush();
    }

    void ShooterDirectorComponent::onEndPlay()
    {
        despawnRuntime();
        _bStarted = SW_FALSE;
        Component::onEndPlay();
    }

    void ShooterDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bStarted == SW_FALSE )
            return;
        if ( _bPoolSpawned == SW_FALSE )
            scheduleFlush(); // 상태 저장 전에 걷었다 — 효과 풀을 다시 세운다
        const float32 step = MathUtil::min( deltaTime, 0.1f );
        updatePlayerView();
        if ( step > 0.0f )
        {
            updateDrones();
            if ( _bPacingReady == SW_TRUE && _bPacingRestart == SW_TRUE )
            {
                _bPacingRestart = SW_FALSE;
                _director.restart();
            }
            if ( _bPacingReady == SW_TRUE )
            {
                _director.update( step );
                applyDirectorEvents();
            }
            logStatus( step );
        }
        const bool bPending = _listPendingDrone.empty() == false || _listPendingEffect.empty() == false || _listPendingSound.empty() == false || _bAmmoPending == SW_TRUE ||
                              _pendingHeal > 0.0f;
        if ( bPending )
            scheduleFlush();
    }

    void ShooterDirectorComponent::despawnRuntime()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( const DroneRecord& drone : _listDrone )
            {
                GameObject* pObject = pManager->resolveGameObject( drone._object );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
            for ( const GameObjectHandle& handle : _listEffect )
            {
                GameObject* pObject = pManager->resolveGameObject( handle );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
        }
        _listDrone.clear();
        _listEffect.clear();
        _listDroneView.clear();
        _listPendingDrone.clear();
        _listPendingEffect.clear();
        _bPoolSpawned   = SW_FALSE;
        _bPacingRestart = SW_TRUE; // 걷은 드론의 스폰 id 를 돌려줄 수 없다 — 다음 틱에 감독도 처음부터
    }

    void ShooterDirectorComponent::restartRound()
    {
        clearDrones();
        _killCount   = 0;
        _pendingHeal = 0.0f;
        if ( _bPacingReady == SW_TRUE )
            _director.restart();
        _listDirectorEvent.clear();
    }

    void ShooterDirectorComponent::clearDrones()
    {
        GameObjectManager* pManager = getObjectManager();
        for ( const DroneRecord& drone : _listDrone )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( drone._object ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyObject( pObject );
        }
        _listDrone.clear();
        _listDroneView.clear();
        _listPendingDrone.clear();
    }

    void ShooterDirectorComponent::reportPlayerDamage( float32 amount )
    {
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "damageTaken" ), amount );
    }

    void ShooterDirectorComponent::spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime )
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        // 숨어 있는 것 하나를 꺼낸다 — 효과마다 오브젝트를 만들고 지우지 않는다. 다 쓰고 있으면 이번 효과는 건너뛴다.
        for ( const GameObjectHandle& handle : _listEffect )
        {
            GameObject*             pObject = pManager->resolveGameObject( handle );
            ShooterEffectComponent* pEffect = pObject != nullptr ? pObject->getComponent<ShooterEffectComponent>() : nullptr;
            MeshComponent*          pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            if ( pEffect == nullptr || pMesh == nullptr || pEffect->isIdle() == false )
                continue;
            pMesh->setLocalPosition( position );
            pMesh->setLocalScale( float3{ size } );
            const shared_ptr<MaterialInstance> look = acquireColorLook( *pMesh, color );
            if ( look != nullptr )
                pMesh->setMaterialInstance( look );
            pMesh->setVisible( true );
            pEffect->activate( lifetime );
            return;
        }
    }

    const shared_ptr<MaterialInstance>& ShooterDirectorComponent::getDroneLook( bool bFlashing ) const
    {
        return bFlashing ? _droneFlashLook : _droneLook;
    }

    bool ShooterDirectorComponent::isAutoPlayOn() const
    {
        return _bAutoPlay || gv_shooterAutoPlay != 0;
    }

    const ShooterDirectorComponent* ShooterDirectorComponent::resolveDirector( const GameObjectManager& manager, GameObjectHandle director )
    {
        const GameObject* pObject = manager.resolveGameObject( director );
        return pObject != nullptr ? pObject->getComponent<ShooterDirectorComponent>() : nullptr;
    }

    // ------------------------------------------------------------------------------
    // 배치 · 스폰
    // ------------------------------------------------------------------------------
    void ShooterDirectorComponent::collectBoxes()
    {
        _listBox.clear();
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        // 씬에 놓인 벽 · 엄폐물 — 에디터에서 옮기면 충돌도 따라온다.
        vector<ShooterArenaBox>& listBox = _listBox;
        pManager->forEachComponentOfType<ShooterBlockerComponent>( [&listBox]( ShooterBlockerComponent* pBlocker )
        { listBox.push_back( pBlocker->computeBox() ); } );
    }

    void ShooterDirectorComponent::startPacing()
    {
        using Internal  = ShooterDirectorComponentInternal;
        _bPacingReady   = SW_FALSE;
        _bPacingRestart = SW_FALSE;
        if ( _profile.loadFromResource( _pacingProfile ) == false || _table.loadFromResource( _spawnTable ) == false )
        {
            SW_LOG_ERROR( "[Shooter] pacing data '%#' / '%#' could not be loaded - no drones will come", _pacingProfile.c_str(), _spawnTable.c_str() );
            return;
        }
        // 감독은 id 만 낸다 — 이 게임이 모르는 id 는 아무 일도 하지 않으므로 데이터 오타를 여기서 알린다.
        vector<hashed_string> listId;
        _profile.collectEncounterIds( listId );
        for ( const hashed_string& id : listId )
        {
            if ( Internal::isKnownId( id, Internal::kArrKnownEncounter ) == false )
                SW_LOG_ERROR( "[Shooter] pacing profile '%#' names encounter '%#' this game does not know", _pacingProfile.c_str(), id.c_str() );
        }
        for ( const SpawnEntryDef& entry : _table.getEntries() )
        {
            if ( Internal::isKnownId( entry._id, Internal::kArrKnownSpawn ) == false )
                SW_LOG_ERROR( "[Shooter] spawn table '%#' names '%#' this game does not know", _spawnTable.c_str(), entry._id.c_str() );
        }
        _director.initialize( &_profile, &_table, static_cast<uint32>( _pacingSeed ) );
        _bPacingReady = SW_TRUE;
        applyDirectorEvents(); // 시작 단계 사건
    }

    void ShooterDirectorComponent::requestDrones( int32 count, float32 healthScale, uint32 spawnId )
    {
        // 웨이브(감독 순환 + 1)마다 체력 · 속도가 오른다 — 감독이 정하는 것은 언제 · 몇 기이고, 드론 한 기의 세기는 게임 규칙이다.
        const float32 wave   = static_cast<float32>( getWave() );
        const float32 health = ( 30.0f + 10.0f * wave ) * MathUtil::max( 0.1f, healthScale );
        const float32 speed  = MathUtil::min( 6.5f, 2.6f + 0.3f * wave );
        for ( int32 droneIndex = 0; droneIndex < count; ++droneIndex )
        {
            DroneRequest request;
            request._health   = health;
            request._speed    = speed;
            request._bobPhase = static_cast<float32>( _spawnCursor ) * 1.3f;
            request._slot     = _spawnCursor++;
            request._spawnId  = spawnId;
            _listPendingDrone.push_back( request );
        }
    }

    void ShooterDirectorComponent::applyDirectorEvents()
    {
        using Internal = ShooterDirectorComponentInternal;
        _listDirectorEvent.clear();
        _director.drainEvents( _listDirectorEvent );
        for ( const AiDirectorEvent& event : _listDirectorEvent )
        {
            switch ( event._kind )
            {
                case AiDirectorEventKind::PhaseChanged:
                {
                    SW_LOG_INFO( "[Shooter] pacing '%#' -> '%#' (wave %#, intensity %.2f)", event._source.empty() ? "start" : event._source.c_str(), event._id.c_str(), getWave(),
                                 event._intensity );
                    break;
                }
                case AiDirectorEventKind::Spawned:
                {
                    requestDrones( 1, 1.0f, event._spawnId );
                    break;
                }
                case AiDirectorEventKind::Encounter:
                {
                    requestDrones( event._count, event._scale, 0 );
                    SW_LOG_INFO( "[Shooter] %# - %# drones (x%.1f health) on wave %#", event._id.c_str(), event._count, event._scale, getWave() );
                    break;
                }
                case AiDirectorEventKind::Reward:
                {
                    if ( event._id == hashed_string( "ammo" ) )
                        _bAmmoPending = SW_TRUE;
                    else if ( event._id == hashed_string( "repair" ) )
                        _pendingHeal += event._scale * Internal::kRepairPerScale;
                    break;
                }
                case AiDirectorEventKind::Despawned:
                {
                    break;
                }
            }
        }
    }

    void ShooterDirectorComponent::scheduleFlush()
    {
        if ( _bFlushScheduled == SW_TRUE )
            return;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        _bFlushScheduled = SW_TRUE;
        // 틱 안이면 틱 뒤로 미뤄진다. 그 사이에 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            ShooterDirectorComponent* pDirector = static_cast<ShooterDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->flushPending();
        } );
    }

    void ShooterDirectorComponent::flushPending()
    {
        _bFlushScheduled            = SW_FALSE;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || _bStarted == SW_FALSE )
            return;
        if ( _bPoolSpawned == SW_FALSE )
            spawnEffectPool( *pManager );
        for ( const DroneRequest& request : _listPendingDrone )
            spawnDrone( *pManager, request );
        _listPendingDrone.clear();
        for ( const EffectRequest& effect : _listPendingEffect )
            spawnEffect( effect._position, effect._size, effect._color, effect._lifetime );
        _listPendingEffect.clear();
        const bool bPlayerPending = _bAmmoPending == SW_TRUE || _pendingHeal > 0.0f;
        if ( bPlayerPending )
        {
            const GameObject*       pObject = pManager->resolveGameObject( _player );
            ShooterPlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pPlayer != nullptr && _bAmmoPending == SW_TRUE )
                pPlayer->addWaveAmmo();
            if ( pPlayer != nullptr && _pendingHeal > 0.0f )
                pPlayer->restoreHealth( _pendingHeal );
            _bAmmoPending = SW_FALSE;
            _pendingHeal  = 0.0f;
        }
        for ( const utf8* pPath : _listPendingSound )
            (void)GameSound::play( pPath );
        _listPendingSound.clear();
    }

    void ShooterDirectorComponent::spawnEffectPool( GameObjectManager& manager )
    {
        _bPoolSpawned               = SW_TRUE;
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || _effectPrefab.empty() )
            return;
        // 탄착 · 터짐 구를 미리 세우고 숨긴다(오브젝트를 매 발 만들고 지우지 않는다).
        for ( int32 effectIndex = 0; effectIndex < _effectPoolSize; ++effectIndex )
        {
            GameObject*    pObject = pAssetManager->getPrefabCache().spawn( &manager, _effectPrefab, "ShotEffect" );
            MeshComponent* pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
                continue;
            pMesh->setVisible( false );
            _listEffect.push_back( pObject->getHandle() );
        }
    }

    void ShooterDirectorComponent::spawnDrone( GameObjectManager& manager, const DroneRequest& request )
    {
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || _dronePrefab.empty() )
            return;
        GameObject*            pObject = pAssetManager->getPrefabCache().spawn( &manager, _dronePrefab, "Drone" );
        ShooterDroneComponent* pDrone  = pObject != nullptr ? pObject->getComponent<ShooterDroneComponent>() : nullptr;
        MeshComponent*         pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pDrone == nullptr || pMesh == nullptr )
        {
            if ( pObject != nullptr )
                manager.destroyObject( pObject );
            if ( request._spawnId != 0 && _bPacingReady == SW_TRUE )
                (void)_director.notifyDespawned( request._spawnId ); // 서지 못한 드론의 예산 자리를 돌려준다
            return;
        }
        applyDroneLook( *pMesh );
        // 스폰 자리를 차례로 돈다. 한 바퀴를 돌 때마다 옆으로 1.2 m 비켜 같은 자리에 겹쳐 서지 않는다.
        float3 position{ 0.0f, _droneHeight, 0.0f };
        if ( _listSpawnPoint.empty() == false )
        {
            const uint32          pointCount = static_cast<uint32>( _listSpawnPoint.size() );
            const GameObject*     pPoint     = manager.resolveGameObject( _listSpawnPoint[request._slot % pointCount] );
            const SceneComponent* pScene     = pPoint != nullptr ? pPoint->getPrimarySceneComponent() : nullptr;
            const float3          base       = pScene != nullptr ? pScene->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f };
            position                         = base + float3{ static_cast<float32>( request._slot / pointCount % 3u ) * 1.2f, _droneHeight, 0.0f };
        }
        pDrone->launch( getOwner()->getHandle(), position, request._health, request._speed, request._bobPhase );
        DroneRecord record;
        record._object  = pObject->getHandle();
        record._spawnId = request._spawnId;
        _listDrone.push_back( record );
    }

    void ShooterDirectorComponent::applyDroneLook( MeshComponent& mesh )
    {
        // 드론은 팔레트 머티리얼(씬의 상자가 늘 들고 있다)에서 만든 두 모습을 나눠 쓴다 — 보통 · 맞았을 때.
        if ( _droneLook == nullptr && mesh.getMaterial() != nullptr )
        {
            _droneLook      = MaterialInstance::create( mesh.getMaterial() );
            _droneFlashLook = MaterialInstance::create( mesh.getMaterial() );
            if ( _droneLook != nullptr )
                _droneLook->setVectorParameter( hashed_string( "color" ), float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
            if ( _droneFlashLook != nullptr )
                _droneFlashLook->setVectorParameter( hashed_string( "color" ), _droneFlashTint );
        }
        if ( _droneLook != nullptr )
            mesh.setMaterialInstance( _droneLook );
    }

    shared_ptr<MaterialInstance> ShooterDirectorComponent::acquireColorLook( MeshComponent& mesh, const float4& color )
    {
        Material* pMaterial = mesh.getMaterial();
        if ( pMaterial == nullptr )
            return nullptr;
        for ( const ColorLook& look : _listColorLook )
        {
            if ( ShooterDirectorComponentInternal::isSameColor( look._color, color ) )
                return look._instance;
        }
        ColorLook look;
        look._instance = MaterialInstance::create( pMaterial );
        if ( look._instance == nullptr )
            return nullptr;
        look._instance->setVectorParameter( hashed_string( "color" ), color );
        look._color = color;
        _listColorLook.push_back( look );
        return look._instance;
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics — 플레이어 · 드론이 쓰지 않는 그룹)
    // ------------------------------------------------------------------------------
    void ShooterDirectorComponent::updateDrones()
    {
        GameObjectManager* pManager = getObjectManager();
        _listDroneView.clear();
        if ( pManager == nullptr )
            return;
        int32 nearCount = 0;
        for ( size_t droneIndex = 0; droneIndex < _listDrone.size(); )
        {
            GameObject*                  pObject = pManager->resolveGameObject( _listDrone[droneIndex]._object );
            const ShooterDroneComponent* pDrone  = pObject != nullptr ? pObject->getComponent<ShooterDroneComponent>() : nullptr;
            if ( pDrone == nullptr || pDrone->isDead() )
            {
                if ( _bPacingReady == SW_TRUE && _listDrone[droneIndex]._spawnId != 0 )
                    (void)_director.notifyDespawned( _listDrone[droneIndex]._spawnId );
                if ( _bPacingReady == SW_TRUE && pDrone != nullptr )
                    (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "droneKilled" ), 1.0f );
                if ( pDrone != nullptr )
                {
                    ++_killCount;
                    EffectRequest burst;
                    burst._position = pDrone->getPosition();
                    burst._size     = 1.4f;
                    burst._color    = ShooterDirectorComponentInternal::kBurstColor;
                    burst._lifetime = 0.2f;
                    _listPendingEffect.push_back( burst );
                    _listPendingSound.push_back( ShooterDirectorComponentInternal::kSoundDroneDown );
                    pManager->destroyObject( pObject );
                }
                _listDrone[droneIndex] = _listDrone.back();
                _listDrone.pop_back();
                continue;
            }
            ShooterDroneView view;
            view._object   = _listDrone[droneIndex]._object;
            view._position = pDrone->getPosition();
            view._radius   = pDrone->getRadius();
            _listDroneView.push_back( view );
            const float3 toPlayer = view._position - _playerEye;
            nearCount += toPlayer.getLength() < ShooterDirectorComponentInternal::kNearDistance ? 1 : 0;
            ++droneIndex;
        }
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().setSignal( hashed_string( "dronesNear" ), static_cast<float32>( nearCount ) );
    }

    void ShooterDirectorComponent::updatePlayerView()
    {
        GameObjectManager*            pManager = getObjectManager();
        const GameObject*             pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
            return;
        _playerEye = pPlayer->getEyePosition();
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().setSignal( hashed_string( "lowAmmo" ), pPlayer->computeAmmoShortage() );
    }

    void ShooterDirectorComponent::logStatus( float32 deltaTime )
    {
        _statusTimer += deltaTime;
        if ( _statusTimer < ShooterDirectorComponentInternal::kStatusInterval )
            return;
        _statusTimer                           = 0.0f;
        GameObjectManager*            pManager = getObjectManager();
        const GameObject*             pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
            return;
        [[maybe_unused]] const WeaponState& weapon  = pPlayer->getCurrentWeapon();
        [[maybe_unused]] const uint32       percent = pPlayer->getShotCount() > 0u ? pPlayer->getHitCount() * 100u / pPlayer->getShotCount() : 0u;
        SW_LOG_INFO( "[Shooter] wave %# (%#, intensity %.2f) · kills %# · HP %# · %# %#/%# · drones %# · accuracy %#%%", getWave(), _director.getPhase().c_str(),
                     _director.getIntensity(), _killCount, static_cast<int32>( pPlayer->getHealth() ), weapon.getDef()._name.c_str(), weapon.getMagazineAmmo(),
                     weapon.getReserveAmmo(), static_cast<uint32>( _listDrone.size() ), percent );
    }

    GameObjectManager* ShooterDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
