#include "pch.h"

#include "Games/Shooter3D/ShooterGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStatsSubsystem.h"

#include "Games/Shooter3D/ShooterAutoAimControllerComponent.h"
#include "Games/Shooter3D/ShooterEffectComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Shooter3D/ShooterNetPawnComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "SHOT" );
            static constexpr uint32 kStateVersion = 3;
            /** @brief 이 게임이 아는 조우, 보상, 스폰 ID 입니다. 프로필이 다른 이름을 쓰면 시작할 때 알립니다. */
            static constexpr const utf8* kArrKnownEncounter[] = { "swarm", "elite", "ammo", "repair" };
            static constexpr const utf8* kArrKnownSpawn[]     = { "skeleton" };
            /** @brief 플레이어 통계 id 입니다(`game/shooter3d/data/shooter3d.stats.xml`). */
            static constexpr const utf8* kStatEnemiesKilled = "enemies_killed";
            static constexpr const utf8* kStatBestWave      = "best_wave";
            static constexpr const utf8* kEventEnemyDown    = "EnemyDown"; ///< shooter3d.audioevents.xml

            /** @brief 원(XZ) 가운데에서 가장 가까운 상자 위 점입니다. */
            static float3 closestPointXz( const float3& point, const float3& boxMin, const float3& boxMax )
            {
                return float3{ MathUtil::clamp( point._x, boxMin._x, boxMax._x ), point._y, MathUtil::clamp( point._z, boxMin._z, boxMax._z ) };
            }

            template <size_t Count>
            static bool isKnownID( const hashed_string& id, const utf8* const ( &arrKnown )[Count] )
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

} // namespace sw

