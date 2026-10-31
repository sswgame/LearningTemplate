#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Poker/PokerHand.h"

// PokerHandTest — 포커 족보 판정: 열 족보, 휠(A-2-3-4-5)의 높은 카드 5, 로열 · 스트레이트 플러시, 족보 안의 비교 숫자 순서(풀하우스는 트리플 먼저,
// 투페어는 높은 페어 · 낮은 페어 · 키커), 무늬만 다른 무승부, 7 장 중 가장 센 5 장, 5 장보다 적은 입력. 키트 GF_CardGame(nogpu).

namespace
{
    /** @brief "As" · "Td" · "9h" · "2c" 꼴(숫자 · 무늬)로 카드 하나를 만듭니다. 번호는 무늬 × 13 + 숫자 - 1 입니다. */
    sw::Card parseCard( const utf8* pText )
    {
        uint8 rank = 0;
        switch ( pText[0] )
        {
            case 'A':
                rank = sw::StandardRank::kAce;
                break;
            case 'K':
                rank = sw::StandardRank::kKing;
                break;
            case 'Q':
                rank = sw::StandardRank::kQueen;
                break;
            case 'J':
                rank = sw::StandardRank::kJack;
                break;
            case 'T':
                rank = 10;
                break;
            default:
                rank = static_cast<uint8>( pText[0] - '0' );
                break;
        }
        uint8 suit = sw::StandardSuit::kClubs;
        switch ( pText[1] )
        {
            case 'd':
                suit = sw::StandardSuit::kDiamonds;
                break;
            case 'h':
                suit = sw::StandardSuit::kHearts;
                break;
            case 's':
                suit = sw::StandardSuit::kSpades;
                break;
            default:
                break;
        }
        return sw::Card{ static_cast<uint16>( suit * 13 + rank - 1 ), suit, rank };
    }

    sw::PokerHandValue evaluate( std::initializer_list<const utf8*> listText )
    {
        sw::vector<sw::Card> listCard;
        for ( const utf8* pText : listText )
        {
            listCard.push_back( parseCard( pText ) );
        }
        if ( listCard.size() == 5 )
            return sw::PokerHandEvaluator::evaluateFive( listCard.data() );
        return sw::PokerHandEvaluator::evaluateBest( listCard.data(), static_cast<int32>( listCard.size() ) );
    }
} // namespace

/**
 * @brief [PokerHandTest] 다섯 장이 열 족보로 갈리고, 족보 순서대로 점수가 오른다
 */
SW_TEST_CASE( PokerHandTest, FiveCardsClassifyIntoEveryRank )
{
    const sw::PokerHandValue arrValue[] = {
        evaluate( { "Ac", "Jd", "8h", "5s", "3c" } ),
        evaluate( { "9c", "9d", "Kh", "5s", "3c" } ),
        evaluate( { "9c", "9d", "5h", "5s", "Kc" } ),
        evaluate( { "7c", "7d", "7h", "Ks", "2c" } ),
        evaluate( { "5c", "6d", "7h", "8s", "9c" } ),
        evaluate( { "2h", "7h", "9h", "Jh", "Kh" } ),
        evaluate( { "Tc", "Td", "Th", "4s", "4c" } ),
        evaluate( { "Qc", "Qd", "Qh", "Qs", "3c" } ),
        evaluate( { "5d", "6d", "7d", "8d", "9d" } ),
        evaluate( { "Ts", "Js", "Qs", "Ks", "As" } ),
    };
    const sw::PokerHandRank arrExpected[] = {
        sw::PokerHandRank::HighCard,
        sw::PokerHandRank::OnePair,
        sw::PokerHandRank::TwoPair,
        sw::PokerHandRank::ThreeOfAKind,
        sw::PokerHandRank::Straight,
        sw::PokerHandRank::Flush,
        sw::PokerHandRank::FullHouse,
        sw::PokerHandRank::FourOfAKind,
        sw::PokerHandRank::StraightFlush,
        sw::PokerHandRank::RoyalFlush,
    };
    for ( size_t index = 0; index < 10; ++index )
    {
        SW_EXPECT_TRUE( arrValue[index]._rank == arrExpected[index] );
        if ( index > 0 )
            SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( arrValue[index], arrValue[index - 1] ) > 0 );
    }
}

