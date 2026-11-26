#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/Component/CardBattleGameModeComponent.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct CardBattleGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "CBGM" };

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventPlay    = "CardBattlePlay";
            static constexpr const utf8* kEventHit     = "CardBattleHit";
            static constexpr const utf8* kEventBlock   = "CardBattleBlock";
            static constexpr const utf8* kEventEndTurn = "CardBattleEndTurn";
            static constexpr const utf8* kEventHurt    = "CardBattleHurt";
            static constexpr const utf8* kEventRefused = "CardBattleRefused";
            static constexpr const utf8* kEventWon     = "CardBattleWon";
            static constexpr const utf8* kEventLost    = "CardBattleLost";
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( cardBattlePhase, "CardBattle.Phase", "Deck battle phase (0 player turn, 1 won, 2 lost)", CardBattleGameModeComponent, &CardBattleGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( cardBattleTurn, "CardBattle.Turn", "Deck battle turn (1 = first)", CardBattleGameModeComponent, &CardBattleGameModeComponent::getTurn );
    SW_AUTOMATION_GETTER_WATCH( cardBattlePlayerHP, "CardBattle.PlayerHP", "Player HP", CardBattleGameModeComponent, &CardBattleGameModeComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( cardBattleEnemyHP, "CardBattle.EnemyHP", "Enemy HP", CardBattleGameModeComponent, &CardBattleGameModeComponent::getEnemyHP );
    SW_AUTOMATION_GETTER_WATCH( cardBattleHand, "CardBattle.HandCount", "Cards in hand", CardBattleGameModeComponent, &CardBattleGameModeComponent::getHandCount );
    SW_AUTOMATION_GETTER_WATCH( cardBattleEnergy, "CardBattle.Energy", "Energy left this turn", CardBattleGameModeComponent, &CardBattleGameModeComponent::getEnergy );

    CardBattleGameModeComponent::CardBattleGameModeComponent()
        : _deckID{}
        , _enemyID{}
        , _battleSettings{}
        , _seed{ GameRandom::kDefaultSeed }
        , _autoPlayInterval{ 0.5f }
        , _winReward{ 0 }
        , _pCatalog{}
        , _battle{}
        , _autoPlayTimer{}
    {
    }

    CardBattleGameModeComponent::~CardBattleGameModeComponent() = default;

    bool CardBattleGameModeComponent::openBattle( CardBattle& outBattle ) const
    {
        const CardBattleDeckDef* const  pDeck  = _pCatalog->findDeck( _deckID );
        const CardBattleEnemyDef* const pEnemy = _pCatalog->findEnemy( _enemyID );
        if ( pDeck == nullptr || pEnemy == nullptr )
        {
            SW_LOG_ERROR_ID( "CardBattle.DeckOrEnemyMissing", "Card battle game mode: deck '%#' or enemy '%#' is not in the catalog - the battle does not open", _deckID.c_str(), _enemyID.c_str() );
            return false;
        }
        CardBattleEnemy enemy;
        enemy._listIntent = pEnemy->_listIntent;
        enemy._hp         = pEnemy->_hp;
        if ( outBattle.initialize( *_pCatalog, _battleSettings, pDeck->_listCard, enemy, _seed ) )
            return true;
        SW_LOG_ERROR_ID( "CardBattle.DeckNotOpened", "Card battle game mode: deck '%#' has no card or an unknown one - the battle does not open", _deckID.c_str() );
        return false;
    }

    bool CardBattleGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false )
            return false;
        CardBattle battle;
        if ( openBattle( battle ) == false )
            return false;
        _battle = std::move( battle );
        _autoPlayTimer.start( _autoPlayInterval );
        return true;
    }

    void CardBattleGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "CardBattle.Started", "[CardBattle] deck '%#' against '%#' (%# HP) - the player turn waits for commands (play card, end turn)", _deckID.c_str(), _enemyID.c_str(), getEnemyHP() );
    }

    bool CardBattleGameModeComponent::isAwaitingCommand() const
    {
        return isStarted() && _battle.getPhase() == CardBattlePhase::PlayerTurn && isAutoPlayOn() == false;
    }

    bool CardBattleGameModeComponent::isPlayable( int32 handIndex ) const
    {
        if ( isStarted() == false || handIndex < 0 || handIndex >= _battle.getHand().getCount() )
            return false;
        const CardBattleCardDef* const pCard = _battle.getCardDef( _battle.getHand().getAt( handIndex ) );
        return _battle.getPhase() == CardBattlePhase::PlayerTurn && pCard != nullptr && pCard->_cost <= _battle.getEnergy();
    }

    const CardBattleEffect* CardBattleGameModeComponent::findEnemyIntent() const
    {
        const CardBattleEnemy& enemy = _battle.getEnemy();
        if ( enemy._listIntent.empty() )
            return nullptr;
        return &enemy._listIntent[static_cast<size_t>( enemy._intentIndex ) % enemy._listIntent.size()];
    }

    bool CardBattleGameModeComponent::playCard( int32 handIndex )
    {
        if ( isAwaitingCommand() == false )
            return false;
        if ( applyCard( handIndex ) == false )
        {
            getSoundQueue().queueEvent( CardBattleGameModeComponentInternal::kEventRefused );
            return false;
        }
        settle();
        return true;
    }

    bool CardBattleGameModeComponent::endTurn()
    {
        if ( isAwaitingCommand() == false )
            return false;
        applyEndTurn();
        settle();
        return true;
    }

    bool CardBattleGameModeComponent::applyCard( int32 handIndex )
    {
        using Internal            = CardBattleGameModeComponentInternal;
        const int32 enemyHPBefore = getEnemyHP();
        const int32 blockBefore   = _battle.getPlayerBlock();
        if ( _battle.playCard( handIndex ) == false )
            return false;
        getSoundQueue().queueEvent( Internal::kEventPlay );
        if ( getEnemyHP() < enemyHPBefore )
            getSoundQueue().queueEvent( Internal::kEventHit );
        if ( _battle.getPlayerBlock() > blockBefore )
            getSoundQueue().queueEvent( Internal::kEventBlock );
        return true;
    }

    void CardBattleGameModeComponent::applyEndTurn()
    {
        using Internal             = CardBattleGameModeComponentInternal;
        const int32 playerHPBefore = getPlayerHP();
        _battle.endTurn();
        getSoundQueue().queueEvent( getPlayerHP() < playerHPBefore ? Internal::kEventHurt : Internal::kEventEndTurn );
    }

    void CardBattleGameModeComponent::stepAutoPlay()
    {
        for ( int32 handIndex = 0; handIndex < _battle.getHand().getCount(); ++handIndex )
        {
            if ( isPlayable( handIndex ) == false )
                continue;
            (void)applyCard( handIndex ); // 낼 수 있는 카드다
            return;
        }
        applyEndTurn();
    }

    void CardBattleGameModeComponent::settle()
    {
        // 부르는 곳은 모두 플레이어 차례에 한 수를 둔 바로 뒤다(`isAwaitingCommand` · `updateGame` 의 차례 갈래) — 차례가 끝난 수에서 한 번만 여기를 지난다.
        if ( _battle.getPhase() == CardBattlePhase::PlayerTurn )
            return;
        endMatch( hashed_string( _battle.getPhase() == CardBattlePhase::Won ? kWinResult : kLoseResult ) );
        getSoundQueue().queueEvent( _battle.getPhase() == CardBattlePhase::Won ? CardBattleGameModeComponentInternal::kEventWon : CardBattleGameModeComponentInternal::kEventLost );
        SW_LOG_INFO_ID( "CardBattle.Finished", "[CardBattle] %# on turn %# with %# HP left", _battle.getPhase() == CardBattlePhase::Won ? "victory" : "defeat", _battle.getTurn(), _battle.getPlayerHP() );
    }

    void CardBattleGameModeComponent::updateMatch( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        if ( isAutoPlayOn() && _autoPlayTimer.update( frameTime ) )
        {
            stepAutoPlay();
            _autoPlayTimer.start( _autoPlayInterval );
            settle();
        }
    }

    void CardBattleGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _battle.getPhase() == CardBattlePhase::Won )
            addMoneyResult( outListValue, _winReward );
    }

    void CardBattleGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( CardBattleGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 전투는 같은 카탈로그 · 같은 덱으로 연 뒤에 읽는다(키트 계약).
        if ( io.valueOpened( _battle, [this]( CardBattle& battle )
        { return _pCatalog != nullptr && openBattle( battle ); } ) == false )
            return;
        io.value( _autoPlayTimer );
        io.end();
    }
} // namespace sw
