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
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Utility/StateArchiveUtil.h"

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
            static constexpr float32     kStatusInterval = 5.0f;
            static constexpr uint32      kStateTag       = 0x544F4853u; ///< 'SHOT'
            static constexpr uint32      kStateVersion   = 1;
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
        , _waveDelay{ 3.0f }
        , _effectPoolSize{ 32 }
        , _droneFlashTint{ 1.0f, 0.35f, 0.3f, 1.0f }
        , _bAutoPlay{ false }
        , _listBox{}
        , _listDroneView{}
        , _listDrone{}
        , _listEffect{}
        , _listPendingDrone{}
        , _listPendingEffect{}
        , _listPendingSound{}
        , _listColorLook{}
        , _pendingStateBytes{}
        , _droneLook{}
        , _droneFlashLook{}
        , _playerEye{ 0.0f, 1.6f, -16.0f }
        , _waveTimer{ 3.0f }
        , _statusTimer{ 0.0f }
        , _wave{ 0 }
        , _killCount{ 0 }
        , _bStarted{ SW_FALSE }
        , _bPoolSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bAmmoPending{ SW_FALSE }
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
        _bStarted  = SW_TRUE;
        _waveTimer = _waveDelay;
        _wave      = 0;
        _killCount = 0;
        if ( _pendingStateBytes.empty() == false )
            applyPendingState();
        updatePlayerView();
        scheduleFlush();
    }

    void ShooterDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, ShooterDirectorComponentInternal::kStateTag, ShooterDirectorComponentInternal::kStateVersion );
        outArchive << _wave;
        outArchive << _killCount;
    }

    void ShooterDirectorComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bStarted == SW_TRUE )
            applyPendingState();
    }

    void ShooterDirectorComponent::applyPendingState()
    {
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        uint32     wave      = 0;
        uint32     killCount = 0;
        const bool bHeader   = StateArchiveUtil::readHeader( archive, ShooterDirectorComponentInternal::kStateTag, ShooterDirectorComponentInternal::kStateVersion );
        archive >> wave;
        archive >> killCount;
        _pendingStateBytes.clear();
        if ( bHeader == false || archive.isError() || archive.getRemainingBytes() != 0 )
        {
            SW_LOG_WARNING( "[Shooter] the saved arena state does not match this build - starting from wave 1" );
            return;
        }
        // 드론은 걷었다 — 웨이브 대기 뒤 `requestWave` 가 하나 올리므로 하나 앞에 둔다(같은 웨이브를 새로 세운다).
        _wave      = wave > 0 ? wave - 1 : 0;
        _killCount = killCount;
        despawnRuntime();
        SW_LOG_INFO( "[Shooter] arena state restored - wave %#, %# kills", wave, _killCount );
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
        if ( step > 0.0f )
        {
            updateDrones();
            if ( _listDrone.empty() && _listPendingDrone.empty() )
            {
                _waveTimer -= step;
                if ( _waveTimer <= 0.0f )
                    requestWave();
            }
            logStatus( step );
        }
        updatePlayerView();
        const bool bPending = _listPendingDrone.empty() == false || _listPendingEffect.empty() == false || _listPendingSound.empty() == false || _bAmmoPending == SW_TRUE;
        if ( bPending )
            scheduleFlush();
    }

    void ShooterDirectorComponent::despawnRuntime()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( const GameObjectHandle& handle : _listDrone )
            {
                GameObject* pObject = pManager->resolveGameObject( handle );
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
        _waveTimer    = _waveDelay;
        _bPoolSpawned = SW_FALSE;
    }

    void ShooterDirectorComponent::restartRound()
    {
        GameObjectManager* pManager = getObjectManager();
        for ( const GameObjectHandle& handle : _listDrone )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyObject( pObject );
        }
        _listDrone.clear();
        _listDroneView.clear();
        _listPendingDrone.clear();
        _wave      = 0;
        _killCount = 0;
        _waveTimer = _waveDelay;
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

    void ShooterDirectorComponent::requestWave()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || _listSpawnPoint.empty() )
            return;
        ++_wave;
        const uint32  droneCount = 3u + 2u * _wave;
        const float32 health     = 30.0f + 10.0f * static_cast<float32>( _wave );
        const float32 speed      = MathUtil::min( 6.5f, 2.6f + 0.3f * static_cast<float32>( _wave ) );
        const uint32  pointCount = static_cast<uint32>( _listSpawnPoint.size() );
        for ( uint32 droneIndex = 0; droneIndex < droneCount; ++droneIndex )
        {
            const GameObject*     pPoint = pManager->resolveGameObject( _listSpawnPoint[droneIndex % pointCount] );
            const SceneComponent* pScene = pPoint != nullptr ? pPoint->getPrimarySceneComponent() : nullptr;
            const float3          base   = pScene != nullptr ? pScene->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f };
            DroneRequest          request;
            request._position = base + float3{ static_cast<float32>( droneIndex / pointCount ) * 1.2f, _droneHeight, 0.0f };
            request._health   = health;
            request._speed    = speed;
            request._bobPhase = static_cast<float32>( droneIndex ) * 1.3f;
            _listPendingDrone.push_back( request );
        }
        _bAmmoPending = SW_TRUE; // 웨이브마다 탄을 조금 채운다(틱 뒤 — 플레이어는 다른 오브젝트다)
        _waveTimer    = _waveDelay;
        SW_LOG_INFO( "[Shooter] wave %# - %# drones (%# HP, %# m/s)", _wave, droneCount, static_cast<int32>( health ), speed );
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
        if ( _bAmmoPending == SW_TRUE )
        {
            _bAmmoPending                   = SW_FALSE;
            const GameObject*       pObject = pManager->resolveGameObject( _player );
            ShooterPlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pPlayer != nullptr )
                pPlayer->addWaveAmmo();
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
            return;
        }
        applyDroneLook( *pMesh );
        pDrone->launch( getOwner()->getHandle(), request._position, request._health, request._speed, request._bobPhase );
        _listDrone.push_back( pObject->getHandle() );
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
        for ( size_t droneIndex = 0; droneIndex < _listDrone.size(); )
        {
            GameObject*                  pObject = pManager->resolveGameObject( _listDrone[droneIndex] );
            const ShooterDroneComponent* pDrone  = pObject != nullptr ? pObject->getComponent<ShooterDroneComponent>() : nullptr;
            if ( pDrone == nullptr || pDrone->isDead() )
            {
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
            view._object   = _listDrone[droneIndex];
            view._position = pDrone->getPosition();
            view._radius   = pDrone->getRadius();
            _listDroneView.push_back( view );
            ++droneIndex;
        }
    }

    void ShooterDirectorComponent::updatePlayerView()
    {
        GameObjectManager*            pManager = getObjectManager();
        const GameObject*             pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        if ( pPlayer != nullptr )
            _playerEye = pPlayer->getEyePosition();
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
        SW_LOG_INFO( "[Shooter] wave %# · kills %# · HP %# · %# %#/%# · drones %# · accuracy %#%%", _wave, _killCount, static_cast<int32>( pPlayer->getHealth() ),
                     weapon.getDef()._name.c_str(), weapon.getMagazineAmmo(), weapon.getReserveAmmo(), static_cast<uint32>( _listDrone.size() ), percent );
    }

    GameObjectManager* ShooterDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