namespace sw
{
    float3 ShooterArenaMath::resolveCircle( const vector<ShooterArenaBox>& listBox, const float3& position, float32 radius )
    {
        float3 resolved = position;
        // 두 번 처리한다. 모서리에서 두 상자에 동시에 닿아도 빠져나온다.
        for ( int32 iteration = 0; iteration < 2; ++iteration )
        {
            for ( const ShooterArenaBox& box : listBox )
            {
                if ( resolved._y >= box._max._y )
                    continue; // 상자 위다. 뛰어 오른 경우는 막지 않는다(높이가 낮은 상자).
                const float3  closest = ShooterGameModeComponentInternal::closestPointXz( resolved, box._min, box._max );
                float3        offset  = float3{ resolved._x - closest._x, 0.0f, resolved._z - closest._z };
                const float32 length  = offset.getLength();
                if ( length >= radius )
                    continue;
                if ( length < 1.0e-5f )
                {
                    // 중심이 상자 안이면 가장 얕은 면으로 민다.
                    const float32 arrPush[4] = { resolved._x - box._min._x, box._max._x - resolved._x, resolved._z - box._min._z, box._max._z - resolved._z };
                    int32         shallow    = 0;
                    for ( int32 sideIndex = 1; sideIndex < 4; ++sideIndex )
                    {
                        shallow = arrPush[sideIndex] < arrPush[shallow] ? sideIndex : shallow;
                    }
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

    ShooterGameModeComponent::ShooterGameModeComponent()
        : _effectPrefab{}
        , _tracerPrefab{}
        , _autoPlayPrefab{}
        , _listEnemyKind{}
        , _eliteEnemyKind{}
        , _player{}
        , _listSpawnPoint{}
        , _arenaHalfSize{ 20.0f }
        , _pacingProfile{}
        , _spawnTable{}
        , _pacingSeed{ 1 }
        , _effectPoolSize{ 32 }
        , _tracerPoolSize{ 24 }
        , _statusLogInterval{ 5.0f }
        , _nearEnemyDistance{ 7.0f }
        , _repairHealthPerScale{ 10.0f }
        , _listBox{}
        , _listEnemyView{}
        , _pProfile{}
        , _pTable{}
        , _director{}
        , _listDirectorEvent{}
        , _listEnemy{}
        , _effectPool{ string{}, 0 }
        , _tracerPool{ string{}, 0 }
        , _listPendingEnemy{}
        , _listPendingEffect{}
        , _listReplicaSample{}
        , _listPendingReplica{}
        , _tintCache{}
        , _autoPlayController{}
        , _playerEye{ 0.0f, 1.6f, -16.0f }
        , _playerFeet{ 0.0f, 0.0f, -16.0f }
        , _statusTimer{ 0.0f }
        , _pendingHeal{ 0.0f }
        , _spawnCursor{ 0 }
        , _killCount{ 0 }
        , _profileReload{}
        , _nextEnemyNetID{ ShooterNetPawnComponent::kPawnEntityID + 1u }
        , _appliedAutoPlay{ -1 }
        , _bAmmoPending{ SW_FALSE }
        , _bPacingReady{ SW_FALSE }
        , _bPlayerAlive{ SW_TRUE }
        , _bReplica{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ShooterGameModeComponent::~ShooterGameModeComponent() = default;

    bool ShooterGameModeComponent::startGame()
    {
        collectBoxes();
        const SceneNetLink* pLink = SceneNetLink::findFor( *this );
        _bReplica                 = pLink != nullptr && pLink->isBound() && pLink->get()._role == NetLinkRole::Client ? SW_TRUE : SW_FALSE;
        if ( _bReplica == SW_FALSE )
            startPacing();
        _killCount = 0;
        // 자동 플레이로 시작하면 플레이어 조종자가 폰을 먼저 조종하지 않게 한다(마우스를 잠갔다 풀지 않게 하려는 것이다). 첫 플러시가 자동 조준 AI 에게 넘긴다.
        PawnComponent* pPawn = findOn<PawnComponent>( _player );
        if ( pPawn != nullptr && isAutoPlayOn() )
            pPawn->setAutoPossess( PawnAutoPossess::None );
        return true;
    }

    void ShooterGameModeComponent::onGameStarted()
    {
        updatePlayerView();
    }

    void ShooterGameModeComponent::serializeState( StateIO& io )
    {
        uint32 killCount = _killCount;
        uint8  bPacing   = static_cast<uint8>( _bPacingReady );
        io.header( ShooterGameModeComponentInternal::kStateTag, ShooterGameModeComponentInternal::kStateVersion );
        io.value( killCount );
        io.value( bPacing );
        if ( io.check( bPacing != SW_TRUE || _bPacingReady != SW_FALSE ) == false )
            return;
        // 감독은 단계 · 웨이브 · 예산 · 산 스폰까지 잇는다. 적 모습은 복원 뒤 걷히고(`onViewsDespawned`) 감독 예산으로 섰던 것만 같은 스폰 id 로 다시 선다.
        if ( bPacing == SW_TRUE )
        {
            io.valueAtomic( _director );
        }
        io.end();
        if ( io.isReading() && io.isOk() )
        {
            _killCount = killCount;
        }
    }

    void ShooterGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
        {
            SW_LOG_INFO_ID( "ShooterGameMode.ArenaStateRestoredKills", "[Shooter] arena state restored - %# kills, wave %#", _killCount, getWave() );
        }
        else
        {
            // 감독은 읽었는데 끝 바이트가 남은 경우도 여기로 온다. 새 판으로 되돌린다.
            SW_LOG_WARNING_ID( "ShooterGameMode.SavedArenaStateDoes", "[Shooter] the saved arena state does not match this build - starting a new round" );
            _killCount = 0;
            if ( _bPacingReady == SW_TRUE )
                _director.restart();
        }
    }

    void ShooterGameModeComponent::applyReloadedProfile()
    {
        if ( _profileReload.takeChanged() == false )
            return;
        shared_ptr<const AIDirectorProfile> pReloaded = SharedAssetCache<AIDirectorProfile>::acquire( _pacingProfile );
        if ( pReloaded == nullptr || pReloaded == _pProfile )
            return; // 다른 프로필이 다시 읽혔거나 읽지 못했다(읽기가 오류를 남겼다) — 쥔 프로필로 계속 돈다
        // 판을 이어 간다 — 국면 · 풀 번호 · 시계 · 예산을 새 프로필로 옮긴다. 구조가 바뀌었으면 옛 프로필로 이 판을 끝까지 돌고 다음 시작에서 새 것을 쓴다.
        if ( _director.rebindProfile( pReloaded.get() ) )
        {
            _pProfile = std::move( pReloaded );
            SW_LOG_INFO_ID( "ShooterGameMode.PacingProfileReloaded", "[Shooter] pacing profile '%#' reloaded - the run continues in phase '%#'", _pacingProfile.c_str(), _director.getPhase().c_str() );
            return;
        }
        SW_LOG_WARNING_ID( "ShooterGameMode.PacingProfileShapeChanged", "[Shooter] pacing profile '%#' changed its phases or pools - this run keeps the previous profile, the next start uses the new one",
                           _pacingProfile.c_str() );
    }

    void ShooterGameModeComponent::updateGame( float32 deltaTime )
    {
        const float32 step = MathUtil::min( deltaTime, 0.1f );
        updatePlayerView();
        if ( step > 0.0f && _bReplica == SW_TRUE )
        {
            updateReplicaEnemies();
            // 복제 쪽도 자동 플레이 조종자를 세운다 — 그 의도가 서버로 가는 입력이다.
            GameObjectManager* pManager = getObjectManager();
            if ( pManager != nullptr && _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 ) )
                syncAutoPlayPossession( *pManager );
            return;
        }
        if ( step > 0.0f )
        {
            updateEnemies();
            if ( _bPacingReady == SW_TRUE )
            {
                applyReloadedProfile();
                _director.update( step );
                applyDirectorEvents();
            }
            logStatus( step );
        }
    }

    bool ShooterGameModeComponent::hasPendingSpawn() const
    {
        const bool bAutoPlayChanged = _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 );
        return _listPendingEnemy.empty() == false || _listPendingReplica.empty() == false || _listPendingEffect.empty() == false || _bAmmoPending == SW_TRUE || _pendingHeal > 0.0f || bAutoPlayChanged;
    }

    void ShooterGameModeComponent::onViewsDespawned()
    {
        // 자동 플레이 AI 오브젝트도 정리됐다. 다시 스폰할 때 빙의를 다시 맞춘다.
        _autoPlayController = GameObjectHandle{};
        _appliedAutoPlay    = -1;
        _listEnemy.clear();
        // 풀의 인스턴스는 풀이 쥔다(스폰 목록에 없다) — 씬 오브젝트로 스냅샷에 실리지 않게 함께 걷는다.
        if ( GameObjectManager* pManager = getObjectManager() )
        {
            _effectPool.destroyAll( *pManager );
            _tracerPool.destroyAll( *pManager );
        }
        _listEnemyView.clear();
        _listPendingEnemy.clear();
        _listPendingEffect.clear();
        // 감독 예산으로 스폰한 적은 감독이 아직 살아 있는 것으로 센다. 같은 스폰 ID 로 다시 스폰한다(체력은 새로 채운다). 무리와 정예는 예산 밖이라 정리된 채 끝난다.
        if ( _bPacingReady == SW_TRUE )
        {
            vector<uint32> listSpawnID;
            _director.getSpawnDirector().collectAliveSpawnIDs( listSpawnID );
            for ( const uint32 spawnID : listSpawnID )
            {
                requestEnemies( 1, 1.0f, spawnID, false );
            }
        }
    }

    void ShooterGameModeComponent::restartRound()
    {
        clearEnemies();
        _killCount   = 0;
        _pendingHeal = 0.0f;
        if ( _bPacingReady == SW_TRUE )
            _director.restart();
        _listDirectorEvent.clear();
    }

    void ShooterGameModeComponent::clearEnemies()
    {
        GameObjectManager* pManager = getObjectManager();
        for ( EnemyRecord& enemy : _listEnemy )
        {
            if ( pManager != nullptr )
                destroySpawned( *pManager, enemy._object );
        }
        _listEnemy.clear();
        _listEnemyView.clear();
        _listPendingEnemy.clear();
    }

    void ShooterGameModeComponent::reportPlayerDamage( float32 amount )
    {
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "damageTaken" ), amount );
    }

