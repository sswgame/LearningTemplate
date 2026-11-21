#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Uno/UnoGame.h"

// UnoGameTest — 우노의 손패 점수: 숫자는 그 수, 스킵 · 리버스 · +2 는 20, 와일드 두 가지는 50 이다(이긴 사람이 남의 손패 점수를 받는다). 키트 GF_CardGame(nogpu).

namespace
{
    struct UnoGameTestUtil
    {
        static sw::Card makeCard( uint16 cardID, sw::UnoColor color, sw::UnoValue value ) { return sw::Card{ cardID, static_cast<uint8>( color ), static_cast<uint8>( value ) }; }
    };
} // namespace

/** @brief [UnoGameTest] 손패 점수는 카드 종류마다 정해진 값을 더한다. 빈 손패는 0 이다. */
SW_TEST_CASE( UnoGameTest, HandPointsFollowTheScoringTable )
{
    sw::CardPile hand;
    SW_EXPECT_EQUAL( 0, sw::UnoGame::computeHandPoints( hand ) );

    hand.push( UnoGameTestUtil::makeCard( 0, sw::UnoColor::Blue, sw::UnoValue::Zero ) );            // 0
    hand.push( UnoGameTestUtil::makeCard( 1, sw::UnoColor::Red, static_cast<sw::UnoValue>( 7 ) ) ); // 7
    hand.push( UnoGameTestUtil::makeCard( 2, sw::UnoColor::Green, sw::UnoValue::Nine ) );           // 9
    SW_EXPECT_EQUAL( 16, sw::UnoGame::computeHandPoints( hand ) );

    hand.push( UnoGameTestUtil::makeCard( 3, sw::UnoColor::Green, sw::UnoValue::Skip ) );     // 20
    hand.push( UnoGameTestUtil::makeCard( 4, sw::UnoColor::Yellow, sw::UnoValue::Reverse ) ); // 20
    hand.push( UnoGameTestUtil::makeCard( 5, sw::UnoColor::Red, sw::UnoValue::DrawTwo ) );    // 20
    SW_EXPECT_EQUAL( 76, sw::UnoGame::computeHandPoints( hand ) );

    hand.push( UnoGameTestUtil::makeCard( 6, sw::UnoColor::Wild, sw::UnoValue::Wild ) );         // 50
    hand.push( UnoGameTestUtil::makeCard( 7, sw::UnoColor::Wild, sw::UnoValue::WildDrawFour ) ); // 50
    SW_EXPECT_EQUAL( 176, sw::UnoGame::computeHandPoints( hand ) );
}

/** @brief [UnoGameTest] 덱 한 벌(108 장)의 점수 합은 숫자 360 + 색 행동 카드 24 장 × 20 + 와일드 8 장 × 50 = 1240 이다 — 덱 구성과 점수표가 함께 맞아야 나온다. */
SW_TEST_CASE( UnoGameTest, WholeDeckScoresTheKnownTotal )
{
    sw::CardPile deck;
    sw::UnoGame::makeDeck( deck );
    SW_REQUIRE_EQUAL( 108, deck.getCount() );
    SW_EXPECT_EQUAL( 1240, sw::UnoGame::computeHandPoints( deck ) );
}

/** @brief [UnoGameTest] 결정성: 차례인 사람이 걸음 번호로 고른 낼 패를 내거나 뽑고, 우노를 잡으며 다시 섞기까지 가면 두 판의 상태 바이트가 같고, 중간 세이브를 읽어 이어 돌려도 같다. */
SW_TEST_CASE( UnoGameTest, GameDeterminism )
{
    sw::UnoSettings settings;
    settings._bStackDrawTwo  = SW_TRUE;
    settings._bStackDrawFour = SW_TRUE;
    const auto setup         = [&settings]( sw::UnoGame& game )
    { return game.initialize( settings, 7u ); };
    const auto step = []( sw::UnoGame& game, int32 stepIndex )
    {
        if ( game.getWinner() >= 0 )
            return;
        const int32 player = game.getCurrentPlayer();
        if ( stepIndex % 10 == 9 )
            (void)game.callUno( ( player + 1 ) % game.getPlayerCount() );
        const sw::CardPile& hand  = game.getHand( player );
        const int32         count = hand.getCount();
        for ( int32 offset = 0; offset < count; ++offset )
        {
            const sw::Card& card = hand.getAt( ( stepIndex + offset ) % count );
            if ( game.isPlayable( card ) )
            {
                (void)game.playCard( player, card._id, static_cast<sw::UnoColor>( stepIndex % 4 ), stepIndex % 3 != 0 );
                return;
            }
        }
        (void)game.drawCard( player );
    };
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::isDeterministic<sw::UnoGame>( setup, step, 240 ) );
}
