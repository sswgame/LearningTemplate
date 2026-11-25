#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/World/Query/WalkSweep.h"

namespace sw
{
    namespace
    {
        struct MonsterCollectorGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "MCGM" };

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventEncounter = "MonsterCollectorEncounter";
            static constexpr const utf8* kEventHit       = "MonsterCollectorHit";
            static constexpr const utf8* kEventFainted   = "MonsterCollectorFainted";
            static constexpr const utf8* kEventLevelUp   = "MonsterCollectorLevelUp";
            static constexpr const utf8* kEventEvolved   = "MonsterCollectorEvolved";
            static constexpr const utf8* kEventSwitched  = "MonsterCollectorSwitched";
            static constexpr const utf8* kEventShake     = "MonsterCollectorShake";
            static constexpr const utf8* kEventCaptured  = "MonsterCollectorCaptured";
            static constexpr const utf8* kEventBrokeFree = "MonsterCollectorBrokeFree";
            static constexpr const utf8* kEventEscaped   = "MonsterCollectorEscaped";
            static constexpr const utf8* kEventRest      = "MonsterCollectorRest";
            static constexpr const utf8* kEventWon       = "MonsterCollectorWon";
            static constexpr const utf8* kEventLost      = "MonsterCollectorLost";
            static constexpr const utf8* kEventRefused   = "MonsterCollectorRefused";
            static constexpr const utf8* kEventMissed    = "MonsterCollectorMissed";
            static constexpr const utf8* kEventCritical  = "MonsterCollectorCritical";
            static constexpr const utf8* kEventStatus    = "MonsterCollectorStatus";
            static constexpr const utf8* kEventStat      = "MonsterCollectorStat";
            static constexpr const utf8* kEventRecovered = "MonsterCollectorRecovered";
            static constexpr const utf8* kEventWeather   = "MonsterCollectorWeather";
            static constexpr const utf8* kEventLearned   = "MonsterCollectorLearned";