    void ShooterGameModeComponent::spawnEffect( const float3& position, float32 size, const float4& color, float32 lifetime )
    {
        GameObjectManager* pManager = getObjectManager();
        MeshComponent*     pMesh    = pManager != nullptr ? acquirePooledMesh( *pManager, _effectPool, _effectPoolSize, "ShotEffect", lifetime ) : nullptr;
        if ( pMesh == nullptr )
            return;
        pMesh->setLocalPosition( position );
        pMesh->setLocalRotation( float3{ 0.0f, 0.0f, 0.0f } );
        pMesh->setLocalScale( float3{ size } );
        _tintCache.apply( *pMesh, color );
        pMesh->setVisible( true );
    }

    void ShooterGameModeComponent::spawnTracer( const float3& from, const float3& to, float32 width, const float4& color, float32 lifetime )
    {
        const float3  span   = to - from;
        const float32 length = span.getLength();
        if ( length < 1.0e-3f )
            return;
        GameObjectManager* pManager = getObjectManager();
        MeshComponent*     pMesh    = pManager != nullptr ? acquirePooledMesh( *pManager, _tracerPool, _tracerPoolSize, "Tracer", lifetime ) : nullptr;
        if ( pMesh == nullptr )
            return;
        // 단위 상자를 선분 가운데에 두고 진행 방향(요 · 피치)으로 돌려 Z 로 늘린다.
        const float3  direction = span * ( 1.0f / length );
        const float32 yaw       = MathUtil::atan2( direction._x, direction._z );
        const float32 pitch     = -MathUtil::asin( MathUtil::clamp( direction._y, -1.0f, 1.0f ) );
        pMesh->setLocalPosition( from + span * 0.5f );
        pMesh->setLocalRotation( float3{ pitch, yaw, 0.0f } );
        pMesh->setLocalScale( float3{ width, width, length } );
        _tintCache.apply( *pMesh, color );
        pMesh->setVisible( true );
    }

    // ------------------------------------------------------------------------------
    // 배치 · 스폰
    // ------------------------------------------------------------------------------
    void ShooterGameModeComponent::collectBoxes()
    {
        _listBox.clear();
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        // 씬에 놓인 벽과 엄폐물이다. 에디터에서 옮기면 충돌도 따라온다.
        vector<ShooterArenaBox>& listBox = _listBox;
        pManager->forEachComponentOfType<ShooterBlockerComponent>( [&listBox]( ShooterBlockerComponent* pBlocker )
        { listBox.push_back( pBlocker->computeBox() ); } );
    }

