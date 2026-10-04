#include "pch.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Camera/CameraDirectorComponent.h"
#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Utility/StateArchiveUtil.h"

#include "Games/Shooter3D/ShooterEffectComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterDirector" );

    namespace
    {
        struct ShooterDirectorComponentInternal
        {
            static constexpr float32 kStatusInterval = 5.0f;
            static constexpr uint32  kStateTag       = 0x544F4853u; ///< 'SHOT'
            static constexpr uint32  kStateVersion   = 2;
            /** @brief 이 거리(m) 안의 적이 "가까운 적" 신호입니다. */
            static constexpr float32 kNearDistance = 7.0f;
            /** @brief 수리 보상의 scale 1 이 채우는 체력입니다. */
            static constexpr float32 kRepairPerScale = 10.0f;
            /** @brief 이 게임이 아는 조우 · 보상 · 스폰 id 입니다 — 프로필이 다른 이름을 쓰면 시작할 때 알린다. */
            static constexpr const utf8* kArrKnownEncounter[] = { "swarm", "elite", "ammo", "repair" };
            static constexpr const utf8* kArrKnownSpawn[]     = { "skeleton" };
            static constexpr const utf8* kEventEnemyDown      = "EnemyDown"; ///< shooter3d.audioevents.xml

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
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_shooterAutoPlay, 0, "Shooter3D: 조준 · 사격도 AI 가 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_shooterAutoPlay, "Shooter3D", "Aim, shoot and move by AI" );
    /** @brief `-gv_shooterMotionTrace=<경로>` — 프레임마다 플레이어 몸 · 본 · 카메라 · 적의 그려진 자리를 CSV 로 남깁니다(튐 진단, 끝날 때 쓴다). */
    SW_TEST_GLOBAL_VARIABLE_STRING( gv_shooterMotionTrace, "", "Shooter3D: 프레임마다 몸 · 본 · 카메라 · 적 자리를 CSV 로 (경로, 비면 끔)" );
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
        : _enemyPrefab{ "game/shooter3d/prefabs/skeleton.prefab.xml" }
        , _effectPrefab{}
        , _tracerPrefab{ "game/shooter3d/prefabs/tracer.prefab.xml" }
        , _listEnemyPreset{ "SkeletonMinion", "SkeletonRaider", "SkeletonRogue", "SkeletonRaider" }
        , _eliteEnemyPreset{ "SkeletonWarrior" }
        , _player{}
        , _listSpawnPoint{}
        , _arenaHalfSize{ 20.0f }
        , _pacingProfile{}
        , _spawnTable{}
        , _pacingSeed{ 1 }
        , _effectPoolSize{ 32 }
        , _tracerPoolSize{ 24 }
        , _bAutoPlay{ false }
        , _listBox{}
        , _listEnemyView{}
        , _profile{}
        , _table{}
        , _director{}
        , _listDirectorEvent{}
        , _listEnemy{}
        , _effectPool{}
        , _tracerPool{}
        , _listPendingEnemy{}
        , _listPendingEffect{}
        , _listPendingEnemyDown{}
        , _listColorLook{}
        , _pendingStateBytes{}
        , _motionTrace{}
        , _playerEye{ 0.0f, 1.6f, -16.0f }
        , _playerFeet{ 0.0f, 0.0f, -16.0f }
        , _statusTimer{ 0.0f }
        , _pendingHeal{ 0.0f }
        , _spawnCursor{ 0 }
        , _killCount{ 0 }
        , _traceFrame{ 0 }
        , _bStarted{ SW_FALSE }
        , _bPoolSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _bAmmoPending{ SW_FALSE }
        , _bPacingReady{ SW_FALSE }
        , _bPacingRestart{ SW_FALSE }
        , _bPlayerAlive{ SW_TRUE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ShooterDirectorComponent::~ShooterDirectorComponent() = default;

    void ShooterDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 판의 규칙은 앞 그룹 — 플레이어 · 적이 같은 프레임에 이 결과를 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        collectBoxes();
        startPacing();
        _bStarted  = SW_TRUE;
        _killCount = 0;
        if ( _pendingStateBytes.empty() == false )
            applyPendingState();
        updatePlayerView();
        scheduleFlush();
    }

    void ShooterDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, ShooterDirectorComponentInternal::kStateTag, ShooterDirectorComponentInternal::kStateVersion );
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
        uint32     killCount = 0;
        const bool bHeader   = StateArchiveUtil::readHeader( archive, ShooterDirectorComponentInternal::kStateTag, ShooterDirectorComponentInternal::kStateVersion );
        archive >> killCount;
        _pendingStateBytes.clear();
        if ( bHeader == false || archive.isError() || archive.getRemainingBytes() != 0 )
        {
            SW_LOG_WARNING( "[Shooter] the saved arena state does not match this build - starting a new round" );
            return;
        }
        // 적은 걷고(`despawnRuntime` 이 다음 틱에 감독도 처음부터 돌리게 한다) 처치 수만 잇는다 — 감독의 주기 · 시간 · 풀은 싣지 않는다.
        _killCount = killCount;
        despawnRuntime();
        SW_LOG_INFO( "[Shooter] arena state restored - %# kills", _killCount );
    }

    void ShooterDirectorComponent::onEndPlay()
    {
        if ( _motionTrace.empty() == false && FileUtil::writeTextFile( gv_shooterMotionTrace, _motionTrace ) == false )
            SW_LOG_WARNING( "[Shooter] motion trace '%#' could not be written", gv_shooterMotionTrace.c_str() );
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
        if ( gv_shooterMotionTrace.empty() == false )
            appendMotionTrace( deltaTime );
        updatePlayerView();
        if ( step > 0.0f )
        {
            updateEnemies();
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
        const bool bPending = _listPendingEnemy.empty() == false || _listPendingEffect.empty() == false || _listPendingEnemyDown.empty() == false || _bAmmoPending == SW_TRUE ||
                              _pendingHeal > 0.0f;
        if ( bPending )
            scheduleFlush();
    }

    void ShooterDirectorComponent::despawnRuntime()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager != nullptr )
        {
            for ( const EnemyRecord& enemy : _listEnemy )
            {
                GameObject* pObject = pManager->resolveGameObject( enemy._object );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
            }
            despawnPool( *pManager, _effectPool );
            despawnPool( *pManager, _tracerPool );
        }
        _listEnemy.clear();
        _effectPool = EffectPool{};
        _tracerPool = EffectPool{};
        _listEnemyView.clear();
        _listPendingEnemy.clear();
        _listPendingEffect.clear();
        _bPoolSpawned   = SW_FALSE;
        _bPacingRestart = SW_TRUE; // 걷은 적의 스폰 id 를 돌려줄 수 없다 — 다음 틱에 감독도 처음부터
    }

    void ShooterDirectorComponent::restartRound()
    {
        clearEnemies();
        _killCount   = 0;
        _pendingHeal = 0.0f;
        if ( _bPacingReady == SW_TRUE )
            _director.restart();
        _listDirectorEvent.clear();
    }

    void ShooterDirectorComponent::clearEnemies()
    {
        GameObjectManager* pManager = getObjectManager();
        for ( const EnemyRecord& enemy : _listEnemy )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( enemy._object ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyObject( pObject );
        }
        _listEnemy.clear();
        _listEnemyView.clear();
        _listPendingEnemy.clear();
    }

    void ShooterDirectorComponent::reportPlayerDamage( float32 amount )
    {
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "damageTaken" ), amount );
    }

    void ShooterDirectorComponent::spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime )
    {
        GameObjectManager* pManager = getObjectManager();
        MeshComponent*     pMesh    = pManager != nullptr ? acquirePooledMesh( *pManager, _effectPool, lifetime ) : nullptr;
        if ( pMesh == nullptr )
            return;
        pMesh->setLocalPosition( position );
        pMesh->setLocalRotation( float3{ 0.0f, 0.0f, 0.0f } );
        pMesh->setLocalScale( float3{ size } );
        const shared_ptr<MaterialInstance> look = acquireColorLook( *pMesh, color );
        if ( look != nullptr )
            pMesh->setMaterialInstance( look );
        pMesh->setVisible( true );
    }

    void ShooterDirectorComponent::spawnTracer( const float3& from, const float3& to, float32 width, const float4& color, float32 lifetime )
    {
        const float3  span   = to - from;
        const float32 length = span.getLength();
        if ( length < 1.0e-3f )
            return;
        GameObjectManager* pManager = getObjectManager();
        MeshComponent*     pMesh    = pManager != nullptr ? acquirePooledMesh( *pManager, _tracerPool, lifetime ) : nullptr;
        if ( pMesh == nullptr )
            return;
        // 단위 상자를 선분 가운데에 두고 진행 방향(요 · 피치)으로 돌려 Z 로 늘린다.
        const float3  direction = span * ( 1.0f / length );
        const float32 yaw       = MathUtil::atan2( direction._x, direction._z );
        const float32 pitch     = -MathUtil::asin( MathUtil::clamp( direction._y, -1.0f, 1.0f ) );
        pMesh->setLocalPosition( from + span * 0.5f );
        pMesh->setLocalRotation( float3{ pitch, yaw, 0.0f } );
        pMesh->setLocalScale( float3{ width, width, length } );
        const shared_ptr<MaterialInstance> look = acquireColorLook( *pMesh, color );
        if ( look != nullptr )
            pMesh->setMaterialInstance( look );
        pMesh->setVisible( true );
    }

    bool ShooterDirectorComponent::isAutoPlayOn() const
    {
        return _bAutoPlay || GameAutoplay::isOn();
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
            SW_LOG_ERROR( "[Shooter] pacing data '%#' / '%#' could not be loaded - no enemies will come", _pacingProfile.c_str(), _spawnTable.c_str() );
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

    void ShooterDirectorComponent::requestEnemies( int32 count, float32 healthScale, uint32 spawnId, bool bElite )
    {
        // 웨이브(감독 순환 + 1)마다 체력 · 속도가 오른다 — 감독이 정하는 것은 언제 · 몇 기이고, 적 한 기의 세기는 게임 규칙이다.
        const float32 wave   = static_cast<float32>( getWave() );
        const float32 health = ( 30.0f + 10.0f * wave ) * MathUtil::max( 0.1f, healthScale );
        const float32 speed  = MathUtil::min( 5.5f, 2.2f + 0.3f * wave );
        for ( int32 enemyIndex = 0; enemyIndex < count; ++enemyIndex )
        {
            EnemyRequest request;
            request._health  = health;
            request._speed   = speed;
            request._slot    = _spawnCursor++;
            request._spawnId = spawnId;
            request._bElite  = bElite ? SW_TRUE : SW_FALSE;
            _listPendingEnemy.push_back( request );
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
                    // 쌓기로 돌아왔다 = 새 웨이브. 진행 사건(동의가 없으면 텔레메트리가 버린다).
                    TelemetryService* pTelemetry = game::getService<TelemetryService>();
                    const bool        bNewWave   = event._source.empty() == false && event._id == _profile.getPhases()[static_cast<size_t>( _profile.getStartPhaseIndex() )]._id;
                    if ( pTelemetry != nullptr && bNewWave )
                    {
                        TelemetryEvent waveReached( "progression.waveReached" );
                        waveReached.setInt( "wave", getWave() ).setInt( "kills", _killCount ).setFloat( "seconds", static_cast<float64>( _director.getTime() ) );
                        (void)pTelemetry->record( waveReached );
                    }
                    SW_LOG_INFO( "[Shooter] pacing '%#' -> '%#' (wave %#, intensity %.2f)", event._source.empty() ? "start" : event._source.c_str(), event._id.c_str(), getWave(),
                                 event._intensity );
                    break;
                }
                case AiDirectorEventKind::Spawned:
                {
                    requestEnemies( 1, 1.0f, event._spawnId, false );
                    break;
                }
                case AiDirectorEventKind::Encounter:
                {
                    requestEnemies( event._count, event._scale, 0, event._id == hashed_string( "elite" ) );
                    SW_LOG_INFO( "[Shooter] %# - %# skeletons (x%.1f health) on wave %#", event._id.c_str(), event._count, event._scale, getWave() );
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
            spawnEffectPools( *pManager );
        for ( const EnemyRequest& request : _listPendingEnemy )
            spawnEnemy( *pManager, request );
        _listPendingEnemy.clear();
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
        for ( const float3& position : _listPendingEnemyDown )
            (void)GameSound::postEventAt( hashed_string( ShooterDirectorComponentInternal::kEventEnemyDown ), position );
        _listPendingEnemyDown.clear();
    }

    void ShooterDirectorComponent::spawnEffectPools( GameObjectManager& manager )
    {
        _bPoolSpawned = SW_TRUE;
        // 탄착 · 섬광 구와 탄도선 상자를 미리 세우고 숨긴다(오브젝트를 매 발 만들고 지우지 않는다).
        spawnPool( manager, _effectPrefab, _effectPoolSize, "ShotEffect", _effectPool );
        spawnPool( manager, _tracerPrefab, _tracerPoolSize, "Tracer", _tracerPool );
    }

    void ShooterDirectorComponent::spawnPool( GameObjectManager& manager, const string& prefab, int32 count, const utf8* pName, EffectPool& outPool )
    {
        outPool                     = EffectPool{};
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || prefab.empty() )
            return;
        for ( int32 effectIndex = 0; effectIndex < count; ++effectIndex )
        {
            GameObject*    pObject = pAssetManager->getPrefabCache().spawn( &manager, prefab, pName );
            MeshComponent* pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
                continue;
            pMesh->setVisible( false );
            outPool._listObject.push_back( pObject->getHandle() );
        }
    }

    void ShooterDirectorComponent::despawnPool( GameObjectManager& manager, EffectPool& inoutPool )
    {
        for ( const GameObjectHandle& handle : inoutPool._listObject )
        {
            GameObject* pObject = manager.resolveGameObject( handle );
            if ( pObject != nullptr )
                manager.destroyObject( pObject );
        }
        inoutPool = EffectPool{};
    }

    MeshComponent* ShooterDirectorComponent::acquirePooledMesh( GameObjectManager& manager, EffectPool& inoutPool, float32 lifetime )
    {
        const uint32 poolSize = static_cast<uint32>( inoutPool._listObject.size() );
        if ( poolSize == 0 )
            return nullptr;
        // 숨어 있는 것을 순번부터 찾는다. 다 쓰고 있으면 순번 자리의 것(가장 먼저 꺼낸 쪽)을 다시 쓴다 — 연사가 풀보다 빨라도 새 탄도선이 보인다.
        uint32 chosen = inoutPool._cursor % poolSize;
        for ( uint32 probe = 0; probe < poolSize; ++probe )
        {
            const uint32                  slot    = ( inoutPool._cursor + probe ) % poolSize;
            const GameObject*             pObject = manager.resolveGameObject( inoutPool._listObject[slot] );
            const ShooterEffectComponent* pEffect = pObject != nullptr ? pObject->getComponent<ShooterEffectComponent>() : nullptr;
            if ( pEffect != nullptr && pEffect->isIdle() )
            {
                chosen = slot;
                break;
            }
        }
        inoutPool._cursor               = ( chosen + 1 ) % poolSize;
        GameObject*             pObject = manager.resolveGameObject( inoutPool._listObject[chosen] );
        ShooterEffectComponent* pEffect = pObject != nullptr ? pObject->getComponent<ShooterEffectComponent>() : nullptr;
        MeshComponent*          pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pEffect == nullptr || pMesh == nullptr )
            return nullptr;
        pEffect->activate( lifetime );
        return pMesh;
    }

    void ShooterDirectorComponent::spawnEnemy( GameObjectManager& manager, const EnemyRequest& request )
    {
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || _enemyPrefab.empty() )
            return;
        GameObject*                   pObject     = pAssetManager->getPrefabCache().spawn( &manager, _enemyPrefab, "Skeleton" );
        ShooterEnemyComponent*        pEnemy      = pObject != nullptr ? pObject->getComponent<ShooterEnemyComponent>() : nullptr;
        CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pEnemy == nullptr )
        {
            if ( pObject != nullptr )
                manager.destroyObject( pObject );
            if ( request._spawnId != 0 && _bPacingReady == SW_TRUE )
                (void)_director.notifyDespawned( request._spawnId ); // 서지 못한 적의 예산 자리를 돌려준다
            return;
        }
        // 모습 — 정예는 전사, 나머지는 목록을 차례로. 씨앗은 스폰 순번이라 같은 판은 같은 얼굴들이다.
        if ( pAppearance != nullptr )
        {
            const bool    bHasList = _listEnemyPreset.empty() == false;
            const string& preset   = request._bElite == SW_TRUE || bHasList == false ? _eliteEnemyPreset
                                                                                     : _listEnemyPreset[request._slot % static_cast<uint32>( _listEnemyPreset.size() )];
            pAppearance->setPreset( hashed_string( preset ), static_cast<uint32>( _pacingSeed ) * 7919u + request._slot );
        }
        // 스폰 자리를 차례로 돈다. 한 바퀴를 돌 때마다 옆으로 1.2 m 비켜 같은 자리에 겹쳐 서지 않는다.
        float3 position{ 0.0f, 0.0f, 0.0f };
        if ( _listSpawnPoint.empty() == false )
        {
            const uint32          pointCount = static_cast<uint32>( _listSpawnPoint.size() );
            const GameObject*     pPoint     = manager.resolveGameObject( _listSpawnPoint[request._slot % pointCount] );
            const SceneComponent* pScene     = pPoint != nullptr ? pPoint->getPrimarySceneComponent() : nullptr;
            const float3          base       = pScene != nullptr ? pScene->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f };
            position                         = float3{ base._x + static_cast<float32>( request._slot / pointCount % 3u ) * 1.2f, 0.0f, base._z };
        }
        // 처음에는 아레나 가운데를 본다.
        const float32 yaw = MathUtil::atan2( -position._x, -position._z );
        pEnemy->launch( getOwner()->getHandle(), position, yaw, request._health, request._speed );
        EnemyRecord record;
        record._object  = pObject->getHandle();
        record._spawnId = request._spawnId;
        _listEnemy.push_back( record );
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
    // 갱신(PrePhysics — 플레이어 · 적이 쓰지 않는 그룹)
    // ------------------------------------------------------------------------------
    void ShooterDirectorComponent::updateEnemies()
    {
        GameObjectManager* pManager = getObjectManager();
        _listEnemyView.clear();
        if ( pManager == nullptr )
            return;
        int32 nearCount = 0;
        for ( size_t enemyIndex = 0; enemyIndex < _listEnemy.size(); )
        {
            EnemyRecord&                 record  = _listEnemy[enemyIndex];
            GameObject*                  pObject = pManager->resolveGameObject( record._object );
            const ShooterEnemyComponent* pEnemy  = pObject != nullptr ? pObject->getComponent<ShooterEnemyComponent>() : nullptr;
            // 쓰러진 순간 한 번 — 처치 수 · 신호 · 효과음, 감독의 예산 자리를 돌려준다. 시체는 시체 시간 동안 남는다.
            const bool bJustDied = pEnemy != nullptr && pEnemy->isDead() && record._bCounted == SW_FALSE;
            if ( bJustDied )
            {
                record._bCounted = SW_TRUE;
                ++_killCount;
                if ( _bPacingReady == SW_TRUE )
                {
                    (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "enemyKilled" ), 1.0f );
                    if ( record._spawnId != 0 )
                        (void)_director.notifyDespawned( record._spawnId );
                }
                _listPendingEnemyDown.push_back( pEnemy->getPosition() );
            }
            const bool bGone = pEnemy == nullptr || pEnemy->isRemovable();
            if ( bGone )
            {
                if ( pEnemy == nullptr && _bPacingReady == SW_TRUE && record._spawnId != 0 && record._bCounted == SW_FALSE )
                    (void)_director.notifyDespawned( record._spawnId );
                if ( pObject != nullptr )
                    pManager->destroyObject( pObject );
                _listEnemy[enemyIndex] = _listEnemy.back();
                _listEnemy.pop_back();
                continue;
            }
            ++enemyIndex;
            if ( pEnemy->isAlive() == false )
                continue;
            ShooterEnemyView view;
            view._object   = record._object;
            view._position = pEnemy->getPosition();
            view._radius   = pEnemy->getRadius();
            view._height   = pEnemy->getHeight();
            _listEnemyView.push_back( view );
            const float3 toPlayer = view._position - _playerFeet;
            nearCount += toPlayer.getLength() < ShooterDirectorComponentInternal::kNearDistance ? 1 : 0;
        }
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().setSignal( hashed_string( "enemiesNear" ), static_cast<float32>( nearCount ) );
    }

    void ShooterDirectorComponent::updatePlayerView()
    {
        GameObjectManager*            pManager = getObjectManager();
        const GameObject*             pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
            return;
        _playerEye    = pPlayer->getEyePosition();
        _playerFeet   = pPlayer->getFeetPosition();
        _bPlayerAlive = pPlayer->isAlive() ? SW_TRUE : SW_FALSE;
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
        SW_LOG_INFO( "[Shooter] wave %# (%#, intensity %.2f) · kills %# · HP %# · %# %#/%# · skeletons %# · accuracy %#%%", getWave(), _director.getPhase().c_str(),
                     _director.getIntensity(), _killCount, static_cast<int32>( pPlayer->getHealth() ), weapon.getDef()._name.c_str(), weapon.getMagazineAmmo(),
                     weapon.getReserveAmmo(), static_cast<uint32>( _listEnemyView.size() ), percent );
    }

    void ShooterDirectorComponent::appendMotionTrace( float32 deltaTime )
    {
        // 디렉터는 앞 그룹(PrePhysics)이라 여기서 읽는 월드 · 본은 지난 프레임에 그려진 그대로다.
        GameObjectManager*            pManager = getObjectManager();
        const GameObject*             pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
            return;
        if ( _motionTrace.empty() )
            _motionTrace = "frame,dt,feetX,feetZ,lookYaw,bodyX,bodyY,bodyZ,bodyYaw,rootX,rootY,rootZ,hipsX,hipsY,hipsZ,headX,headY,headZ,handX,handY,handZ,"
                           "camX,camY,camZ,camYaw,enemyX,enemyZ,enemyHipsY,state\n";
        static constexpr const utf8* kArrBone[4] = { "root", "hips", "head", "hand.r" };
        float3                       arrBone[4]{};
        float3                       body{};
        float32                      bodyYaw = 0.0f;
        hashed_string                state;
        const GameObject*            pBody = pPlayer->findBodyObject();
        if ( pBody != nullptr )
        {
            const SkeletalMeshComponent* pUnit = pBody->getComponent<SkeletalMeshComponent>();
            if ( pUnit != nullptr )
            {
                const float4x4 world = pUnit->getWorldMatrix();
                body                 = world.getTranslation();
                const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                bodyYaw              = MathUtil::atan2( forward._x, forward._z );
                for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
                {
                    float4x4 bone;
                    if ( pUnit->findBoneModelTransform( hashed_string( kArrBone[boneIndex] ), bone ) )
                        arrBone[boneIndex] = bone.getTranslation();
                }
            }
            const SkeletalAnimatorComponent* pAnimator = pBody->getComponent<SkeletalAnimatorComponent>();
            if ( pAnimator != nullptr )
                state = pAnimator->getCurrentStateName();
        }
        float3                 camera{};
        float32                cameraYaw = 0.0f;
        const GameObjectHandle player    = _player;
        pManager->forEachComponentOfType<CameraDirectorComponent>( [&]( CameraDirectorComponent* pCameraDirector )
        {
            const CameraComponent* pCamera = pCameraDirector->getTarget() == player ? pCameraDirector->getOwner()->getComponent<CameraComponent>() : nullptr;
            if ( pCamera == nullptr )
                return;
            const float4x4 world = pCamera->getWorldMatrix();
            camera               = world.getTranslation();
            const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
            cameraYaw            = MathUtil::atan2( forward._x, forward._z );
        } );
        float3  enemy{};
        float32 enemyHipsY = 0.0f;
        for ( const EnemyRecord& record : _listEnemy )
        {
            const GameObject*            pEnemy = pManager->resolveGameObject( record._object );
            const SkeletalMeshComponent* pUnit  = pEnemy != nullptr ? pEnemy->getComponent<SkeletalMeshComponent>() : nullptr;
            if ( pUnit == nullptr )
                continue;
            enemy = pUnit->getWorldPosition();
            float4x4 hips;
            if ( pUnit->findBoneModelTransform( hashed_string( "hips" ), hips ) )
                enemyHipsY = hips.getTranslation()._y;
            break;
        }
        const float3&                           feet = pPlayer->getFeetPosition();
        StringBuilder<constant::kMaxBuffer1024> row;
        row.appendFormat( "%#,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", _traceFrame, deltaTime, feet._x, feet._z, pPlayer->getLookYaw(), body._x, body._y, body._z, bodyYaw );
        for ( const float3& bone : arrBone )
            row.appendFormat( ",%.4f,%.4f,%.4f", bone._x, bone._y, bone._z );
        row.appendFormat( ",%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%#\n", camera._x, camera._y, camera._z, cameraYaw, enemy._x, enemy._z, enemyHipsY, state.c_str() );
        _motionTrace += row.c_str();
        ++_traceFrame;
    }

    GameObjectManager* ShooterDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