            /** @brief 전투 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( MonsterBattleEvent::Kind kind )
            {
                switch ( kind )
                {
                    case MonsterBattleEvent::Kind::Damage:
                    case MonsterBattleEvent::Kind::StatusDamage:
                    case MonsterBattleEvent::Kind::WeatherDamage:
                    {
                        return kEventHit;
                    }
                    case MonsterBattleEvent::Kind::Fainted:
                    {
                        return kEventFainted;
                    }
                    case MonsterBattleEvent::Kind::LevelUp:
                    {
                        return kEventLevelUp;
                    }
                    case MonsterBattleEvent::Kind::Switched:
                    {
                        return kEventSwitched;
                    }
                    case MonsterBattleEvent::Kind::CaptureShake:
                    {
                        return kEventShake;
                    }
                    case MonsterBattleEvent::Kind::Captured:
                    {
                        return kEventCaptured;
                    }
                    case MonsterBattleEvent::Kind::BrokeFree:
                    {
                        return kEventBrokeFree;
                    }
                    case MonsterBattleEvent::Kind::Escaped:
                    {
                        return kEventEscaped;
                    }
                    case MonsterBattleEvent::Kind::NoPp:
                    case MonsterBattleEvent::Kind::EscapeFailed:
                    case MonsterBattleEvent::Kind::MoveLearnBlocked:
                    {
                        return kEventRefused;
                    }
                    case MonsterBattleEvent::Kind::Missed:
                    case MonsterBattleEvent::Kind::Immune:
                    {
                        return kEventMissed;
                    }
                    case MonsterBattleEvent::Kind::Critical:
                    {
                        return kEventCritical;
                    }
                    case MonsterBattleEvent::Kind::StatusApplied:
                    case MonsterBattleEvent::Kind::FullyParalyzed:
                    case MonsterBattleEvent::Kind::Asleep:
                    case MonsterBattleEvent::Kind::Frozen:
                    {
                        return kEventStatus;
                    }
                    case MonsterBattleEvent::Kind::StatChanged:
                    {
                        return kEventStat;
                    }
                    case MonsterBattleEvent::Kind::Woke:
                    case MonsterBattleEvent::Kind::Thawed:
                    {
                        return kEventRecovered;
                    }
                    case MonsterBattleEvent::Kind::WeatherStarted:
                    case MonsterBattleEvent::Kind::WeatherEnded:
                    {
                        return kEventWeather;
                    }
                    case MonsterBattleEvent::Kind::LearnedMove:
                    {
                        return kEventLearned;
                    }
                    case MonsterBattleEvent::Kind::MoveUsed:  // 기술 표의 소리다(`queueBattleSound`)
                    case MonsterBattleEvent::Kind::ExpGained: // 오른 레벨이 낸다
                    {
                        break;
                    }
                }
                return nullptr;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( monsterCollectorPhase, "MonsterCollector.Phase", "Monster collector phase (0 explore, 1 battle, 2 victory, 3 defeat)", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorFoeHP, "MonsterCollector.FoeHP", "HP of the wild monster (0 outside a battle)", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getFoeHP );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorBallCount, "MonsterCollector.BallCount", "Balls left", MonsterCollectorGameModeComponent, &MonsterCollectorGameModeComponent::getRemainingBallCount );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorCaptures, "MonsterCollector.CaptureCount", "Monsters captured in this outing", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getCaptureCount );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorBattleCount, "MonsterCollector.BattleCount", "Battles opened in this outing", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getBattleCount );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorPartySize, "MonsterCollector.PartySize", "Monsters in the party", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getPartySize );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorLeaderXP, "MonsterCollector.LeaderXP", "Total experience of the first party monster", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getLeaderXP );
    SW_AUTOMATION_GETTER_WATCH( monsterCollectorPlayerX, "MonsterCollector.PlayerX", "Absolute ground-plane X (m) of the player", MonsterCollectorGameModeComponent,
                                &MonsterCollectorGameModeComponent::getPlayerX );

    MonsterCollectorGameModeComponent::MonsterCollectorGameModeComponent()
        : _elementChart{}
        , _listStarter{}
        , _starterLevel{ 10 }
        , _boxCapacity{ 30 }
        , _area{}
        , _timeOfDay{}
        , _encounterRate{ 0.2f }
        , _graceSteps{ 3 }
        , _ballCount{ 5 }
        , _ballMultiplier{ 1.0f }
        , _captureGoal{ 2 }
        , _captureReward{ 10 }
        , _startPosition{ 0.0f, 0.0f }
        , _restPosition{ 0.0f, 2.0f }
        , _fieldMin{ -8.0f, -4.0f }
        , _fieldMax{ 16.0f, 4.0f }
        , _useRadius{ 1.0f }
        , _moveSpeed{ 4.0f }
        , _bodyRadius{ 0.3f }
        , _stepLength{ 1.0f }
        , _seed{ 7 }
        , _pCatalog{}
        , _pChart{}
        , _storage{}
        , _battle{}
        , _encounterCounter{}
        , _random{}
        , _listBattleEvent{}
        , _playerPosition{ 0.0f, 0.0f }
        , _commandMove{ 0.0f, 0.0f }
        , _strideProgress{ 0.0f }
        , _remainingBallCount{ 0 }
        , _captureCount{ 0 }
        , _battleCount{ 0 }
        , _phase{ MonsterCollectorPhase::Explore }
    {
    }

    MonsterCollectorGameModeComponent::~MonsterCollectorGameModeComponent() = default;

    bool MonsterCollectorGameModeComponent::openStorage()
    {
        if ( _storage.initialize( _pCatalog.get(), _boxCapacity ) == false )
            return false;
        for ( const CatalogKey<MonsterSpeciesDef>& starter : _listStarter )
        {
            if ( _storage.add( MonsterInstance::createMonster( *_pCatalog, starter.key(), _starterLevel, _random ) ) == MonsterStoragePlace::Party )
                continue;
            SW_LOG_ERROR_ID( "MonsterCollector.StarterNotAdded", "Monster collector game mode: starter '%#' cannot join - unknown species or more than %# starters; the outing does not open", starter.c_str(),
                             MonsterStorage::kPartySize );
            return false;
        }
        return true;
    }

    bool MonsterCollectorGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireRequired( _elementChart.path(), _pChart ) == false )
            return false;
        _random.setSeed( _seed );
        hashed_string speciesID;
        int32         level       = 0;
        GameRandom    probeRandom = _random; // 조우 표에 이 지역 · 시간대의 줄이 있는지만 본다 — 판의 난수는 쓰지 않는다
        if ( _listStarter.empty() || _pCatalog->rollEncounter( _area, _timeOfDay, probeRandom, speciesID, level ) == false )
        {
            SW_LOG_ERROR_ID( "MonsterCollector.SetupIncomplete", "Monster collector game mode: starters (%#) and an encounter table of area '%#' at time '%#' are required - the outing does not open",
                             _listStarter.size(), _area.c_str(), _timeOfDay.c_str() );
            return false;
        }
        if ( openStorage() == false )
            return false;
        _battle = MonsterBattle{};
        _encounterCounter.reset();
        _encounterCounter.restartGrace(); // 첫 걸음에 조우하지 않는다
        _playerPosition     = _startPosition;
        _commandMove        = float2{ 0.0f, 0.0f };
        _remainingBallCount = _ballCount;
        return true;
    }

    void MonsterCollectorGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "MonsterCollector.Started", "[MonsterCollector] %# in the party, %# balls, catch %# - the input arrives as commands (move, attack, ball, switch, run, rest)", getPartySize(), _remainingBallCount,
                        _captureGoal );
    }

    const MonsterInstance* MonsterCollectorGameModeComponent::findLead() const
    {
        if ( isStarted() == false || _storage.getParty().empty() )
            return nullptr;
        return _phase == MonsterCollectorPhase::Battle ? &_battle.getActive( MonsterBattle::kPlayerSide ) : &_storage.getParty().front();
    }

    int32 MonsterCollectorGameModeComponent::getFoeHP() const
    {
        return _phase == MonsterCollectorPhase::Battle ? _battle.getActive( MonsterBattle::kFoeSide )._hp : 0;
    }

    int32 MonsterCollectorGameModeComponent::getLeaderXP() const
    {
        return _storage.getParty().empty() ? 0 : static_cast<int32>( _storage.getParty().front()._exp );
    }

    bool MonsterCollectorGameModeComponent::isNearRest() const
    {
        const float32 deltaX = _restPosition._x - _playerPosition._x;
        const float32 deltaY = _restPosition._y - _playerPosition._y;
        return deltaX * deltaX + deltaY * deltaY <= _useRadius * _useRadius;
    }

    bool MonsterCollectorGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPlayerPosition()._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( getPlayerPosition() );
        return isStarted();
    }

    bool MonsterCollectorGameModeComponent::submitMove( float2 move )
    {
        if ( isStarted() == false || _phase != MonsterCollectorPhase::Explore )
            return false;
        _commandMove = move;
        return true;
    }

    bool MonsterCollectorGameModeComponent::submitAttack( int32 moveSlot )
    {
        return playRound( MonsterAction::makeMove( moveSlot ) );
    }

    bool MonsterCollectorGameModeComponent::submitBall()
    {
        if ( _remainingBallCount <= 0 || playRound( MonsterAction::makeBall( _ballMultiplier ) ) == false )
            return false;
        --_remainingBallCount;
        return true;
    }

    bool MonsterCollectorGameModeComponent::submitSwitch( int32 partyIndex )
    {
        if ( isAwaitingSwitch() )
            return _battle.switchFainted( MonsterBattle::kPlayerSide, partyIndex );
        return playRound( MonsterAction::makeSwitch( partyIndex ) );
    }

    bool MonsterCollectorGameModeComponent::submitRun()
    {
        return playRound( MonsterAction::makeRun() );
    }

    bool MonsterCollectorGameModeComponent::submitRest()
    {
        const bool bCannotRest = isStarted() == false || _phase != MonsterCollectorPhase::Explore || isNearRest() == false;
        if ( bCannotRest )
            return false;
        _storage.restoreParty();
        _remainingBallCount = MathUtil::max( _remainingBallCount, _ballCount );
        _encounterCounter.restartGrace();
        getSoundQueue().queueEvent( MonsterCollectorGameModeComponentInternal::kEventRest );
        SW_LOG_INFO_ID( "MonsterCollector.Rested", "[MonsterCollector] the party rested, %# balls", _remainingBallCount );
        return true;
    }

    void MonsterCollectorGameModeComponent::updateExplore( float32 deltaTime )
    {
        const float2 before = _playerPosition;
        const float2 step   = WalkSweep::walkOnGround( getObjectManager(), getPlayerPosition(), _commandMove * ( _moveSpeed * deltaTime ), _bodyRadius );
        _playerPosition._x  = MathUtil::clamp( before._x + step._x, _fieldMin._x, _fieldMax._x );
        _playerPosition._y  = MathUtil::clamp( before._y + step._y, _fieldMin._y, _fieldMax._y );

        const float32 walkedX = _playerPosition._x - before._x;
        const float32 walkedY = _playerPosition._y - before._y;
        _strideProgress += MathUtil::sqrt( walkedX * walkedX + walkedY * walkedY );
        const StepEncounterRule rule{ StepEncounterMode::Chance, _encounterRate, _graceSteps };
        while ( _stepLength <= _strideProgress )
        {
            _strideProgress -= _stepLength;
            _encounterCounter.advance();
            if ( _encounterCounter.roll( rule, &_random ) == false )
                continue;
            _encounterCounter.markEncounter();
            openBattle();
            return;
        }
    }

    void MonsterCollectorGameModeComponent::openBattle()
    {
        hashed_string speciesID;
        int32         level = 0;
        if ( _pCatalog->rollEncounter( _area, _timeOfDay, _random, speciesID, level ) == false )
            return; // 다시 읽은 카탈로그가 조우 표를 지웠다
        vector<MonsterInstance> listFoe;
        listFoe.push_back( MonsterInstance::createMonster( *_pCatalog, speciesID, level, _random ) );
        _commandMove    = float2{ 0.0f, 0.0f };
        _strideProgress = 0.0f;
        ++_battleCount;
        _battle.initialize( *_pCatalog, *_pChart, GameRandom::deriveSeed( _seed, static_cast<uint32>( _battleCount ) ) );
        _battle.start( _storage.getParty(), listFoe, true );
        _phase = MonsterCollectorPhase::Battle;
        getSoundQueue().queueEvent( MonsterCollectorGameModeComponentInternal::kEventEncounter );
        SW_LOG_INFO_ID( "MonsterCollector.BattleStarted", "[MonsterCollector] battle %# - a wild '%#' of level %#", _battleCount, speciesID.c_str(), level );
        settleBattle(); // 모두 쓰러진 파티로 열린 전투는 바로 끝난다
    }

    optional<int32> MonsterCollectorGameModeComponent::findFoeMoveSlot() const
    {
        const MonsterInstance& foe    = _battle.getActive( MonsterBattle::kFoeSide );
        const MonsterInstance& player = _battle.getActive( MonsterBattle::kPlayerSide );
        optional<int32>        bestSlot;
        float32                bestDamage = 0.0f;
        for ( int32 slot = 0; slot < MonsterInstance::kMoveSlotCount; ++slot )
        {
            const MonsterMoveSlot& move = foe._arrMove[slot];
            if ( move.isEmpty() || move._pp <= 0 )
                continue;
            const float32 damage = _battle.computeExpectedDamage( MonsterBattle::kFoeSide, foe, move._moveID, MonsterBattle::kPlayerSide, player );
            if ( bestSlot.has_value() && damage <= bestDamage )
                continue;
            bestSlot   = slot;
            bestDamage = damage;
        }
        return bestSlot;
    }

    bool MonsterCollectorGameModeComponent::playRound( const MonsterAction& action )
    {
        const bool bNoCommand = isStarted() == false || _phase != MonsterCollectorPhase::Battle || isAwaitingSwitch();
        if ( bNoCommand || _battle.setAction( MonsterBattle::kPlayerSide, action ) == false )
            return false;
        const optional<int32> foeSlot = findFoeMoveSlot();
        if ( foeSlot.has_value() )
            (void)_battle.setAction( MonsterBattle::kFoeSide, MonsterAction::makeMove( *foeSlot ) ); // 빈 슬롯이 아닌 것을 골랐다
        _battle.resolveRound();
        settleBattle();
        return true;
    }

    void MonsterCollectorGameModeComponent::queueBattleSound( const MonsterBattleEvent& event )
    {
        if ( const utf8* const pSound = MonsterCollectorGameModeComponentInternal::findSound( event._kind ) )
            getSoundQueue().queueEvent( pSound );
        const MonsterMoveDef* const pMove = event._kind == MonsterBattleEvent::Kind::MoveUsed ? _pCatalog->findMove( event._id ) : nullptr;
        if ( pMove != nullptr && pMove->_useSound.empty() == false )
            getSoundQueue().queueEvent( pMove->_useSound.c_str() ); // 이름 표의 글은 사라지지 않는다
    }

    void MonsterCollectorGameModeComponent::settleBattle()
    {
        _listBattleEvent.clear();
        _battle.pollEvents( _listBattleEvent );
        for ( const MonsterBattleEvent& event : _listBattleEvent )
        {
            queueBattleSound( event );
            if ( event._kind == MonsterBattleEvent::Kind::Fainted )
                SW_LOG_INFO_ID( "MonsterCollector.Fainted", "[MonsterCollector] '%#' of side %# fainted", event._id.c_str(), event._side );
            else if ( event._kind == MonsterBattleEvent::Kind::LevelUp )
                SW_LOG_INFO_ID( "MonsterCollector.LevelUp", "[MonsterCollector] the lead reached level %#", event._value );
            else if ( event._kind == MonsterBattleEvent::Kind::BrokeFree )
                SW_LOG_INFO_ID( "MonsterCollector.BrokeFree", "[MonsterCollector] '%#' broke free after %# shakes", event._id.c_str(), event._value );
        }
        const MonsterBattleResult result = _battle.getResult();
        if ( result == MonsterBattleResult::Ongoing )
            return;
        _storage.getParty() = _battle.getParty( MonsterBattle::kPlayerSide ); // 전투는 사본으로 싸운다 — HP · PP · 경험치를 돌려받는다
        evolveParty();
        if ( result == MonsterBattleResult::Lost )
        {
            finishGame( MonsterCollectorPhase::Defeat );
            return;
        }
        _phase = MonsterCollectorPhase::Explore;
        if ( result != MonsterBattleResult::Captured )
            return;
        ++_captureCount;
        if ( _storage.add( _battle.getCaptured() ) == MonsterStoragePlace::Full )
            SW_LOG_WARNING_ID( "MonsterCollector.StorageFull", "[MonsterCollector] the party and the box are full - '%#' goes free", _battle.getCaptured()._speciesID.c_str() );
        SW_LOG_INFO_ID( "MonsterCollector.Captured", "[MonsterCollector] caught '%#' (%# of %#)", _battle.getCaptured()._speciesID.c_str(), _captureCount, _captureGoal );
        if ( _captureGoal <= _captureCount )
            finishGame( MonsterCollectorPhase::Victory );
    }

    void MonsterCollectorGameModeComponent::evolveParty()
    {
        for ( MonsterInstance& monster : _storage.getParty() )
        {
            const hashed_string targetID = MonsterInstance::findEvolution( *_pCatalog, monster, hashed_string{} );
            if ( targetID.empty() || MonsterInstance::evolve( *_pCatalog, monster, targetID ) == false )
                continue;
            getSoundQueue().queueEvent( MonsterCollectorGameModeComponentInternal::kEventEvolved );
            SW_LOG_INFO_ID( "MonsterCollector.Evolved", "[MonsterCollector] a party monster evolved into '%#'", targetID.c_str() );
        }
    }

    void MonsterCollectorGameModeComponent::finishGame( MonsterCollectorPhase phase )
    {
        _phase = phase;
        endMatch( hashed_string( phase == MonsterCollectorPhase::Victory ? kWinResult : kLoseResult ) );
        getSoundQueue().queueEvent( phase == MonsterCollectorPhase::Victory ? MonsterCollectorGameModeComponentInternal::kEventWon : MonsterCollectorGameModeComponentInternal::kEventLost );
        if ( phase == MonsterCollectorPhase::Victory )
            SW_LOG_INFO_ID( "MonsterCollector.Victory", "[MonsterCollector] %# monsters caught in %# battles", _captureCount, _battleCount );
        else
            SW_LOG_INFO_ID( "MonsterCollector.Defeat", "[MonsterCollector] the party fell" );
    }

    void MonsterCollectorGameModeComponent::updateMatch( float32 deltaTime )
    {
        if ( _phase == MonsterCollectorPhase::Explore )
            updateExplore( deltaTime ); // 전투는 명령이 진행한다
    }

    void MonsterCollectorGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _phase != MonsterCollectorPhase::Defeat )
            addMoneyResult( outListValue, static_cast<int64>( _captureCount ) * _captureReward );
    }

    void MonsterCollectorGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( MonsterCollectorGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 자리 · 볼 · 잡은 수 · 단계는 상태 칸이다(`GameModeComponent::serializeState`). 규칙 객체는 같은 카탈로그로 연 뒤에 읽는다(키트 계약).
        // 전투는 전투 중일 때만 싣는다 — 다음 전투는 시작할 때 새로 열고 시드는 연 전투 수가 정한다.
        const bool bStorage = io.valueOpened( _storage, [this]( MonsterStorage& storage )
        {
            if ( _pCatalog != nullptr && _pChart != nullptr && storage.initialize( _pCatalog.get(), _boxCapacity ) )
                return true;
            SW_LOG_ERROR_ID( "MonsterCollector.StateNeedsCatalog", "Monster collector game mode: state read before the catalog and the element chart are loaded - refused" );
            return false;
        } );
        if ( bStorage == false )
            return;
        const bool bInBattle  = _phase == MonsterCollectorPhase::Battle;
        const auto openBattle = [this]( MonsterBattle& battle )
        {
            battle.initialize( *_pCatalog, *_pChart, _seed );
            return true;
        };
        if ( bInBattle )
        {
            if ( io.valueOpened( _battle, openBattle ) == false )
                return;
        }
        else if ( io.isReading() )
        {
            MonsterBattle battle;
            (void)openBattle( battle ); // 늘 열린다
            _battle = std::move( battle );
        }
        int32 stepsSinceEncounter = _encounterCounter.getStepsSinceEncounter();
        int32 totalSteps          = _encounterCounter.getTotalSteps();
        io.value( _random );
        io.value( stepsSinceEncounter );
        io.value( totalSteps );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        const bool bBattleOpen = _battle.getResult() == MonsterBattleResult::Ongoing && _battle.getParty( MonsterBattle::kPlayerSide ).empty() == false &&
                                 _battle.getParty( MonsterBattle::kFoeSide ).empty() == false;
        io.check( _storage.getParty().empty() == false && ( bInBattle == false || bBattleOpen ) && _encounterCounter.restore( stepsSinceEncounter, totalSteps ) );
    }
} // namespace sw