    void ShooterGameModeComponent::startPacing()
    {
        using Internal = ShooterGameModeComponentInternal;
        _bPacingReady  = SW_FALSE;
        // 적 종류(프리팹 + 프리셋)는 씬 데이터가 정본이다(C++ 에 기본값이 없다). 비었거나 외형 표에 없는 프리셋이면 이유를 남기고 적을 내지 않는다.
        const AppearanceDatabase* pAppearance = game::findSubsystem<AppearanceDatabase>();
        const bool                bEnemiesSet = _listEnemyKind.empty() == false && _eliteEnemyKind._prefab.empty() == false;
        if ( pAppearance == nullptr || bEnemiesSet == false )
        {
            SW_LOG_ERROR_ID( "ShooterGameMode.EnemyPresetsNotSet", "[Shooter] enemy kinds are not set (or no appearance database) - no enemies will come" );
            return;
        }
        bool bPresetsKnown = pAppearance->getPresets().findPreset( hashed_string( _eliteEnemyKind._preset ) ) != nullptr;
        if ( bPresetsKnown == false )
            SW_LOG_ERROR_ID( "ShooterGameMode.ElitePresetNotAppearance", "[Shooter] elite preset '%#' is not in the appearance presets", _eliteEnemyKind._preset.c_str() );
        for ( const ShooterEnemyKind& kind : _listEnemyKind )
        {
            if ( pAppearance->getPresets().findPreset( hashed_string( kind._preset ) ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "ShooterGameMode.EnemyPresetNotAppearance", "[Shooter] enemy preset '%#' is not in the appearance presets", kind._preset.c_str() );
            bPresetsKnown = false;
        }
        if ( bPresetsKnown == false )
            return;
        _profileReload.markSeen();
        if ( acquireRequired( _pacingProfile, _pProfile ) == false )
            return;
        if ( acquireRequired( _spawnTable, _pTable ) == false )
            return;
        // 감독은 ID 만 낸다. 이 게임이 모르는 ID 는 아무 일도 하지 않으므로 데이터 오타를 여기서 알린다.
        vector<hashed_string> listID;
        _pProfile->collectEncounterIDs( listID );
        for ( const hashed_string& id : listID )
        {
            if ( Internal::isKnownID( id, Internal::kArrKnownEncounter ) == false )
                SW_LOG_ERROR_ID( "ShooterGameMode.PacingProfileNamesEncounter", "[Shooter] pacing profile '%#' names encounter '%#' this game does not know", _pacingProfile.c_str(), id.c_str() );
        }
        for ( const SpawnEntryDef& entry : _pTable->getEntries() )
        {
            if ( Internal::isKnownID( entry._id, Internal::kArrKnownSpawn ) == false )
                SW_LOG_ERROR_ID( "ShooterGameMode.SpawnTableNamesGame", "[Shooter] spawn table '%#' names '%#' this game does not know", _spawnTable.c_str(), entry._id.c_str() );
        }
        _director.initialize( _pProfile.get(), _pTable.get(), static_cast<uint32>( _pacingSeed ) );
        _bPacingReady = SW_TRUE;
        applyDirectorEvents(); // 시작 단계 사건
    }

    void ShooterGameModeComponent::requestEnemies( int32 count, float32 healthScale, uint32 spawnID, bool bElite )
    {
        // 웨이브(감독 순환 + 1)마다 체력과 속도가 오른다. 감독이 정하는 것은 언제 몇 기를 낼지이고, 적 한 기의 세기는 게임 규칙이다.
        const float32 wave   = static_cast<float32>( getWave() );
        const float32 health = ( 30.0f + 10.0f * wave ) * MathUtil::max( 0.1f, healthScale );
        const float32 speed  = MathUtil::min( 5.5f, 2.2f + 0.3f * wave );
        for ( int32 enemyIndex = 0; enemyIndex < count; ++enemyIndex )
        {
            EnemyRequest request;
            request._health  = health;
            request._speed   = speed;
            request._slot    = _spawnCursor++;
            request._spawnID = spawnID;
            request._bElite  = bElite ? SW_TRUE : SW_FALSE;
            _listPendingEnemy.push_back( request );
        }
    }

    void ShooterGameModeComponent::applyDirectorEvents()
    {
        _listDirectorEvent.clear();
        _director.pollEvents( _listDirectorEvent );
        for ( const AIDirectorEvent& event : _listDirectorEvent )
        {
            switch ( event._kind )
            {
                case AIDirectorEventKind::PhaseChanged:
                {
                    // 쌓기로 돌아왔다 = 새 웨이브. 진행 사건(동의가 없으면 텔레메트리가 버린다).
                    TelemetryService* pTelemetry = game::findSubsystem<TelemetryService>();
                    const bool        bNewWave   = event._source.empty() == false && event._id == _pProfile->getPhases()[static_cast<size_t>( _pProfile->getStartPhaseIndex().value() )]._id;
                    if ( pTelemetry != nullptr && bNewWave )
                    {
                        TelemetryEvent waveReached( "progression.waveReached" );
                        waveReached.setInt( "wave", getWave() ).setInt( "kills", _killCount ).setFloat( "seconds", static_cast<float64>( _director.getTime() ) );
                        (void)pTelemetry->record( waveReached );
                    }
                    PlayerStatsSubsystem* pPlayerStats = game::findSubsystem<PlayerStatsSubsystem>();
                    if ( pPlayerStats != nullptr && pPlayerStats->isActive() && bNewWave )
                        (void)pPlayerStats->getStats().submit( hashed_string( ShooterGameModeComponentInternal::kStatBestWave ), static_cast<float64>( getWave() ) ); // 기록이 좋아질 때만 바뀐다
                    SW_LOG_INFO_ID( "ShooterGameMode.PacingWaveIntensity", "[Shooter] pacing '%#' -> '%#' (wave %#, intensity %.2f)", event._source.empty() ? "start" : event._source.c_str(), event._id.c_str(), getWave(),
                                    event._intensity );
                    break;
                }
                case AIDirectorEventKind::Spawned:
                {
                    requestEnemies( 1, 1.0f, event._spawnID, false );
                    break;
                }
                case AIDirectorEventKind::Encounter:
                {
                    requestEnemies( event._count, event._scale, 0, event._id == hashed_string( "elite" ) );
                    SW_LOG_INFO_ID( "ShooterGameMode.SkeletonsXHealthWave", "[Shooter] %# - %# skeletons (x%.1f health) on wave %#", event._id.c_str(), event._count, event._scale, getWave() );
                    break;
                }
                case AIDirectorEventKind::Reward:
                {
                    if ( event._id == hashed_string( "ammo" ) )
                        _bAmmoPending = SW_TRUE;
                    else if ( event._id == hashed_string( "repair" ) )
                        _pendingHeal += event._scale * _repairHealthPerScale;
                    break;
                }
                case AIDirectorEventKind::Despawned:
                {
                    break;
                }
            }
        }
    }

    void ShooterGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
            spawnEffectPools( manager );
        for ( const EnemyRequest& request : _listPendingEnemy )
        {
            spawnEnemy( manager, request );
        }
        _listPendingEnemy.clear();
        for ( const ShooterReplicatedEnemy& replica : _listPendingReplica )
        {
            spawnReplicaEnemy( manager, replica );
        }
        _listPendingReplica.clear();
        for ( const EffectRequest& effect : _listPendingEffect )
        {
            spawnEffect( effect._position, effect._size, effect._color, effect._lifetime );
        }
        _listPendingEffect.clear();
        if ( _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 ) )
            syncAutoPlayPossession( manager );
        const bool bPlayerPending = _bAmmoPending == SW_TRUE || _pendingHeal > 0.0f;
        if ( bPlayerPending )
        {
            ShooterPlayerComponent* pPlayer = findOn<ShooterPlayerComponent>( _player );
            if ( pPlayer != nullptr && _bAmmoPending == SW_TRUE )
                pPlayer->addWaveAmmo();
            if ( pPlayer != nullptr && _pendingHeal > 0.0f )
                pPlayer->restoreHealth( _pendingHeal );
            _bAmmoPending = SW_FALSE;
            _pendingHeal  = 0.0f;
        }
    }

    void ShooterGameModeComponent::syncAutoPlayPossession( GameObjectManager& manager )
    {
        const bool     bAutoPlay     = isAutoPlayOn();
        GameObject*    pPlayerObject = manager.resolveGameObject( _player );
        PawnComponent* pPawn         = pPlayerObject != nullptr ? pPlayerObject->getComponent<PawnComponent>() : nullptr;
        _appliedAutoPlay             = bAutoPlay ? 1 : 0; // 폰이 없어도 맞춘 것으로 친다. 매 틱 플러시를 잡지 않게 하기 위해서다.
        if ( pPawn == nullptr )
            return;
        // 네트 서버의 폰은 원격 조종자가 쥔다 — 자동 플레이는 클라이언트 쪽 조종자가 하고, 그 의도가 선으로 온다.
        const ShooterNetPawnComponent* pNetPawn = pPlayerObject->getComponent<ShooterNetPawnComponent>();
        if ( pNetPawn != nullptr && pNetPawn->isServer() )
            return;
        GameObject* pAIObject = manager.resolveGameObject( _autoPlayController );
        if ( bAutoPlay )
        {
            if ( pAIObject == nullptr )
            {
                // 조종자와 그 행동 트리는 프리팹 데이터다(스켈레톤 적의 `enemy_ai.prefab.xml` 과 같은 길). 스폰한 것은 정리할 목록에 든다.
                pAIObject = spawnPrefab( manager, _autoPlayPrefab.path(), "ShooterAutoPlay" );
                if ( pAIObject == nullptr || pAIObject->getComponent<ShooterAutoAimControllerComponent>() == nullptr )
                {
                    SW_LOG_ERROR_ID( "ShooterGameMode.AutoPlayPrefabInvalid", "Auto play controller prefab '%#' did not spawn a ShooterAutoAimControllerComponent", _autoPlayPrefab.path() );
                    return;
                }
                _autoPlayController = pAIObject->getHandle();
            }
            ShooterAutoAimControllerComponent* pAI = pAIObject->getComponent<ShooterAutoAimControllerComponent>();
            if ( pAI != nullptr && pAI->getPawn() != pPawn->getHandle() )
            {
                pAI->possess( *pPawn );
                SW_LOG_INFO_ID( "ShooterGameMode.AutoPlayTookPlayer", "[Shooter] auto play took the player" );
            }
            return;
        }
        // 끄면 플레이어 조종자에게 돌려주고 AI 를 정리한다. 이미 다른 조종자(플레이어)가 조종하고 있으면 그대로 둔다.
        if ( ControlSystem::returnPawnToPlayer( manager, *pPawn, _autoPlayController ) )
            SW_LOG_INFO_ID( "Shooter.PlayerTookBodyBack", "[Shooter] the player took the body back" );
        if ( pAIObject != nullptr )
            destroySpawned( manager, _autoPlayController );
    }

    void ShooterGameModeComponent::spawnEffectPools( GameObjectManager& manager )
    {
        // 탄착과 섬광 구, 탄도선 상자를 미리 스폰해 꺼 둔다(오브젝트를 매 발 만들고 지우지 않는다).
        _effectPool = PrefabPool( _effectPrefab.path(), static_cast<uint32>( _effectPoolSize ) );
        _tracerPool = PrefabPool( _tracerPrefab.path(), static_cast<uint32>( _tracerPoolSize ) );
        (void)_effectPool.prewarm( manager, static_cast<uint32>( _effectPoolSize ), "ShotEffect" );
        (void)_tracerPool.prewarm( manager, static_cast<uint32>( _tracerPoolSize ), "Tracer" );
    }

    MeshComponent* ShooterGameModeComponent::acquirePooledMesh( GameObjectManager& manager, PrefabPool& inoutPool, int32 poolSize, const utf8* pName, float32 lifetime )
    {
        // 수명이 다해 스스로 숨은 것을 돌려받고 꺼낸다. 모두 쓰고 있으면 가장 먼저 꺼낸 것을 다시 쓴다 — 연사가 풀보다 빨라도 새 탄도선이 보인다.
        inoutPool.releaseBusyIf( manager, []( const GameObject& object )
        {
            const ShooterEffectComponent* pEffect = object.getComponent<ShooterEffectComponent>();
            return pEffect != nullptr && pEffect->isIdle();
        } );
        GameObject*             pObject = inoutPool.acquireRecycling( manager, static_cast<uint32>( poolSize ), pName );
        ShooterEffectComponent* pEffect = pObject != nullptr ? pObject->getComponent<ShooterEffectComponent>() : nullptr;
        MeshComponent*          pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pEffect == nullptr || pMesh == nullptr )
            return nullptr;
        pEffect->activate( lifetime );
        return pMesh;
    }

    void ShooterGameModeComponent::spawnEnemy( GameObjectManager& manager, const EnemyRequest& request )
    {
        // 종류는 정예면 정예, 나머지는 목록을 차례로 쓴다. 시드는 스폰 순번이라 같은 판은 같은 얼굴이 나온다.
        const uint8                   kindIndex   = request._bElite == SW_TRUE ? kEliteKindIndex : static_cast<uint8>( request._slot % static_cast<uint32>( _listEnemyKind.size() ) );
        const ShooterEnemyKind&       kind        = kindIndex == kEliteKindIndex ? _eliteEnemyKind : _listEnemyKind[kindIndex];
        const uint32                  seed        = static_cast<uint32>( _pacingSeed ) * 7919u + request._slot;
        GameObject*                   pObject     = spawnPrefab( manager, kind._prefab, kind._preset.c_str() ); // 이름은 종류의 외형 프리셋(QuadShell · SkeletonMinion) - 시나리오 · 로그가 종류로 찾는다
        ShooterEnemyComponent*        pEnemy      = pObject != nullptr ? pObject->getComponent<ShooterEnemyComponent>() : nullptr;
        CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pEnemy == nullptr )
        {
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            if ( request._spawnID != 0 && _bPacingReady == SW_TRUE )
                (void)_director.notifyDespawned( request._spawnID ); // 스폰하지 못한 적의 예산 슬롯을 돌려준다.
            return;
        }
        if ( pAppearance != nullptr )
            pAppearance->setPreset( hashed_string( kind._preset ), seed );
        // 스폰 위치를 차례로 돈다. 한 바퀴를 돌 때마다 옆으로 1.2m 비켜서 같은 위치에 겹쳐 스폰하지 않게 한다.
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
        if ( pEnemy->launch( getOwner()->getHandle(), position, yaw, request._health, request._speed ) == false )
        {
            // 체력 설정이 거절됐다(이유는 HealthStateMachine 가 남겼다) — 올려서 대신 세우지 않고 정리한다.
            manager.destroyGameObject( pObject );
            if ( request._spawnID != 0 && _bPacingReady == SW_TRUE )
                (void)_director.notifyDespawned( request._spawnID );
            return;
        }
        EnemyRecord record;
        record._object         = pObject->getHandle();
        record._spawnID        = request._spawnID;
        record._netEntityID    = _nextEnemyNetID++;
        record._appearanceSeed = seed;
        record._kindIndex      = kindIndex;
        pEnemy->setNetEntityID( record._netEntityID );
        _listEnemy.push_back( record );
    }

    void ShooterGameModeComponent::spawnReplicaEnemy( GameObjectManager& manager, const ShooterReplicatedEnemy& replica )
    {
        const bool bKnownKind = replica._kindIndex == kEliteKindIndex || replica._kindIndex < _listEnemyKind.size();
        if ( bKnownKind == false )
        {
            SW_LOG_ERROR_ID( "ShooterGameMode.ReplicaEnemyKindUnknown", "[Shooter] the server sent enemy kind %# but this scene has %# kinds - the enemy is not drawn", static_cast<int32>( replica._kindIndex ),
                             static_cast<int32>( _listEnemyKind.size() ) );
            return;
        }
        const ShooterEnemyKind&       kind        = replica._kindIndex == kEliteKindIndex ? _eliteEnemyKind : _listEnemyKind[replica._kindIndex];
        GameObject*                   pObject     = spawnPrefab( manager, kind._prefab, kind._preset.c_str() ); // 이름은 서버 쪽과 같은 종류의 프리셋
        ShooterEnemyComponent*        pEnemy      = pObject != nullptr ? pObject->getComponent<ShooterEnemyComponent>() : nullptr;
        CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pEnemy == nullptr )
        {
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return;
        }
        if ( pAppearance != nullptr )
            pAppearance->setPreset( hashed_string( kind._preset ), replica._appearanceSeed );
        pEnemy->launchReplica( getOwner()->getHandle(), replica._position, replica._yaw );
        pEnemy->setNetEntityID( replica._netEntityID );
        EnemyRecord record;
        record._object         = pObject->getHandle();
        record._netEntityID    = replica._netEntityID;
        record._appearanceSeed = replica._appearanceSeed;
        record._kindIndex      = replica._kindIndex;
        _listEnemy.push_back( record );
    }

    // ------------------------------------------------------------------------------
    // 갱신(PrePhysics). 플레이어와 적이 쓰지 않는 그룹이다.
    // ------------------------------------------------------------------------------
    void ShooterGameModeComponent::updateReplicaEnemies()
    {
        GameObjectManager*             pManager = getObjectManager();
        const ShooterNetPawnComponent* pNetPawn = findOn<ShooterNetPawnComponent>( _player );
        _listEnemyView.clear();
        _listReplicaSample.clear();
        if ( pNetPawn == nullptr )
            return;
        pNetPawn->collectReplicatedEnemies( _listReplicaSample );
        for ( size_t enemyIndex = 0; enemyIndex < _listEnemy.size(); )
        {
            const EnemyRecord&            record  = _listEnemy[enemyIndex];
            GameObject*                   pObject = pManager->resolveGameObject( record._object );
            ShooterEnemyComponent*        pEnemy  = pObject != nullptr ? pObject->getComponent<ShooterEnemyComponent>() : nullptr;
            const ShooterReplicatedEnemy* pSample = VectorUtil::findBy<&ShooterReplicatedEnemy::_netEntityID>( _listReplicaSample, record._netEntityID );
            if ( pEnemy == nullptr || pSample == nullptr )
            {
                // 스냅샷에서 빠졌다(서버에서 쓰러졌거나 관심 영역 밖) — 복제 모습은 시체를 남기지 않는다.
                CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
                if ( pAppearance != nullptr )
                    pAppearance->destroyOwnerStowingParts();
                else if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
                _listEnemy[enemyIndex] = _listEnemy.back();
                _listEnemy.pop_back();
                continue;
            }
            ++enemyIndex;
            pEnemy->setReplicatedPose( pSample->_position, pSample->_yaw, pSample->_phase );
            ShooterEnemyView view;
            view._object         = record._object;
            view._position       = pSample->_position;
            view._radius         = pEnemy->getRadius();
            view._height         = pEnemy->getHeight();
            view._netEntityID    = record._netEntityID;
            view._appearanceSeed = record._appearanceSeed;
            view._yaw            = pSample->_yaw;
            view._phase          = pSample->_phase;
            view._kindIndex      = record._kindIndex;
            _listEnemyView.push_back( view );
        }
        for ( const ShooterReplicatedEnemy& sample : _listReplicaSample )
        {
            const bool bSpawned = VectorUtil::findBy<&EnemyRecord::_netEntityID>( _listEnemy, sample._netEntityID ) != nullptr;
            const bool bPending = VectorUtil::findBy<&ShooterReplicatedEnemy::_netEntityID>( _listPendingReplica, sample._netEntityID ) != nullptr;
            if ( bSpawned == false && bPending == false )
                _listPendingReplica.push_back( sample );
        }
    }

    void ShooterGameModeComponent::updateEnemies()
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
            // 쓰러진 순간 한 번만 처치 수, 신호, 효과음을 내고 감독의 예산 슬롯을 돌려준다. 시체는 시체 시간 동안 남는다.
            const bool bJustDied = pEnemy != nullptr && pEnemy->isDead() && record._bCounted == SW_FALSE;
            if ( bJustDied )
            {
                record._bCounted = SW_TRUE;
                ++_killCount;
                PlayerStatsSubsystem* pPlayerStats = game::findSubsystem<PlayerStatsSubsystem>();
                if ( pPlayerStats != nullptr && pPlayerStats->isActive() )
                    (void)pPlayerStats->getStats().increment( hashed_string( ShooterGameModeComponentInternal::kStatEnemiesKilled ) ); // 업적(`shooter3d.stats.xml`)이 이 통계를 본다
                if ( _bPacingReady == SW_TRUE )
                {
                    (void)_director.getBuiltinIntensityModel().addSignal( hashed_string( "enemyKilled" ), 1.0f );
                    if ( record._spawnID != 0 )
                        (void)_director.notifyDespawned( record._spawnID );
                }
                getSoundQueue().queueEventAt( ShooterGameModeComponentInternal::kEventEnemyDown, pEnemy->getPosition() );
            }
            const bool bGone = pEnemy == nullptr || pEnemy->isRemovable();
            if ( bGone )
            {
                const bool bShouldNotifyDespawn = pEnemy == nullptr && _bPacingReady == SW_TRUE && record._spawnID != 0 && record._bCounted == SW_FALSE;
                if ( bShouldNotifyDespawn )
                    (void)_director.notifyDespawned( record._spawnID );
                // 외형이 있으면 투구 · 망토를 씬의 부품 풀에 쉬게 한 뒤 지운다(다음 적이 꺼내 쓴다). 떨어진 무기는 풀이 받지 않아 지워진다.
                CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
                if ( pAppearance != nullptr )
                    pAppearance->destroyOwnerStowingParts();
                else if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
                _listEnemy[enemyIndex] = _listEnemy.back();
                _listEnemy.pop_back();
                continue;
            }
            ++enemyIndex;
            if ( pEnemy->isAlive() == false )
                continue;
            ShooterEnemyView view;
            view._object         = record._object;
            view._position       = pEnemy->getPosition();
            view._radius         = pEnemy->getRadius();
            view._height         = pEnemy->getHeight();
            view._netEntityID    = record._netEntityID;
            view._appearanceSeed = record._appearanceSeed;
            view._yaw            = pEnemy->getYaw();
            view._phase          = pEnemy->getPhase();
            view._kindIndex      = record._kindIndex;
            _listEnemyView.push_back( view );
            const float3 toPlayer = view._position - _playerFeet;
            nearCount += toPlayer.getLength() < _nearEnemyDistance ? 1 : 0;
        }
        if ( _bPacingReady == SW_TRUE )
            (void)_director.getBuiltinIntensityModel().setSignal( hashed_string( "enemiesNear" ), static_cast<float32>( nearCount ) );
    }

    void ShooterGameModeComponent::updatePlayerView()
    {
        const ShooterPlayerComponent* pPlayer = findOn<ShooterPlayerComponent>( _player );
        if ( pPlayer == nullptr )
            return;
        _playerEye    = pPlayer->getEyePosition();
        _playerFeet   = pPlayer->getFeetPosition();
        _bPlayerAlive = pPlayer->isAlive() ? SW_TRUE : SW_FALSE;
        if ( _bPacingReady == SW_TRUE )
        {
            static const hashed_string s_lowAmmoSignal( "lowAmmo" );
            (void)_director.getBuiltinIntensityModel().setSignal( s_lowAmmoSignal, pPlayer->computeAmmoShortage() );
        }
    }

    void ShooterGameModeComponent::logStatus( float32 deltaTime )
    {
        _statusTimer += deltaTime;
        if ( _statusTimer < _statusLogInterval )
            return;
        _statusTimer                          = 0.0f;
        const ShooterPlayerComponent* pPlayer = findOn<ShooterPlayerComponent>( _player );
        if ( pPlayer == nullptr )
            return;
        [[maybe_unused]] const WeaponState& weapon  = pPlayer->getCurrentWeapon();
        [[maybe_unused]] const uint32       percent = pPlayer->getShotCount() > 0u ? pPlayer->getHitCount() * 100u / pPlayer->getShotCount() : 0u;
        SW_LOG_INFO_ID( "ShooterGameMode.WaveIntensityKillsHP", "[Shooter] wave %# (%#, intensity %.2f) · kills %# · HP %# · %# %#/%# · skeletons %# · accuracy %#%%", getWave(), _director.getPhase().c_str(),
                        _director.getIntensity(), _killCount, static_cast<int32>( pPlayer->getHealth() ), weapon.getDef()._name.c_str(), weapon.getMagazineAmmo(),
                        weapon.getReserveAmmo(), static_cast<uint32>( _listEnemyView.size() ), percent );
    }

} // namespace sw

namespace sw
{
    namespace
    {
        struct ShooterEffectWatchInternal
        {
            /** @brief 감시 값 `Shooter3D.EffectCreatedCount` 입니다. 판의 효과 풀이 지금까지 새로 만든 인스턴스 수입니다. */
            [[nodiscard]] static bool readEffectCreatedCount( const GameObjectManager* pManager, float64& outValue )
            {
                bool bFound = false;
                if ( pManager == nullptr )
                    return false;
                pManager->forEachComponentOfType<ShooterGameModeComponent>( [&bFound, &outValue]( ShooterGameModeComponent* pGameMode )
                {
                    if ( bFound )
                        return;
                    bFound   = true;
                    outValue = static_cast<float64>( pGameMode->getEffectCreatedCount() );
                } );
                return bFound;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( shooterEffectCreatedCount, "Shooter3D.EffectCreatedCount", "Instances the effect pools of the shooter game mode created so far (reuse does not count)",
                         &ShooterEffectWatchInternal::readEffectCreatedCount );
} // namespace sw
