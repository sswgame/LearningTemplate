#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Component/MonsterHuntGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"

namespace sw
{
    namespace
    {
        struct MonsterHuntGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "MHGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( monsterHuntPhase, "MonsterHunt.Phase", "Monster hunt phase (0 haggle, 1 investigate, 2 hunt, 3 victory, 4 defeat)", MonsterHuntGameModeComponent,
                                &MonsterHuntGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntReward, "MonsterHunt.Reward", "Current offer, or the settled reward once the haggling closed", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getReward );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntClues, "MonsterHunt.FoundClues", "Clues investigated", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getFoundClueCount );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntKnowledge, "MonsterHunt.Knowledge", "Bestiary knowledge level of the hunted monster", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getKnowledge );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntPlayerHP, "MonsterHunt.PlayerHP", "HP of the hunter", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntMonsterHP, "MonsterHunt.MonsterHP", "HP of the monster", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getMonsterHP );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntOilHits, "MonsterHunt.OilHits", "Hits left on the blade oil", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getOilHits );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntToxicity, "MonsterHunt.Toxicity", "Potion toxicity", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getToxicity );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntAttackReady, "MonsterHunt.AttackReady", "1 when the attack cooldown is over", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getAttackReady );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntInReach, "MonsterHunt.MonsterInReach", "1 when the monster is within the attack reach", MonsterHuntGameModeComponent,
                                &MonsterHuntGameModeComponent::getMonsterInReach );
    SW_AUTOMATION_GETTER_WATCH( monsterHuntPlayerX, "MonsterHunt.PlayerX", "Absolute ground-plane X (m) of the hunter", MonsterHuntGameModeComponent, &MonsterHuntGameModeComponent::getPlayerX );

    MonsterHuntGameModeComponent::MonsterHuntGameModeComponent()
        : _contract{}
        , _monster{}
        , _askStep{ 30 }
        , _elementChart{}
        , _oil{}
        , _potion{}
        , _sign{}
        , _bagSlotCount{ 8 }
        , _startPosition{ 0.0f, 0.0f }
        , _spawnPosition{ 16.0f, 0.0f }
        , _fieldMin{ -2.0f, -4.0f }
        , _fieldMax{ 20.0f, 4.0f }
        , _senseRadius{ 3.0f }
        , _moveSpeed{ 4.0f }
        , _playerMaxHP{ 100 }
        , _fastDamage{ 20.0f }
        , _strongDamage{ 35.0f }
        , _attackReach{ 2.0f }
        , _signReach{ 5.0f }
        , _attackCooldownSeconds{ 0.4f }
        , _dodgeSeconds{ 0.6f }
        , _potionHealPerSecond{ 2.0f }
        , _monsterMaxHP{ 120 }
        , _monsterDamage{ 15 }
        , _monsterReach{ 1.5f }
        , _monsterSpeed{ 3.0f }
        , _monsterAttackSeconds{ 2.0f }
        , _seed{ 7 }
        , _resultHoldSeconds{ 3.0f }
        , _pCatalog{}
        , _pChart{}
        , _haggle{}
        , _investigation{}
        , _bestiary{}
        , _combat{}
        , _alchemy{}
        , _bag{}
        , _listInvestigationEvent{}
        , _listBestiaryEvent{}
        , _attackCooldown{}
        , _dodge{}
        , _monsterAttack{}
        , _resultHold{}
        , _playerPosition{ 0.0f, 0.0f }
        , _monsterPosition{ 0.0f, 0.0f }
        , _commandMove{ 0.0f, 0.0f }
        , _healProgress{ 0.0f }
        , _foundClueCount{ 0 }
        , _playerHP{ 0 }
        , _monsterHP{ 0 }
        , _phase{ MonsterHuntPhase::Haggle }
    {
    }

    MonsterHuntGameModeComponent::~MonsterHuntGameModeComponent() = default;

    bool MonsterHuntGameModeComponent::validateSetup() const
    {
        const bool bContractKnown = _pCatalog->findContract( _contract.key() ) != nullptr && _pCatalog->findMonster( _monster.key() ) != nullptr;
        const bool bGearKnown     = _pCatalog->findAlchemy( _oil.key() ) != nullptr && _pCatalog->findAlchemy( _potion.key() ) != nullptr && _pCatalog->findSign( _sign.key() ) != nullptr;
        if ( bContractKnown && bGearKnown )
            return true;
        SW_LOG_ERROR_ID( "MonsterHunt.SetupIncomplete", "Monster hunt game mode: contract '%#', monster '%#', oil '%#', potion '%#' and sign '%#' must all be in the catalog - the contract does not open",
                         _contract.c_str(), _monster.c_str(), _oil.c_str(), _potion.c_str(), _sign.c_str() );
        return false;
    }

    bool MonsterHuntGameModeComponent::openBag()
    {
        if ( _bag.initialize( nullptr, _bagSlotCount ) == false )
            return false;
        _alchemy.initialize( _pCatalog.get(), nullptr );
        const vector<hashed_string>& listAlcohol = _pCatalog->getAlchemy()._listAlcohol;
        const bool                   bPacked     = _bag.addItem( _oil.key(), 1 ) == 1 && _bag.addItem( _potion.key(), 1 ) == 1 && listAlcohol.empty() == false && _bag.addItem( listAlcohol.front(), 1 ) == 1;
        if ( bPacked == false )
            SW_LOG_ERROR_ID( "MonsterHunt.BagNotPacked", "Monster hunt game mode: oil '%#', potion '%#' and the first alcohol of the catalog do not fit %# slots - the contract does not open", _oil.c_str(),
                             _potion.c_str(), _bagSlotCount );
        return bPacked;
    }

    bool MonsterHuntGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireRequired( _elementChart.path(), _pChart ) == false || validateSetup() == false )
            return false;
        const bool bOpened = _haggle.initialize( _pCatalog.get(), _contract.key() ) && _investigation.initialize( _pCatalog.get(), _contract.key(), nullptr ) &&
                             _combat.initialize( _pCatalog.get(), _seed ) && openBag();
        if ( bOpened == false )
            return false;
        _bestiary.initialize( _pCatalog.get(), _pChart.get() );
        _attackCooldown.clear();
        _dodge.clear();
        _monsterAttack.clear();
        _resultHold.clear();
        _playerPosition  = _startPosition;
        _monsterPosition = _spawnPosition;
        _commandMove     = float2{ 0.0f, 0.0f };
        _playerHP        = _playerMaxHP;
        return true;
    }

    void MonsterHuntGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "MonsterHunt.Started", "[MonsterHunt] contract '%#' offers %# - the input arrives as commands (raise, accept, move, investigate, attack, dodge, sign, oil, potion)", _contract.c_str(),
                        _haggle.getOffer() );
    }

    float32 MonsterHuntGameModeComponent::computeMonsterDistance() const
    {
        const float32 deltaX = _monsterPosition._x - _playerPosition._x;
        const float32 deltaY = _monsterPosition._y - _playerPosition._y;
        return MathUtil::sqrt( deltaX * deltaX + deltaY * deltaY );
    }

    void MonsterHuntGameModeComponent::collectNearbyClues( vector<const MonsterHuntClueDef*>& outListClue ) const
    {
        _investigation.senseClues( getPlayerPosition()._value, _senseRadius, outListClue );
    }

    bool MonsterHuntGameModeComponent::isWeaknessKnown() const
    {
        vector<const MonsterHuntWeakness*> listWeakness;
        _bestiary.collectKnownWeaknesses( _monster.key(), listWeakness );
        return listWeakness.empty() == false;
    }

    bool MonsterHuntGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPlayerPosition()._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( getPlayerPosition() );
        return isStarted();
    }

    bool MonsterHuntGameModeComponent::submitRaise()
    {
        if ( isStarted() == false || _phase != MonsterHuntPhase::Haggle )
            return false;
        const MonsterHuntHaggleResult result = _haggle.propose( _haggle.getOffer() + _askStep );
        SW_LOG_INFO_ID( "MonsterHunt.Haggled", "[MonsterHunt] haggling result %#, offer %#", static_cast<int32>( result ), getReward() );
        beginInvestigation();
        return true;
    }

    bool MonsterHuntGameModeComponent::submitAccept()
    {
        if ( isStarted() == false || _phase != MonsterHuntPhase::Haggle )
            return false;
        (void)_haggle.acceptOffer(); // 받을 보상은 `getFinalReward` 로 읽는다
        beginInvestigation();
        return true;
    }

    void MonsterHuntGameModeComponent::beginInvestigation()
    {
        if ( _haggle.isClosed() == false )
            return;
        _phase = MonsterHuntPhase::Investigate;
        if ( _alchemy.meditate( _bag ) == false )
            SW_LOG_WARNING_ID( "MonsterHunt.NoAlcohol", "[MonsterHunt] no alcohol in the bag - oil and potion stay empty" );
        SW_LOG_INFO_ID( "MonsterHunt.ContractTaken", "[MonsterHunt] contract taken for %#", _haggle.getFinalReward() );
        if ( _investigation.isSolved() )
            beginHunt(); // 단서 없는 의뢰
    }

    bool MonsterHuntGameModeComponent::submitMove( float2 move )
    {
        if ( isStarted() == false || isAlive() == false )
            return false;
        _commandMove = move;
        return true;
    }

    bool MonsterHuntGameModeComponent::submitInvestigate()
    {
        if ( isStarted() == false || _phase != MonsterHuntPhase::Investigate )
            return false;
        vector<const MonsterHuntClueDef*> listClue;
        collectNearbyClues( listClue );
        for ( const MonsterHuntClueDef* pClue : listClue )
        {
            if ( _investigation.investigate( pClue->_id, getPlayerPosition()._value ) != MonsterHuntClueResult::Found )
                continue; // 감각 안이지만 아직 멀다
            ++_foundClueCount;
            reportEvents();
            if ( _investigation.isSolved() )
                beginHunt();
            return true;
        }
        return false;
    }

    void MonsterHuntGameModeComponent::beginHunt()
    {
        _phase           = MonsterHuntPhase::Hunt;
        _monsterHP       = _monsterMaxHP;
        _monsterPosition = _spawnPosition;
        _monsterAttack.start( _monsterAttackSeconds );
        (void)_bestiary.investigate( _monster.key() ); // 이미 아는 단계면 그대로다
        reportEvents();
        SW_LOG_INFO_ID( "MonsterHunt.HuntStarted", "[MonsterHunt] '%#' comes out of its lair", _monster.c_str() );
    }

    bool MonsterHuntGameModeComponent::submitAttack( bool bStrong )
    {
        if ( isStarted() == false || _phase != MonsterHuntPhase::Hunt || _attackCooldown.isActive() )
            return false;
        if ( _combat.performAction( bStrong ? MonsterHuntAction::StrongAttack : MonsterHuntAction::FastAttack ) != MonsterHuntCombatResult::Ok )
            return false;
        _attackCooldown.start( _attackCooldownSeconds );
        if ( computeMonsterDistance() > _attackReach )
            return true; // 빗나갔다 — 오일 횟수는 쓰지 않는다
        const float32       baseDamage = ( bStrong ? _strongDamage : _fastDamage ) * _combat.computeDamageScale();
        const hashed_string element    = _alchemy.consumeOilHit();
        _combat.registerHitLanded();
        damageMonster( baseDamage, element );
        return true;
    }

    bool MonsterHuntGameModeComponent::submitDodge()
    {
        if ( isStarted() == false || _phase != MonsterHuntPhase::Hunt || _combat.performAction( MonsterHuntAction::Dodge ) != MonsterHuntCombatResult::Ok )
            return false;
        _dodge.start( _dodgeSeconds );
        return true;
    }

    bool MonsterHuntGameModeComponent::submitSign()
    {
        if ( isStarted() == false || _phase != MonsterHuntPhase::Hunt || computeMonsterDistance() > _signReach )
            return false;
        const MonsterHuntSignCast cast = _combat.castSign( _sign.key(), false, nullptr, StatBlock{}, _pChart.get() );
        if ( cast._result != MonsterHuntCombatResult::Ok )
            return false;
        damageMonster( cast._power, cast._element );
        return true;
    }

    bool MonsterHuntGameModeComponent::submitOil()
    {
        return isStarted() && isAlive() && _alchemy.applyOil( _oil.key(), _bag ) == MonsterHuntUseResult::Ok;
    }

    bool MonsterHuntGameModeComponent::submitPotion()
    {
        return isStarted() && isAlive() && _alchemy.drink( _potion.key(), _bag ) == MonsterHuntUseResult::Ok;
    }

    void MonsterHuntGameModeComponent::damageMonster( float32 baseDamage, const hashed_string& element )
    {
        const int32 damage = static_cast<int32>( MathUtil::floor( baseDamage * _bestiary.computeMultiplier( _monster.key(), element ) ) );
        if ( DepletingValue::take( _monsterHP, damage )._bDepleted == SW_FALSE )
            return;
        (void)_bestiary.recordKill( _monster.key() ); // 지식이 오르지 않아도 처치다
        reportEvents();
        finishGame( MonsterHuntPhase::Victory );
    }

    void MonsterHuntGameModeComponent::reportEvents()
    {
        _listInvestigationEvent.clear();
        _investigation.pollEvents( _listInvestigationEvent );
        for ( const MonsterHuntInvestigationEvent& event : _listInvestigationEvent )
        {
            if ( event._kind == MonsterHuntInvestigationEvent::Kind::ClueFound )
                SW_LOG_INFO_ID( "MonsterHunt.ClueFound", "[MonsterHunt] clue '%#' of step '%#'", event._clueID.c_str(), event._stepID.c_str() );
        }
        _listBestiaryEvent.clear();
        _bestiary.pollEvents( _listBestiaryEvent );
        for ( const MonsterHuntBestiaryEvent& event : _listBestiaryEvent )
        {
            if ( event._kind == MonsterHuntBestiaryEvent::Kind::WeaknessRevealed )
                SW_LOG_INFO_ID( "MonsterHunt.WeaknessRevealed", "[MonsterHunt] '%#' is weak to '%#'", event._monsterID.c_str(), event._weaknessID.c_str() );
        }
    }

    void MonsterHuntGameModeComponent::updateHunt( float32 deltaTime )
    {
        _combat.update( deltaTime );
        _alchemy.update( deltaTime );
        (void)_attackCooldown.update( deltaTime ); // 끝났는지는 `isActive` 로 본다
        (void)_dodge.update( deltaTime );
        if ( _alchemy.isEffectActive( _potion.key() ) )
        {
            _healProgress += _potionHealPerSecond * deltaTime;
            const float32 wholeHeal = MathUtil::floor( _healProgress );
            _healProgress -= wholeHeal;
            _playerHP = MathUtil::min( _playerMaxHP, _playerHP + static_cast<int32>( wholeHeal ) );
        }
        const float32 distance = computeMonsterDistance();
        const float32 approach = MathUtil::min( _monsterSpeed * deltaTime, distance - _monsterReach );
        if ( approach > 0.0f )
        {
            _monsterPosition._x += ( _playerPosition._x - _monsterPosition._x ) / distance * approach;
            _monsterPosition._y += ( _playerPosition._y - _monsterPosition._y ) / distance * approach;
        }
        const bool bInReach = computeMonsterDistance() <= _monsterReach + MathUtil::kLooseEpsilon;
        if ( _monsterAttack.updateRepeat( deltaTime, _monsterAttackSeconds, bInReach ) == false || _dodge.isActive() )
            return;
        _combat.registerHitTaken();
        if ( DepletingValue::take( _playerHP, _monsterDamage )._bDepleted == SW_TRUE )
            finishGame( MonsterHuntPhase::Defeat );
    }

    void MonsterHuntGameModeComponent::finishGame( MonsterHuntPhase phase )
    {
        _phase = phase;
        _resultHold.start( _resultHoldSeconds );
        if ( phase == MonsterHuntPhase::Victory )
            SW_LOG_INFO_ID( "MonsterHunt.Victory", "[MonsterHunt] '%#' is slain - the reward is %#", _monster.c_str(), _haggle.getFinalReward() );
        else
            SW_LOG_INFO_ID( "MonsterHunt.Defeat", "[MonsterHunt] the hunter fell" );
    }

    void MonsterHuntGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _phase == MonsterHuntPhase::Haggle )
            return; // 흥정은 명령이 진행한다
        if ( isAlive() )
        {
            _playerPosition._x = MathUtil::clamp( _playerPosition._x + _commandMove._x * _moveSpeed * deltaTime, _fieldMin._x, _fieldMax._x );
            _playerPosition._y = MathUtil::clamp( _playerPosition._y + _commandMove._y * _moveSpeed * deltaTime, _fieldMin._y, _fieldMax._y );
            if ( _phase == MonsterHuntPhase::Hunt )
                updateHunt( deltaTime );
            return;
        }
        if ( _resultHold.update( deltaTime ) )
        {
            const bool bWon = _phase == MonsterHuntPhase::Victory;
            (void)finishMinigame( hashed_string( bWon ? MonsterHuntGameModeComponentInternal::kWinResult : MonsterHuntGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
        }
    }

    void MonsterHuntGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`MonsterHuntAvatarComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void MonsterHuntGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _phase == MonsterHuntPhase::Victory )
            addMoneyResult( outListValue, _haggle.getFinalReward() );
    }

    void MonsterHuntGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( MonsterHuntGameModeComponentInternal::kStateTag, MonsterHuntGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 자리 · 체력 · 단계는 상태 칸이다(`GameModeComponent::serializeState`). 규칙 객체는 같은 카탈로그로 연 뒤에 읽는다(키트 계약).
        MonsterHuntHaggle        restoredHaggle;
        MonsterHuntInvestigation restoredInvestigation;
        MonsterHuntBestiary      restoredBestiary;
        MonsterHuntCombat        restoredCombat;
        MonsterHuntAlchemy       restoredAlchemy;
        Inventory                restoredBag;
        Countdown                attackCooldown = _attackCooldown;
        Countdown                dodge          = _dodge;
        Countdown                monsterAttack  = _monsterAttack;
        Countdown                resultHold     = _resultHold;
        if ( io.isReading() )
        {
            const bool bReady = _pCatalog != nullptr && _pChart != nullptr && restoredHaggle.initialize( _pCatalog.get(), _contract.key() ) &&
                                restoredInvestigation.initialize( _pCatalog.get(), _contract.key(), nullptr ) && restoredCombat.initialize( _pCatalog.get(), _seed ) &&
                                restoredBag.initialize( nullptr, _bagSlotCount );
            if ( bReady == false )
            {
                SW_LOG_ERROR_ID( "MonsterHunt.StateNeedsCatalog", "Monster hunt game mode: state read before the catalog and the element chart are loaded - refused" );
                io.fail();
                return;
            }
            restoredBestiary.initialize( _pCatalog.get(), _pChart.get() );
            restoredAlchemy.initialize( _pCatalog.get(), nullptr );
        }
        io.value( io.isWriting() ? _haggle : restoredHaggle );
        io.value( io.isWriting() ? _investigation : restoredInvestigation );
        io.value( io.isWriting() ? _bestiary : restoredBestiary );
        io.value( io.isWriting() ? _combat : restoredCombat );
        io.value( io.isWriting() ? _alchemy : restoredAlchemy );
        io.value( io.isWriting() ? _bag : restoredBag );
        io.value( attackCooldown );
        io.value( dodge );
        io.value( monsterAttack );
        io.value( resultHold );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        const bool bHealthValid = _playerHP <= _playerMaxHP && _monsterHP <= _monsterMaxHP;
        const bool bPhaseValid  = ( _phase == MonsterHuntPhase::Haggle ) != restoredHaggle.isClosed();
        if ( io.check( bHealthValid && bPhaseValid ) == false )
            return;
        _haggle         = restoredHaggle;
        _investigation  = std::move( restoredInvestigation );
        _bestiary       = std::move( restoredBestiary );
        _combat         = restoredCombat;
        _alchemy        = std::move( restoredAlchemy );
        _bag            = std::move( restoredBag );
        _attackCooldown = attackCooldown;
        _dodge          = dodge;
        _monsterAttack  = monsterAttack;
        _resultHold     = resultHold;
    }
} // namespace sw
