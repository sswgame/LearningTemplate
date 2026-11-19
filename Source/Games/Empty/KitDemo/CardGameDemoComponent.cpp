#include "pch.h"

#include "Games/Empty/KitDemo/CardGameDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct CardGameDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDCG" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr uint32      kSeed          = 7;        ///< 같은 판이 나오게 시드를 고정한다
            static constexpr const utf8* kStrikeCardID  = "strike"; ///< 카탈로그의 공격 카드(비용 1, 피해 6)
            static constexpr int32       kDeckSize      = 5;        ///< 한 턴 뽑기(기본 5)와 같아 첫 턴에 덱이 모두 손에 온다
            static constexpr int32       kEnemyHP       = 30;       ///< 첫 턴 3 장(18)으로는 남고 둘째 턴 2 장(12)에 쓰러진다
            static constexpr int32       kEnemyAttack   = 5;        ///< 첫 턴이 끝날 때 한 번 맞는 피해
            static constexpr int32       kMaxTurns      = 10;       ///< 규칙이 틀려 끝나지 않을 때 멈추는 상한
            static constexpr int32       kFirstHandCard = 0;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoCardGameState, "KitDemo.CardGame.State", "Card game demo state (0 not started, 1 battle run finished)", CardGameDemoComponent,
                                &CardGameDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCardGamePhase, "KitDemo.CardGame.Phase", "Deck battle phase after the run (0 player turn, 1 won, 2 lost)", CardGameDemoComponent,
                                &CardGameDemoComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCardGameTurn, "KitDemo.CardGame.Turn", "Turn the deck battle ended on", CardGameDemoComponent, &CardGameDemoComponent::getTurn );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCardGamePlayerHP, "KitDemo.CardGame.PlayerHP", "Player HP after the deck battle", CardGameDemoComponent, &CardGameDemoComponent::getPlayerHP );

    CardGameDemoComponent::CardGameDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _phase{ 0 }
        , _turn{ 0 }
        , _playerHP{ 0 }
    {
    }

    CardGameDemoComponent::~CardGameDemoComponent() = default;

    bool CardGameDemoComponent::runBattle()
    {
        vector<hashed_string> listDeckCardID;
        for ( int32 index = 0; index < CardGameDemoComponentInternal::kDeckSize; ++index )
        {
            listDeckCardID.push_back( hashed_string( CardGameDemoComponentInternal::kStrikeCardID ) );
        }
        CardBattleEnemy enemy;
        enemy._hp = CardGameDemoComponentInternal::kEnemyHP;
        CardBattleEffect attack;
        attack._kind   = CardBattleEffectKind::Damage;
        attack._amount = CardGameDemoComponentInternal::kEnemyAttack;
        enemy._listIntent.push_back( attack );

        CardBattle battle;
        if ( battle.initialize( _pCatalog.get(), CardBattleSettings{}, listDeckCardID, enemy, CardGameDemoComponentInternal::kSeed ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.CardGameOpenFailed", "Card game demo: the catalog has no '%#' card - the deck is empty and the battle does not open",
                             CardGameDemoComponentInternal::kStrikeCardID );
            return false;
        }
        // 턴마다 손 맨 앞 패를 에너지가 다할 때까지 내고 턴을 마친다 — 덱이 모두 같은 카드라 섞인 순서와 상관없이 결과가 같다.
        for ( int32 turn = 0; turn < CardGameDemoComponentInternal::kMaxTurns && battle.getPhase() == CardBattlePhase::PlayerTurn; ++turn )
        {
            while ( battle.playCard( CardGameDemoComponentInternal::kFirstHandCard ) )
            {
            }
            battle.endTurn();
        }
        _phase    = static_cast<int32>( battle.getPhase() );
        _turn     = battle.getTurn();
        _playerHP = battle.getPlayerHP();
        markRunFinished();
        return true;
    }

    bool CardGameDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runBattle();
    }

    void CardGameDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.CardGameDone", "[KitDemo] card game: battle phase %# on turn %#, player HP %#", _phase, _turn, _playerHP );
    }

    void CardGameDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, CardGameDemoComponentInternal::kStateTag, CardGameDemoComponentInternal::kStateVersion );
    }
} // namespace sw
