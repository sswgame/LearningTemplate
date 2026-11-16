#include "pch.h"

#include "Games/Velmora/VelmoraGameModeComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/Velmora/VelmoraRegionCatalog.h"

namespace sw
{
    namespace
    {
        struct VelmoraGameModeComponentInternal
        {
            static constexpr const utf8* kAttackAction = "Velmora.Attack";
            static constexpr const utf8* kDashAction   = "Velmora.Dash";
            static constexpr const utf8* kUseAction    = "Velmora.Use";

            /** @brief 축 입력이 이보다 작으면 바라보는 쪽을 바꾸지 않는다(패드 스틱의 떨림). */
            static constexpr float32 kFacingDeadZone = 0.2f;
            /** @brief 디버그 그리기의 높이(m) — 땅 위에 뜬 구 · 룸이 같은 높이다(액션 룸 시연과 같다). */
            static constexpr float32 kDrawHeight = 0.5f;

            /** @brief 첫 열린 디렉터의 지금 지역 이름입니다(감시 값 `Velmora.Region`). */
            [[nodiscard]] static bool readRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue, bool bRespawn )
            {
                const VelmoraGameModeComponent* pDirector = nullptr;
                if ( query._pManager != nullptr && query._pManager->getComponentRegistry().getAll<VelmoraGameModeComponent>().empty() == false )
                    pDirector = query._pManager->getComponentRegistry().getAll<VelmoraGameModeComponent>()[0];
                if ( pDirector == nullptr )
                {
                    outValue.setText( "no Velmora director in the scene" );
                    return false;
                }
                outValue.setText( string_view{ ( bRespawn ? pDirector->getRespawnRegion() : pDirector->getCurrentRegion() ).c_str() } );
                return true;
            }
            [[nodiscard]] static bool readCurrentRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readRegion( query, outValue, false ); }
            [[nodiscard]] static bool readRespawnRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readRegion( query, outValue, true ); }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( velmoraPhase, "Velmora.Phase", "Velmora phase (0 explore, 1 hall, 2 boss wait, 3 boss)", VelmoraGameModeComponent, &VelmoraGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( velmoraPlayerX, "Velmora.PlayerX", "Velmora player plane X (m)", VelmoraGameModeComponent, &VelmoraGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( velmoraPlayerZ, "Velmora.PlayerZ", "Velmora player plane Z (m)", VelmoraGameModeComponent, &VelmoraGameModeComponent::getPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( velmoraHealth, "Velmora.Health", "Velmora player health in quarter hearts", VelmoraGameModeComponent, &VelmoraGameModeComponent::getHealthQuarters );
    SW_AUTOMATION_GETTER_WATCH( velmoraHearts, "Velmora.Hearts", "Velmora player heart containers", VelmoraGameModeComponent, &VelmoraGameModeComponent::getHeartCount );
    SW_AUTOMATION_GETTER_WATCH( velmoraAlive, "Velmora.Alive", "Living enemies in the Velmora region fight", VelmoraGameModeComponent, &VelmoraGameModeComponent::getAliveEnemyCount );
    SW_AUTOMATION_GETTER_WATCH( velmoraFalls, "Velmora.Falls", "Times the Velmora player fell this game", VelmoraGameModeComponent, &VelmoraGameModeComponent::getFallCount );
    SW_AUTOMATION_QUERY_WATCH( velmoraRegion, "Velmora.Region", "Region id the Velmora player stands in", &VelmoraGameModeComponentInternal::readCurrentRegion, false, false );
    SW_AUTOMATION_QUERY_WATCH( velmoraRespawn, "Velmora.Respawn", "Region id the Velmora player rises in after a fall", &VelmoraGameModeComponentInternal::readRespawnRegion, false, false );

    VelmoraGameModeComponent::VelmoraGameModeComponent()
        : _startRegion{}
        , _moveSpeed{ 6.0f }
        , _useRadius{ 1.5f }
        , _vitalsSettings{}
        , _bossDelaySeconds{ 1.0f }
        , _campaign{}
        , _room{}
        , _bossDelay{}
        , _playerPosition{ 0.0f, 0.0f }
        , _facing{ FacingDir::Right }
        , _currentRegion{}
        , _encounterRegion{}
        , _pendingArea{}
        , _pendingBoss{}
        , _pendingCheckpoint{}
        , _listQuestEvent{}
        , _listWalletEvent{}
        , _listHealthEvent{}
        , _fallCount{ 0 }
        , _phase{ VelmoraPhase::Explore }
    {
    }

    VelmoraGameModeComponent::~VelmoraGameModeComponent() = default;

    SessionStateComponent* VelmoraGameModeComponent::findState() const
    {
        return SessionStateComponent::findOnOwner( *this );
    }

    const VelmoraRegionCatalog* VelmoraGameModeComponent::findRegions() const
    {
        return game::findSubsystem<const VelmoraRegionCatalog>();
    }

    bool VelmoraGameModeComponent::validateRegionMonsters( const VelmoraRegionCatalog& regions ) const
    {
        const MonsterCatalog* const pMonsters = game::findSubsystem<const MonsterCatalog>();
        if ( pMonsters == nullptr )
        {
            SW_LOG_ERROR_ID( "Velmora.NoMonsters", "Velmora: the game has no monster catalog service - the valley does not open" );
            return false;
        }
        bool bAllFound = true;
        for ( const VelmoraRegionDef& region : regions.getAll() )
        {
            for ( const vector<ActionRoomSpawnDef>* pListSpawn : { &region._listHallSpawn, &region._listBossSpawn } )
            {
                for ( const ActionRoomSpawnDef& spawn : *pListSpawn )
                {
                    if ( pMonsters->findMonster( spawn._monsterID ) != nullptr )
                        continue;
                    SW_LOG_ERROR_ID( "Velmora.UnknownMonster", "Velmora: region '%#' places monster '%#', which the monster catalog does not have", region._id.c_str(), spawn._monsterID.c_str() );
                    bAllFound = false;
                }
            }
        }
        return bAllFound;
    }

    bool VelmoraGameModeComponent::startGame()
    {
        const VelmoraRegionCatalog* const pRegions = findRegions();
        const QuestCatalog* const         pQuests  = game::findSubsystem<const QuestCatalog>();
        if ( pRegions == nullptr || pQuests == nullptr )
        {
            SW_LOG_ERROR_ID( "Velmora.NoData", "Velmora: the region (%#) or quest (%#) catalog service is missing - the valley does not open", pRegions != nullptr ? "ok" : "missing",
                             pQuests != nullptr ? "ok" : "missing" );
            return false;
        }
        const VelmoraRegionDef* const pStart = pRegions->find( _startRegion );
        if ( pStart == nullptr )
        {
            SW_LOG_ERROR_ID( "Velmora.NoStartRegion", "Velmora: start region '%#' is not in the region catalog - the valley does not open", _startRegion.c_str() );
            return false;
        }
        if ( validateRegionMonsters( *pRegions ) == false )
            return false;
        SessionStateSettings settings;
        settings._pQuestCatalog = pQuests;
        if ( SessionStateComponent::openOnOwner( *this, settings ) == nullptr )
            return false;
        if ( _campaign.initialize( _vitalsSettings, _startRegion ) == false )
            return false;
        _room.clear();
        _bossDelay.clear();
        _playerPosition  = pStart->_respawn;
        _facing          = FacingDir::Right;
        _currentRegion   = hashed_string{};
        _encounterRegion = hashed_string{};
        _fallCount       = 0;
        _phase           = VelmoraPhase::Explore;
        return true;
    }

    void VelmoraGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Velmora.Started", "[Velmora] the valley opens at (%#, %#) - Move to walk, Velmora.Use at beacons and camps, Velmora.Attack and Velmora.Dash in fights", _playerPosition._x,
                        _playerPosition._y );
    }

    void VelmoraGameModeComponent::movePlayer( const float2& move, float32 deltaTime, const VelmoraRegionCatalog& regions )
    {
        if ( move._x > VelmoraGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDir::Right;
        else if ( move._x < -VelmoraGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDir::Left;
        const float2 next{ _playerPosition._x + move._x * _moveSpeed * deltaTime, _playerPosition._y + move._y * _moveSpeed * deltaTime };
        if ( _encounterRegion.empty() == false )
        {
            // 전투 중에는 게이트가 닫힌다 — 그 지역 경계 안에 묶는다.
            const VelmoraRegionDef* const pArena = regions.find( _encounterRegion );
            if ( pArena != nullptr )
            {
                _playerPosition = float2{ MathUtil::clamp( next._x, pArena->_min._x, pArena->_max._x ), MathUtil::clamp( next._y, pArena->_min._y, pArena->_max._y ) };
                return;
            }
        }
        // 지역이 땅이다 — 지역 밖으로는 가지 않는다(축마다 따로 봐서 벽을 따라 미끄러진다).
        if ( regions.findAt( next ) != nullptr )
            _playerPosition = next;
        else if ( regions.findAt( float2{ next._x, _playerPosition._y } ) != nullptr )
            _playerPosition._x = next._x;
        else if ( regions.findAt( float2{ _playerPosition._x, next._y } ) != nullptr )
            _playerPosition._y = next._y;
    }

    void VelmoraGameModeComponent::updateRegion( const VelmoraRegionCatalog& regions, SessionStateComponent& state )
    {
        const VelmoraRegionDef* const pRegion = regions.findAt( _playerPosition );
        if ( pRegion == nullptr || pRegion->_id == _currentRegion )
            return;
        _currentRegion         = pRegion->_id;
        const bool bFirstVisit = _campaign.enterRegion( *pRegion, state.getQuestLog(), state.getFlags() );
        _pendingArea           = pRegion->_id;
        SW_LOG_INFO_ID( "Velmora.RegionEntered", "[Velmora] entered '%#'%#", pRegion->_name, bFirstVisit ? " for the first time" : "" );
        if ( _phase == VelmoraPhase::Explore && pRegion->hasEncounter() && VelmoraCampaign::isEncounterCleared( *pRegion, state.getFlags() ) == false )
            beginEncounter( *pRegion );
    }

    void VelmoraGameModeComponent::beginEncounter( const VelmoraRegionDef& region )
    {
        _encounterRegion = region._id;
        _room.setLayout( region.makeLayout() );
        if ( region._listHallSpawn.empty() == false )
        {
            _room.beginHall();
            _phase = VelmoraPhase::Hall;
            SW_LOG_INFO_ID( "Velmora.HallStarted", "[Velmora] '%#': a fight with %# enemies", region._name, _room.getAliveEnemyCount() );
            return;
        }
        _phase = VelmoraPhase::BossWait;
        _bossDelay.start( _bossDelaySeconds );
        _pendingBoss = region._id;
    }

    void VelmoraGameModeComponent::interactWithSite( const VelmoraRegionDef& region, SessionStateComponent& state )
    {
        const float32 radiusSquared = _useRadius * _useRadius;
        const auto    isNear        = [this, radiusSquared]( const VelmoraSiteDef& site )
        {
            const float32 dx = site._position._x - _playerPosition._x;
            const float32 dz = site._position._y - _playerPosition._y;
            return site.isSet() && dx * dx + dz * dz <= radiusSquared;
        };
        if ( isNear( region._beacon ) )
        {
            if ( _campaign.lightBeacon( region, state.getQuestLog(), state.getFlags() ) )
                SW_LOG_INFO_ID( "Velmora.BeaconLit", "[Velmora] lit beacon '%#' in '%#'", region._beacon._id.c_str(), region._name );
            return;
        }
        if ( isNear( region._camp ) && _campaign.restAtCamp( region ) )
        {
            _pendingCheckpoint = region._camp._id;
            SW_LOG_INFO_ID( "Velmora.Rested", "[Velmora] rested at camp '%#' - health full, rising here after a fall", region._camp._id.c_str() );
        }
    }

    void VelmoraGameModeComponent::updateEncounter( float32 deltaTime, const VelmoraRegionCatalog& regions, SessionStateComponent& state )
    {
        static const hashed_string    s_attackAction( VelmoraGameModeComponentInternal::kAttackAction );
        static const hashed_string    s_dashAction( VelmoraGameModeComponentInternal::kDashAction );
        const VelmoraRegionDef* const pArena = regions.find( _encounterRegion );
        if ( pArena == nullptr ) // 판을 연 뒤 표가 지역을 잃지 않는다 — 다시 읽기로 빠졌으면 전투를 닫는다
        {
            _room.clear();
            _encounterRegion = hashed_string{};
            _phase           = VelmoraPhase::Explore;
            return;
        }
        if ( _phase == VelmoraPhase::BossWait )
        {
            if ( _bossDelay.update( deltaTime ) )
            {
                _room.beginBoss();
                _phase = VelmoraPhase::Boss;
                SW_LOG_INFO_ID( "Velmora.BossStarted", "[Velmora] '%#': the boss fight begins with %# enemies", pArena->_name, _room.getAliveEnemyCount() );
            }
            return;
        }
        ActionRoomFrameInput input;
        input._playerPos                   = _playerPosition;
        input._facing                      = _facing;
        input._bAttackPressed              = wasActionTriggered( s_attackAction ) ? SW_TRUE : SW_FALSE;
        input._bDashPressed                = wasActionTriggered( s_dashAction ) ? SW_TRUE : SW_FALSE;
        const ActionRoomFrameResult result = _room.update( deltaTime, input );
        // 적의 닿은 피해(`MonsterStatsDef::_atk`)는 4분의 1 하트 단위다.
        if ( result._damageToPlayer > 0 && _campaign.takeDamage( result._damageToPlayer ) )
        {
            handlePlayerDowned( regions );
            return;
        }
        if ( result._bClearedThisFrame == SW_FALSE )
            return;
        if ( _phase == VelmoraPhase::Hall && pArena->_listBossSpawn.empty() == false )
        {
            _phase = VelmoraPhase::BossWait;
            _bossDelay.start( _bossDelaySeconds );
            _pendingBoss = pArena->_id;
            SW_LOG_INFO_ID( "Velmora.HallCleared", "[Velmora] '%#': hall cleared, the boss comes in %# s", pArena->_name, _bossDelaySeconds );
            return;
        }
        _campaign.clearEncounter( *pArena, state.getQuestLog(), state.getFlags() );
        _encounterRegion = hashed_string{};
        _phase           = VelmoraPhase::Explore;
        SW_LOG_INFO_ID( "Velmora.RegionCleared", "[Velmora] '%#' cleared%#", pArena->_name, result._bBossDefeated == SW_TRUE ? " - the boss fell" : "" );
    }

    void VelmoraGameModeComponent::handlePlayerDowned( const VelmoraRegionCatalog& regions )
    {
        _room.onPlayerDefeated();
        _bossDelay.clear();
        _encounterRegion = hashed_string{};
        _phase           = VelmoraPhase::Explore;
        ++_fallCount;
        const VelmoraRegionDef* pRespawn = regions.find( _campaign.getRespawnRegion() );
        if ( pRespawn == nullptr ) // 일어설 지역이 표에서 빠진 판(확장 팩을 끈 판)은 시작 지역에서 일어선다
        {
            pRespawn = regions.find( _startRegion );
            _campaign.setRespawnRegion( _startRegion );
        }
        if ( pRespawn != nullptr )
            _playerPosition = pRespawn->_respawn;
        _currentRegion = hashed_string{}; // 일어선 자리의 진입을 다음 틱이 다시 처리한다
        if ( _campaign.respawn() == false )
            SW_LOG_ERROR_ID( "Velmora.CannotRise", "Velmora: the player could not rise - the vitals settings were refused" );
        SW_LOG_INFO_ID( "Velmora.Fell", "[Velmora] the player fell and rises in '%#'", _campaign.getRespawnRegion().c_str() );
    }

    void VelmoraGameModeComponent::reconcileWithRegions( const VelmoraRegionCatalog& regions )
    {
        if ( regions.find( _campaign.getRespawnRegion() ) == nullptr )
        {
            SW_LOG_WARNING_ID( "Velmora.RespawnRegionGone", "Velmora: the loaded game rises in region '%#', which is not in the region catalog (expansion off?) - rising in '%#' instead",
                               _campaign.getRespawnRegion().c_str(), _startRegion.c_str() );
            _campaign.setRespawnRegion( _startRegion );
        }
        if ( regions.findAt( _playerPosition ) != nullptr )
            return;
        const VelmoraRegionDef* const pRespawn = regions.find( _campaign.getRespawnRegion() );
        if ( pRespawn == nullptr )
            return;
        SW_LOG_WARNING_ID( "Velmora.PlayerOutsideRegions", "Velmora: the loaded player stands at (%#, %#), outside every region (expansion off?) - moved to the respawn point of '%#'",
                           _playerPosition._x, _playerPosition._y, pRespawn->_id.c_str() );
        _playerPosition = pRespawn->_respawn;
    }

    void VelmoraGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const VelmoraRegionCatalog* const pRegions = findRegions();
        SessionStateComponent* const      pState   = findState();
        if ( pRegions == nullptr || pState == nullptr )
            return;
        static const hashed_string s_moveAction( CommonInputActions::kMove );
        static const hashed_string s_useAction( VelmoraGameModeComponentInternal::kUseAction );

        movePlayer( getActionVector2D( s_moveAction ), deltaTime, *pRegions );
        updateRegion( *pRegions, *pState );
        if ( _phase == VelmoraPhase::Explore && wasActionTriggered( s_useAction ) )
        {
            if ( const VelmoraRegionDef* const pRegion = pRegions->find( _currentRegion ) )
                interactWithSite( *pRegion, *pState );
        }
        if ( _phase != VelmoraPhase::Explore )
            updateEncounter( deltaTime, *pRegions, *pState );

        // 알림 그릇은 프레임마다 비운다 — 퀘스트 보상은 여기서 주고, 지갑 · 체력 알림은 읽는 화면이 아직 없어 버린다(쌓이지 않게).
        _listQuestEvent.clear();
        pState->getQuestLog().pollEvents( _listQuestEvent );
        const int32 completedCount = _campaign.applyQuestEvents( _listQuestEvent, pState->getWallet() );
        if ( completedCount > 0 )
            SW_LOG_INFO_ID( "Velmora.QuestCompleted", "[Velmora] %# quest(s) completed - gold %#, hearts %#", completedCount, pState->getWallet().getBalance( Wallet::getDefaultCurrency() ),
                            _campaign.getVitals().getHeartCount() );
        _listWalletEvent.clear();
        pState->getWallet().pollEvents( _listWalletEvent );
        _listHealthEvent.clear();
        _campaign.pollHealthEvents( _listHealthEvent );

        drawWorld( *pRegions, *pState );
    }

    void VelmoraGameModeComponent::drawWorld( const VelmoraRegionCatalog& regions, const SessionStateComponent& state ) const
    {
        DebugDrawQueue* const pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr )
            return;
        using Internal       = VelmoraGameModeComponentInternal;
        const float32 height = Internal::kDrawHeight;
        for ( const VelmoraRegionDef& region : regions.getAll() )
        {
            // 지역 경계 — 씬에 땅 메시가 없는 지역(확장 팩)도 어디까지 걸을 수 있는지 보인다.
            const float4 border( 0.75f, 0.8f, 0.7f, 1.0f );
            const float3 corner00( region._min._x, 0.05f, region._min._y );
            const float3 corner10( region._max._x, 0.05f, region._min._y );
            const float3 corner11( region._max._x, 0.05f, region._max._y );
            const float3 corner01( region._min._x, 0.05f, region._max._y );
            pDraw->drawLine( corner00, corner10, border );
            pDraw->drawLine( corner10, corner11, border );
            pDraw->drawLine( corner11, corner01, border );
            pDraw->drawLine( corner01, corner00, border );
            if ( region._beacon.isSet() )
            {
                const bool   bLit  = VelmoraCampaign::isBeaconLit( region, state.getFlags() );
                const float4 color = bLit ? float4( 1.0f, 0.62f, 0.18f, 1.0f ) : float4( 0.35f, 0.35f, 0.4f, 1.0f );
                pDraw->drawBox( float3( region._beacon._position._x, 1.0f, region._beacon._position._y ), float3( 0.3f, 1.0f, 0.3f ), color );
            }
            if ( region._camp.isSet() )
                pDraw->drawSphere( float3( region._camp._position._x, 0.2f, region._camp._position._y ), 0.4f, float4( 0.95f, 0.4f, 0.1f, 1.0f ) );
        }
        if ( _phase == VelmoraPhase::Hall || _phase == VelmoraPhase::Boss )
            _room.drawDebug();
        const float4 playerColor = _room.isPlayerInvulnerable() ? float4( 0.6f, 0.9f, 1.0f, 1.0f ) : float4( 0.2f, 0.6f, 1.0f, 1.0f );
        pDraw->drawSphere( float3( _playerPosition._x, height, _playerPosition._y ), 0.28f, playerColor );
    }

    bool VelmoraGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = float3( _playerPosition._x, 0.0f, _playerPosition._y );
        return isStarted();
    }

    bool VelmoraGameModeComponent::hasPendingSpawn() const
    {
        return _pendingArea.empty() == false || _pendingBoss.empty() == false || _pendingCheckpoint.empty() == false;
    }

    void VelmoraGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 디버그 그리기뿐이라 세울 오브젝트가 없다. 자동 저장 관리자는 게임 스레드 전용이라 틱에서 쌓은 요청을 여기서 낸다.
        (void)manager;
        (void)bRespawnViews;
        if ( AutosaveManager* const pAutosave = game::findSubsystem<AutosaveManager>() )
        {
            if ( _pendingArea.empty() == false )
                pAutosave->notifyAreaChanged( _pendingArea );
            if ( _pendingBoss.empty() == false )
                pAutosave->notifyBeforeBoss( _pendingBoss );
            if ( _pendingCheckpoint.empty() == false )
                pAutosave->reachCheckpoint( _pendingCheckpoint );
        }
        _pendingArea       = hashed_string{};
        _pendingBoss       = hashed_string{};
        _pendingCheckpoint = hashed_string{};
    }

    void VelmoraGameModeComponent::serializeState( StateIO& io )
    {
        // 싸움은 싣지 않는다 — 불러온 판은 그 지역에 다시 들어서며 전투를 처음부터 연다(보스 앞 자동 저장도 같은 자리에서 다시 싸운다).
        // 그래서 확장 팩을 끈 판이 확장 지역의 적 종을 모르는 바이트를 만나지 않는다. 판 상태(퀘스트 · 깃발 · 지갑)는 같은 오브젝트의 판 상태가 싣는다.
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        VelmoraCampaign campaign       = _campaign;
        float2          playerPosition = _playerPosition;
        FacingDir       facing         = _facing;
        int32           fallCount      = _fallCount;
        io.value( campaign );
        io.value( playerPosition );
        io.enumValue( facing, FacingDir::Up );
        io.value( fallCount );
        io.check( fallCount >= 0 );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _campaign       = campaign;
        _playerPosition = playerPosition;
        _facing         = facing;
        _fallCount      = fallCount;
        _room.clear();
        _bossDelay.clear();
        _currentRegion   = hashed_string{};
        _encounterRegion = hashed_string{};
        _phase           = VelmoraPhase::Explore;
        if ( const VelmoraRegionCatalog* const pRegions = findRegions() )
            reconcileWithRegions( *pRegions );
    }

    void VelmoraGameModeComponent::onRegister( GameObjectManager& manager )
    {
        GameModeComponent::onRegister( manager );
        manager.getComponentRegistry().add<VelmoraGameModeComponent>( this );
    }

    void VelmoraGameModeComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<VelmoraGameModeComponent>( this );
        GameModeComponent::onUnregister( manager );
    }
} // namespace sw
