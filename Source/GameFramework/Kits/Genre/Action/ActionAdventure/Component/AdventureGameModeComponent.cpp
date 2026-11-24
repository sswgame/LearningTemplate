#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Autosave.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    namespace
    {
        struct AdventureGameModeComponentInternal
        {
            /** @brief 축 입력이 이보다 작으면 바라보는 쪽을 바꾸지 않는다(패드 스틱의 떨림). */
            static constexpr float32 kFacingDeadZone = 0.2f;

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventHurt  = "AdventureHurt";
            static constexpr const utf8* kEventHeal  = "AdventureHeal";
            static constexpr const utf8* kEventDied  = "AdventureDied";
            static constexpr const utf8* kEventCoin  = "AdventureCoin";
            static constexpr const utf8* kEventQuest = "AdventureQuest";

            /** @brief 체력 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( HealthEventKind kind )
            {
                switch ( kind )
                {
                    case HealthEventKind::Damaged:
                    {
                        return kEventHurt;
                    }
                    case HealthEventKind::Healed:
                    case HealthEventKind::Revived:
                    {
                        return kEventHeal;
                    }
                    case HealthEventKind::Died:
                    {
                        return kEventDied;
                    }
                    case HealthEventKind::ShieldBroken: // 모험의 체력은 하트뿐이다 — 실드 · 기절 · 경직이 없다
                    case HealthEventKind::Downed:
                    case HealthEventKind::ReviveStarted:
                    case HealthEventKind::ReviveInterrupted:
                    case HealthEventKind::PoiseBroken:
                    case HealthEventKind::PoiseRecovered:
                    {
                        break;
                    }
                }
                return nullptr;
            }

            /** @brief 디렉터(씬에 하나)의 지금 지역 이름입니다(감시 값 `Adventure.Region`). */
            [[nodiscard]] static bool readRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue, bool bRespawn )
            {
                // 디렉터는 씬에 하나다 — 둘이면 오류로 값이 없다(등록 순서로 고르지 않는다).
                const AdventureGameModeComponent* pDirector = query._pManager != nullptr ? query._pManager->getComponentRegistry().findUnique<AdventureGameModeComponent>() : nullptr;
                if ( pDirector == nullptr )
                {
                    outValue.setText( "no adventure director in the scene" );
                    return false;
                }
                outValue.setText( string_view{ ( bRespawn ? pDirector->getRespawnRegion() : pDirector->getCurrentRegion() ).c_str() } );
                return true;
            }
            [[nodiscard]] static bool readCurrentRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readRegion( query, outValue, false ); }
            [[nodiscard]] static bool readRespawnRegion( const AutomationWatchQuery& query, AutomationWatchValue& outValue ) { return readRegion( query, outValue, true ); }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( adventurePlayerX, "Adventure.PlayerX", "Adventure player plane X (m)", AdventureGameModeComponent, &AdventureGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( adventurePlayerZ, "Adventure.PlayerZ", "Adventure player plane Z (m)", AdventureGameModeComponent, &AdventureGameModeComponent::getPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( adventureHealth, "Adventure.Health", "Adventure player health in quarter hearts", AdventureGameModeComponent, &AdventureGameModeComponent::getHealthQuarters );
    SW_AUTOMATION_GETTER_WATCH( adventureHearts, "Adventure.Hearts", "Adventure player heart containers", AdventureGameModeComponent, &AdventureGameModeComponent::getHeartCount );
    SW_AUTOMATION_GETTER_WATCH( adventureFalls, "Adventure.Falls", "Times the adventure player fell this game", AdventureGameModeComponent, &AdventureGameModeComponent::getFallCount );
    SW_AUTOMATION_QUERY_WATCH( adventureRegion, "Adventure.Region", "Region id the adventure player stands in", &AdventureGameModeComponentInternal::readCurrentRegion, false, false );
    SW_AUTOMATION_QUERY_WATCH( adventureRespawn, "Adventure.Respawn", "Region id the adventure player rises in after a fall", &AdventureGameModeComponentInternal::readRespawnRegion, false, false );

    AdventureGameModeComponent::AdventureGameModeComponent()
        : _startRegion{}
        , _flagPrefix{}
        , _moveSpeed{ 6.0f }
        , _useRadius{ 1.5f }
        , _vitalsSettings{}
        , _pRegions{}
        , _pQuests{}
        , _campaign{}
        , _playerPosition{ 0.0f, 0.0f }
        , _commandMove{ 0.0f, 0.0f }
        , _facing{ FacingDirection::Right }
        , _currentRegion{}
        , _pendingEncounter{}
        , _encounterRegion{}
        , _pendingArea{}
        , _pendingBoss{}
        , _pendingCheckpoint{}
        , _listQuestEvent{}
        , _listWalletEvent{}
        , _listHealthEvent{}
        , _fallCount{ 0 }
        , _bUseQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    AdventureGameModeComponent::~AdventureGameModeComponent() = default;

    bool AdventureGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pRegions ) == false )
            return false;
        if ( _pQuests == nullptr )
        {
            SharedAssetRef<QuestCatalog> quests;
            if ( acquireBoundCatalog( quests ) == false )
                return false;
            _pQuests = quests.snapshot();
        }
        const AdventureRegionDef* const pStart = _pRegions->find( _startRegion );
        if ( pStart == nullptr )
        {
            SW_LOG_ERROR_ID( "Adventure.NoStartRegion", "Adventure: start region '%#' is not in the region catalog - the game does not open", _startRegion.c_str() );
            return false;
        }
        if ( GameStateComponent::openOnOwner( *this, GameStateSettings{} ) == nullptr )
            return false;
        const GameObject* const pPlayerState = requirePlayerState();
        if ( pPlayerState == nullptr || PlayerComponentUtil::openOn<QuestLogComponent>( *pPlayerState, *_pQuests ) == nullptr ||
             PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState ) == nullptr )
            return false; // 까닭은 찾기가 남겼다
        if ( _campaign.initialize( _vitalsSettings, _startRegion.key(), _flagPrefix ) == false )
            return false;
        _playerPosition   = pStart->_respawn;
        _commandMove      = float2{ 0.0f, 0.0f };
        _facing           = FacingDirection::Right;
        _currentRegion    = hashed_string{};
        _pendingEncounter = hashed_string{};
        _encounterRegion  = hashed_string{};
        _fallCount        = 0;
        _bUseQueued       = SW_FALSE;
        return true;
    }

    void AdventureGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Adventure.Started", "[Adventure] the game opens at (%#, %#) - the player input arrives as commands (move, use)", _playerPosition._x, _playerPosition._y );
    }

    bool AdventureGameModeComponent::submitPlayerInput( const float2& move, bool bUsePressed )
    {
        if ( isStarted() == false )
            return false;
        _commandMove = move;
        if ( bUsePressed )
            _bUseQueued = SW_TRUE;
        return true;
    }

    void AdventureGameModeComponent::movePlayer( const float2& move, float32 deltaTime, const AdventureRegionCatalog& regions )
    {
        if ( move._x > AdventureGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDirection::Right;
        else if ( move._x < -AdventureGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDirection::Left;
        const float2 next{ _playerPosition._x + move._x * _moveSpeed * deltaTime, _playerPosition._y + move._y * _moveSpeed * deltaTime };
        if ( _encounterRegion.empty() == false )
        {
            // 전투 중에는 게이트가 닫힌다 — 그 지역 경계 안에 묶는다.
            const AdventureRegionDef* const pArena = regions.find( _encounterRegion );
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

    void AdventureGameModeComponent::updateRegion( const AdventureRegionCatalog& regions, GameStateComponent& state, QuestLog& questLog )
    {
        const AdventureRegionDef* const pRegion = regions.findAt( _playerPosition );
        if ( pRegion == nullptr || pRegion->_id == _currentRegion )
            return;
        _currentRegion         = pRegion->_id;
        _pendingEncounter      = hashed_string{};
        const bool bFirstVisit = _campaign.enterRegion( *pRegion, questLog, state.getFlags() );
        _pendingArea           = pRegion->_id;
        SW_LOG_INFO_ID( "Adventure.RegionEntered", "[Adventure] entered '%#'%#", pRegion->_name, bFirstVisit ? " for the first time" : "" );
        if ( _encounterRegion.empty() && pRegion->hasEncounter() && _campaign.isEncounterCleared( *pRegion, state.getFlags() ) == false )
            _pendingEncounter = pRegion->_id;
    }

    const AdventureRegionDef* AdventureGameModeComponent::openEncounter()
    {
        const AdventureRegionCatalog* const pRegions = findRegions();
        const AdventureRegionDef* const     pRegion  = pRegions != nullptr ? pRegions->find( _pendingEncounter ) : nullptr;
        _pendingEncounter                            = hashed_string{};
        if ( pRegion != nullptr )
            _encounterRegion = pRegion->_id;
        return pRegion;
    }

    void AdventureGameModeComponent::announceBoss()
    {
        _pendingBoss = _encounterRegion;
    }

    bool AdventureGameModeComponent::applyPlayerDamage( int32 quarters )
    {
        const AdventureRegionCatalog* const pRegions = findRegions();
        if ( pRegions == nullptr || _campaign.takeDamage( quarters ) == false )
            return false;
        handlePlayerDowned( *pRegions );
        return true;
    }

    void AdventureGameModeComponent::completeEncounter()
    {
        const AdventureRegionCatalog* const pRegions  = findRegions();
        GameStateComponent* const           pState    = GameStateComponent::findOnOwner( *this );
        QuestLogComponent* const            pQuestLog = findOnPlayerState<QuestLogComponent>();
        WalletComponent* const              pWallet   = findOnPlayerState<WalletComponent>();
        const AdventureRegionDef* const     pRegion   = pRegions != nullptr ? pRegions->find( _encounterRegion ) : nullptr;
        if ( pRegion == nullptr || pState == nullptr || pQuestLog == nullptr || pWallet == nullptr )
            return;
        _campaign.clearEncounter( *pRegion, pQuestLog->getQuestLog(), pState->getFlags() );
        _encounterRegion = hashed_string{};
        applyQuestEvents( pQuestLog->getQuestLog(), pWallet->getWallet() ); // 끝난 퀘스트와 그 보상이 같은 걸음에 보인다
    }

    void AdventureGameModeComponent::interactWithSite( const AdventureRegionDef& region, GameStateComponent& state, QuestLog& questLog )
    {
        const float32 radiusSquared = _useRadius * _useRadius;
        const auto    isNear        = [this, radiusSquared]( const AdventureSiteDef& site )
        {
            const float32 dx = site._position._x - _playerPosition._x;
            const float32 dz = site._position._y - _playerPosition._y;
            return site.isSet() && dx * dx + dz * dz <= radiusSquared;
        };
        if ( isNear( region._beacon ) )
        {
            if ( _campaign.lightBeacon( region, questLog, state.getFlags() ) )
                SW_LOG_INFO_ID( "Adventure.BeaconLit", "[Adventure] lit beacon '%#' in '%#'", region._beacon._id.c_str(), region._name );
            return;
        }
        if ( isNear( region._camp ) && _campaign.restAtCamp( region ) )
        {
            _pendingCheckpoint = region._camp._id;
            SW_LOG_INFO_ID( "Adventure.Rested", "[Adventure] rested at camp '%#' - health full, rising here after a fall", region._camp._id.c_str() );
        }
    }

    void AdventureGameModeComponent::handlePlayerDowned( const AdventureRegionCatalog& regions )
    {
        _encounterRegion = hashed_string{};
        ++_fallCount;
        const AdventureRegionDef* pRespawn = regions.find( _campaign.getRespawnRegion() );
        if ( pRespawn == nullptr ) // 일어설 지역이 표에서 빠진 판(확장 팩을 끈 판)은 시작 지역에서 일어선다
        {
            pRespawn = regions.find( _startRegion );
            _campaign.setRespawnRegion( _startRegion.key() );
        }
        if ( pRespawn != nullptr )
            _playerPosition = pRespawn->_respawn;
        _currentRegion = hashed_string{}; // 일어선 자리의 진입을 다음 틱이 다시 처리한다
        if ( _campaign.respawn() == false )
            SW_LOG_ERROR_ID( "Adventure.CannotRise", "Adventure: the player could not rise - the vitals settings were refused" );
        SW_LOG_INFO_ID( "Adventure.Fell", "[Adventure] the player fell and rises in '%#'", _campaign.getRespawnRegion().c_str() );
    }

    void AdventureGameModeComponent::reconcileWithRegions( const AdventureRegionCatalog& regions )
    {
        if ( regions.find( _campaign.getRespawnRegion() ) == nullptr )
        {
            SW_LOG_WARNING_ID( "Adventure.RespawnRegionGone", "Adventure: the loaded game rises in region '%#', which is not in the region catalog (expansion off?) - rising in '%#' instead",
                               _campaign.getRespawnRegion().c_str(), _startRegion.c_str() );
            _campaign.setRespawnRegion( _startRegion.key() );
        }
        if ( regions.findAt( _playerPosition ) != nullptr )
            return;
        const AdventureRegionDef* const pRespawn = regions.find( _campaign.getRespawnRegion() );
        if ( pRespawn == nullptr )
            return;
        SW_LOG_WARNING_ID( "Adventure.PlayerOutsideRegions", "Adventure: the loaded player stands at (%#, %#), outside every region (expansion off?) - moved to the respawn point of '%#'",
                           _playerPosition._x, _playerPosition._y, pRespawn->_id.c_str() );
        _playerPosition = pRespawn->_respawn;
    }

    void AdventureGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const AdventureRegionCatalog* const pRegions  = findRegions();
        GameStateComponent* const           pState    = GameStateComponent::findOnOwner( *this );
        QuestLogComponent* const            pQuestLog = findOnPlayerState<QuestLogComponent>();
        WalletComponent* const              pWallet   = findOnPlayerState<WalletComponent>();
        if ( pRegions == nullptr || pState == nullptr || pQuestLog == nullptr || pWallet == nullptr )
            return;
        const bool bUse = _bUseQueued == SW_TRUE;
        _bUseQueued     = SW_FALSE;
        movePlayer( _commandMove, deltaTime, *pRegions );
        updateRegion( *pRegions, *pState, pQuestLog->getQuestLog() );
        if ( bUse && _encounterRegion.empty() )
        {
            if ( const AdventureRegionDef* const pRegion = pRegions->find( _currentRegion ) )
                interactWithSite( *pRegion, *pState, pQuestLog->getQuestLog() );
        }

        // 알림 그릇은 프레임마다 비운다 — 퀘스트 보상은 여기서 주고, 지갑 · 체력 알림은 소리로 알린다.
        applyQuestEvents( pQuestLog->getQuestLog(), pWallet->getWallet() );
        _listWalletEvent.clear();
        pWallet->getWallet().pollEvents( _listWalletEvent );
        for ( const WalletEvent& event : _listWalletEvent )
        {
            if ( event._delta > 0 )
                getSoundQueue().queueEvent( AdventureGameModeComponentInternal::kEventCoin );
        }
        _listHealthEvent.clear();
        _campaign.pollHealthEvents( _listHealthEvent );
        for ( const HealthEvent& event : _listHealthEvent )
        {
            if ( const utf8* const pSound = AdventureGameModeComponentInternal::findSound( event._type ) )
                getSoundQueue().queueEvent( pSound );
        }
    }

    void AdventureGameModeComponent::applyQuestEvents( QuestLog& questLog, Wallet& wallet )
    {
        _listQuestEvent.clear();
        questLog.pollEvents( _listQuestEvent );
        const int32 completedCount = _campaign.applyQuestEvents( _listQuestEvent, wallet );
        if ( completedCount <= 0 )
            return;
        getSoundQueue().queueEvent( AdventureGameModeComponentInternal::kEventQuest );
        SW_LOG_INFO_ID( "Adventure.QuestCompleted", "[Adventure] %# quest(s) completed - gold %#, hearts %#", completedCount, wallet.getBalance( Wallet::getDefaultCurrency() ),
                        _campaign.getVitals().getHeartCount() );
    }

    bool AdventureGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = float3( _playerPosition._x, 0.0f, _playerPosition._y );
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( outFocusPosition ); // 지역 표 · 자리는 절대 자리다
        return isStarted();
    }

    bool AdventureGameModeComponent::hasPendingSpawn() const
    {
        return _pendingArea.empty() == false || _pendingBoss.empty() == false || _pendingCheckpoint.empty() == false;
    }

    void AdventureGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
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

    void AdventureGameModeComponent::serializeGameState( StateIO& io )
    {
        // 싸움은 싣지 않는다 — 불러온 판은 그 지역에 다시 들어서며 전투를 처음부터 연다(보스 앞 자동 저장도 같은 자리에서 다시 싸운다).
        // 그래서 확장 팩을 끈 판이 확장 지역의 적 종을 모르는 바이트를 만나지 않는다. 판 상태(퀘스트 · 깃발 · 지갑)는 같은 오브젝트의 판 상태가 싣는다.
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        // 자리 · 쓰러진 수는 상태 칸이다(`GameModeComponent::serializeState`). 판 진행(리플렉션 칸이 아닌 멤버) · 방향(리플렉션 열거가 아니다)은 손 본문이 싣는다.
        AdventureCampaign campaign = _campaign;
        FacingDirection   facing   = _facing;
        io.value( campaign );
        io.enumValue( facing, FacingDirection::Up );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _campaign         = campaign;
        _facing           = facing;
        _commandMove      = float2{ 0.0f, 0.0f };
        _bUseQueued       = SW_FALSE;
        _currentRegion    = hashed_string{};
        _pendingEncounter = hashed_string{};
        _encounterRegion  = hashed_string{};
        // 앞 판이 이번 프레임에 쌓은 자동 저장 알림은 되돌린 판의 것이 아니다.
        _pendingArea       = hashed_string{};
        _pendingBoss       = hashed_string{};
        _pendingCheckpoint = hashed_string{};
        if ( const AdventureRegionCatalog* const pRegions = findRegions() )
            reconcileWithRegions( *pRegions );
    }
} // namespace sw