/**
 * @brief [PokerHandTest] 휠(A-2-3-4-5)은 스트레이트이고 높은 카드가 5 라 6 높은 스트레이트보다 약하다. 무늬가 같으면 스트레이트 플러시(로열 아님)다
 */
SW_TEST_CASE( PokerHandTest, WheelIsFiveHighStraight )
{
    const sw::PokerHandValue wheel = evaluate( { "Ac", "2d", "3h", "4s", "5c" } );
    SW_EXPECT_TRUE( wheel._rank == sw::PokerHandRank::Straight );
    SW_EXPECT_EQUAL( uint8( 5 ), wheel._arrTieBreak[0] );
    const sw::PokerHandValue sixHigh = evaluate( { "2c", "3d", "4h", "5s", "6c" } );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( wheel, sixHigh ) < 0 );
    const sw::PokerHandValue aceHigh = evaluate( { "Tc", "Jd", "Qh", "Ks", "Ac" } );
    SW_EXPECT_TRUE( aceHigh._rank == sw::PokerHandRank::Straight );
    SW_EXPECT_EQUAL( uint8( 14 ), aceHigh._arrTieBreak[0] );

    const sw::PokerHandValue steelWheel = evaluate( { "Ah", "2h", "3h", "4h", "5h" } );
    SW_EXPECT_TRUE( steelWheel._rank == sw::PokerHandRank::StraightFlush );

    // 감싸 도는 것(Q-K-A-2-3)은 스트레이트가 아니다
    SW_EXPECT_TRUE( evaluate( { "Qc", "Kd", "Ah", "2s", "3c" } )._rank == sw::PokerHandRank::HighCard );
}

/**
 * @brief [PokerHandTest] 같은 족보 안의 비교: 풀하우스는 트리플이 먼저, 투페어는 높은 페어 → 낮은 페어 → 키커, 원페어는 페어 → 키커 내림차순
 */
SW_TEST_CASE( PokerHandTest, TieBreakOrderWithinRank )
{
    const sw::PokerHandValue threesOverAces = evaluate( { "3c", "3d", "3h", "As", "Ac" } );
    const sw::PokerHandValue twosOverKings  = evaluate( { "2c", "2d", "2h", "Ks", "Kc" } );
    SW_EXPECT_EQUAL( uint8( 3 ), threesOverAces._arrTieBreak[0] );
    SW_EXPECT_EQUAL( uint8( 14 ), threesOverAces._arrTieBreak[1] );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( threesOverAces, twosOverKings ) > 0 );

    const sw::PokerHandValue kingsAndTwos   = evaluate( { "Kc", "Kd", "2h", "2s", "3c" } );
    const sw::PokerHandValue queensAndJacks = evaluate( { "Qc", "Qd", "Jh", "Js", "Ac" } );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( kingsAndTwos, queensAndJacks ) > 0 ); // 높은 페어가 먼저
    const sw::PokerHandValue kingsAndThrees = evaluate( { "Kh", "Ks", "3h", "3s", "2c" } );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( kingsAndThrees, kingsAndTwos ) > 0 ); // 그다음 낮은 페어
    const sw::PokerHandValue kingsTwosAce = evaluate( { "Kh", "Ks", "2c", "2d", "Ah" } );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( kingsTwosAce, kingsAndTwos ) > 0 ); // 마지막 키커

    const sw::PokerHandValue pairKicker = evaluate( { "9c", "9d", "Ah", "4s", "3c" } );
    SW_EXPECT_EQUAL( uint8( 9 ), pairKicker._arrTieBreak[0] );
    SW_EXPECT_EQUAL( uint8( 14 ), pairKicker._arrTieBreak[1] );
    SW_EXPECT_EQUAL( uint8( 4 ), pairKicker._arrTieBreak[2] );
    SW_EXPECT_EQUAL( uint8( 3 ), pairKicker._arrTieBreak[3] );
    SW_EXPECT_EQUAL( uint8( 0 ), pairKicker._arrTieBreak[4] );
}

/**
 * @brief [PokerHandTest] 무늬만 다른 같은 패는 무승부(0)다. 플러시끼리는 가장 높은 카드부터 비교한다
 */
SW_TEST_CASE( PokerHandTest, SuitsNeverBreakTies )
{
    const sw::PokerHandValue clubs  = evaluate( { "Ac", "Kc", "9d", "5h", "2s" } );
    const sw::PokerHandValue hearts = evaluate( { "Ah", "Kh", "9s", "5c", "2d" } );
    SW_EXPECT_EQUAL( 0, sw::PokerHandEvaluator::compareHands( clubs, hearts ) );
    SW_EXPECT_EQUAL( clubs._score, hearts._score );

    const sw::PokerHandValue aceFlush  = evaluate( { "Ad", "9d", "7d", "4d", "2d" } );
    const sw::PokerHandValue kingFlush = evaluate( { "Ks", "Qs", "Js", "9s", "8s" } );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( aceFlush, kingFlush ) > 0 );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( kingFlush, aceFlush ) < 0 );
}

/**
 * @brief [PokerHandTest] 7 장(홀 2 + 보드 5)에서 가장 센 5 장을 고른다 — 숨은 스트레이트 플러시, 보드가 그대로 이기는 경우, 고른 5 장의 내용
 */
SW_TEST_CASE( PokerHandTest, BestOfSevenPicksStrongestFive )
{
    const sw::PokerHandValue hidden = evaluate( { "6h", "7h", "8h", "9h", "Ac", "Ad", "Th" } );
    SW_EXPECT_TRUE( hidden._rank == sw::PokerHandRank::StraightFlush ); // 에이스 두 장보다 세다
    SW_EXPECT_EQUAL( uint8( 10 ), hidden._arrTieBreak[0] );
    for ( const sw::Card& card : hidden._arrBest )
    {
        SW_EXPECT_EQUAL( sw::StandardSuit::kHearts, card._suit );
    }

    // 보드가 로열 플러시이면 두 손이 같다
    const sw::PokerHandValue first  = evaluate( { "2c", "3d", "Ts", "Js", "Qs", "Ks", "As" } );
    const sw::PokerHandValue second = evaluate( { "Ah", "Ac", "Ts", "Js", "Qs", "Ks", "As" } );
    SW_EXPECT_TRUE( first._rank == sw::PokerHandRank::RoyalFlush );
    SW_EXPECT_EQUAL( 0, sw::PokerHandEvaluator::compareHands( first, second ) );

    // 풀하우스 둘을 만들 수 있으면 트리플이 높은 쪽
    const sw::PokerHandValue doubleTrips = evaluate( { "8c", "8d", "8h", "Kc", "Kd", "Kh", "2s" } );
    SW_EXPECT_TRUE( doubleTrips._rank == sw::PokerHandRank::FullHouse );
    SW_EXPECT_EQUAL( uint8( 13 ), doubleTrips._arrTieBreak[0] );
    SW_EXPECT_EQUAL( uint8( 8 ), doubleTrips._arrTieBreak[1] );
}

/**
 * @brief [PokerHandTest] 5 장보다 적으면 점수 0 인 빈 결과이고, 6 장도 가장 센 5 장을 고른다
 */
SW_TEST_CASE( PokerHandTest, FewerThanFiveCardsGiveEmptyValue )
{
    const sw::Card           arrCard[] = { parseCard( "Ac" ), parseCard( "Ad" ), parseCard( "Ah" ), parseCard( "As" ) };
    const sw::PokerHandValue empty     = sw::PokerHandEvaluator::evaluateBest( arrCard, 4 );
    SW_EXPECT_EQUAL( uint32( 0 ), empty._score );
    SW_EXPECT_FALSE( empty._arrBest[0].isValid() );
    SW_EXPECT_TRUE( sw::PokerHandEvaluator::compareHands( empty, evaluate( { "2c", "3d", "4h", "5s", "7c" } ) ) < 0 );

    const sw::PokerHandValue six = evaluate( { "Ac", "Ad", "Ah", "As", "2c", "Kd" } );
    SW_EXPECT_TRUE( six._rank == sw::PokerHandRank::FourOfAKind );
    SW_EXPECT_EQUAL( uint8( 13 ), six._arrTieBreak[1] ); // 키커는 2 가 아니라 K
}
