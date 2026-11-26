#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Poker/PokerTable.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

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
            return sw::PokerHandValue::evaluateFive( listCard.data() );
        return sw::PokerHandValue::evaluateBest( listCard.data(), static_cast<int32>( listCard.size() ) );
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
            SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( arrValue[index], arrValue[index - 1] ) > 0 );
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
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( wheel, sixHigh ) < 0 );
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
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( threesOverAces, twosOverKings ) > 0 );

    const sw::PokerHandValue kingsAndTwos   = evaluate( { "Kc", "Kd", "2h", "2s", "3c" } );
    const sw::PokerHandValue queensAndJacks = evaluate( { "Qc", "Qd", "Jh", "Js", "Ac" } );
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( kingsAndTwos, queensAndJacks ) > 0 ); // 높은 페어가 먼저
    const sw::PokerHandValue kingsAndThrees = evaluate( { "Kh", "Ks", "3h", "3s", "2c" } );
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( kingsAndThrees, kingsAndTwos ) > 0 ); // 그다음 낮은 페어
    const sw::PokerHandValue kingsTwosAce = evaluate( { "Kh", "Ks", "2c", "2d", "Ah" } );
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( kingsTwosAce, kingsAndTwos ) > 0 ); // 마지막 키커

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
    SW_EXPECT_EQUAL( 0, sw::PokerHandValue::compareHands( clubs, hearts ) );
    SW_EXPECT_EQUAL( clubs._score, hearts._score );

    const sw::PokerHandValue aceFlush  = evaluate( { "Ad", "9d", "7d", "4d", "2d" } );
    const sw::PokerHandValue kingFlush = evaluate( { "Ks", "Qs", "Js", "9s", "8s" } );
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( aceFlush, kingFlush ) > 0 );
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( kingFlush, aceFlush ) < 0 );
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
    SW_EXPECT_EQUAL( 0, sw::PokerHandValue::compareHands( first, second ) );

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
    const sw::PokerHandValue empty     = sw::PokerHandValue::evaluateBest( arrCard, 4 );
    SW_EXPECT_EQUAL( uint32( 0 ), empty._score );
    SW_EXPECT_FALSE( empty._arrBest[0].isValid() );
    SW_EXPECT_TRUE( sw::PokerHandValue::compareHands( empty, evaluate( { "2c", "3d", "4h", "5s", "7c" } ) ) < 0 );

    const sw::PokerHandValue six = evaluate( { "Ac", "Ad", "Ah", "As", "2c", "Kd" } );
    SW_EXPECT_TRUE( six._rank == sw::PokerHandRank::FourOfAKind );
    SW_EXPECT_EQUAL( uint8( 13 ), six._arrTieBreak[1] ); // 키커는 2 가 아니라 K
}

/**
 * @brief [PokerTableTest] 열 수 없는 판은 칩을 건드리지 않는다 — 진행 중인 판을 다시 열거나 장수가 모자란 덱으로 열어도 칩이 사라지지 않는다
 */
SW_TEST_CASE( PokerTableTest, RefusedStartKeepsEveryChip )
{
    sw::PokerTable table;
    SW_REQUIRE_TRUE( table.initialize( sw::PokerSettings{}, { 100, 100 } ) );
    const auto countChips = [&table]()
    {
        int32 total = 0;
        for ( int32 seat = 0; seat < table.getSeatCount(); ++seat )
        {
            total += table.getSeat( seat )._stack;
        }
        return total;
    };

    sw::CardPile shortDeck;
    sw::CardPile::makeStandardDeck( shortDeck );
    while ( shortDeck.getCount() > 8 ) // 홀 넷 + 보드 다섯에 한 장 모자란다
    {
        sw::Card card;
        SW_REQUIRE_TRUE( shortDeck.draw( card ) );
    }
    SW_EXPECT_FALSE( table.startHandWithDeck( shortDeck ) );
    SW_EXPECT_EQUAL( 200, countChips() );
    SW_EXPECT_TRUE( table.getStreet() == sw::PokerStreet::HandOver );

    sw::GameRandom random( 7u );
    SW_REQUIRE_TRUE( table.startHand( random ) );
    const int32 pot = table.computePotTotal();
    SW_REQUIRE_TRUE( pot > 0 );
    SW_EXPECT_FALSE( table.startHand( random ) );
    SW_EXPECT_EQUAL( pot, table.computePotTotal() );
    SW_REQUIRE_TRUE( table.act( table.getCurrentSeat(), sw::PokerActionKind::Fold ) );
    SW_EXPECT_EQUAL( 200, countChips() );
}

/**
 * @brief [PokerTableTest] 빅 블라인드가 모자라게 올인해도 들어오려면 온전한 빅 블라인드를 낸다. 그 판의 저장도 다시 읽힌다
 */
SW_TEST_CASE( PokerTableTest, ShortBigBlindStillCostsAFullCall )
{
    sw::PokerSettings settings;
    settings._smallBlind = 10;
    settings._bigBlind   = 20;
    sw::PokerTable table;
    SW_REQUIRE_TRUE( table.initialize( settings, { 100, 100, 5 } ) ); // 버튼 0, 스몰 1, 빅 2
    sw::GameRandom random( 7u );
    SW_REQUIRE_TRUE( table.startHand( random ) );
    SW_EXPECT_EQUAL( 20, table.getCurrentBet() );
    SW_EXPECT_EQUAL( 40, table.getMinRaiseTo() );
    SW_REQUIRE_EQUAL( 0, table.getCurrentSeat() );
    SW_REQUIRE_TRUE( table.act( 0, sw::PokerActionKind::Call ) );
    SW_EXPECT_EQUAL( 20, table.getSeat( 0 )._committed );

    sw::PokerTable poor; // 셋 다 블라인드보다 적다 — 맞춰야 할 값이 판의 칩보다 크다
    sw::PokerTable restored;
    SW_REQUIRE_TRUE( poor.initialize( settings, { 3, 3, 3 } ) );
    SW_REQUIRE_TRUE( restored.initialize( settings, { 3, 3, 3 } ) );
    SW_REQUIRE_TRUE( poor.startHand( random ) );
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::roundTrips( poor, restored ) );
}

/**
 * @brief [PokerTableTest] 상대가 모두 올인이고 이미 그만큼 낸 한 사람은 행동하지 않는다 — 걸 곳이 없는 사람에게 폴드를 묻지 않고 보드를 끝까지 깐다
 */
SW_TEST_CASE( PokerTableTest, LoneCoveringPlayerDoesNotAct )
{
    sw::PokerTable table;
    SW_REQUIRE_TRUE( table.initialize( sw::PokerSettings{}, { 100, 1 } ) ); // 버튼 · 스몰 0 이 1 을 내고, 빅 1 은 1 로 올인이다
    sw::GameRandom random( 7u );
    SW_REQUIRE_TRUE( table.startHand( random ) );
    SW_EXPECT_TRUE( table.getStreet() == sw::PokerStreet::HandOver );
    SW_EXPECT_EQUAL( 101, table.getSeat( 0 )._stack + table.getSeat( 1 )._stack );
}

/**
 * @brief [PokerTableTest] 모자란 올인은 이미 행동한 사람에게 레이즈를 다시 열지 않는다(TDA 47 예 3-A). 뒤에서 온전한 레이즈가 나오면 열린다(3-B)
 */
SW_TEST_CASE( PokerTableTest, ShortAllInDoesNotReopenTheBetting )
{
    sw::PokerSettings settings;
    settings._smallBlind = 10;
    settings._bigBlind   = 20;
    for ( const bool bBigBlindRaises : { false, true } )
    {
        sw::PokerTable table;
        SW_REQUIRE_TRUE( table.initialize( settings, { 35, 1000, 1000, 1000 } ) ); // 버튼 0, 스몰 1, 빅 2, 첫 차례 3
        sw::GameRandom random( 7u );
        SW_REQUIRE_TRUE( table.startHand( random ) );
        SW_REQUIRE_TRUE( table.act( 3, sw::PokerActionKind::Call ) );
        SW_REQUIRE_TRUE( table.act( 0, sw::PokerActionKind::AllIn ) ); // 35 — 증액 15 는 레이즈 20 에 모자란다
        SW_REQUIRE_TRUE( table.act( 1, sw::PokerActionKind::Fold ) );
        SW_EXPECT_TRUE( table.canRaise( 2 ) ); // 빅 블라인드는 아직 행동하지 않았다
        SW_EXPECT_EQUAL( 55, table.getMinRaiseTo() );
        if ( bBigBlindRaises )
        {
            SW_REQUIRE_TRUE( table.act( 2, sw::PokerActionKind::Raise, 55 ) );
            SW_EXPECT_TRUE( table.canRaise( 3 ) );
            SW_EXPECT_TRUE( table.act( 3, sw::PokerActionKind::Raise, 75 ) );
            continue;
        }
        SW_REQUIRE_TRUE( table.act( 2, sw::PokerActionKind::Call ) );
        SW_REQUIRE_EQUAL( 3, table.getCurrentSeat() );
        SW_EXPECT_FALSE( table.canRaise( 3 ) );
        SW_EXPECT_FALSE( table.act( 3, sw::PokerActionKind::Raise, 55 ) );
        SW_EXPECT_FALSE( table.act( 3, sw::PokerActionKind::AllIn ) );
        SW_EXPECT_TRUE( table.act( 3, sw::PokerActionKind::Call ) );
        SW_EXPECT_TRUE( table.getStreet() == sw::PokerStreet::Flop );
    }
}

/**
 * @brief [PokerTableTest] 모자란 올인 여럿을 합쳐 온전한 레이즈만큼 맞춰야 하면 다시 열린다(TDA 47 예 1). 최소 증액은 마지막 온전한 레이즈 그대로다(예 2)
 */
SW_TEST_CASE( PokerTableTest, CumulativeShortAllInsReopenTheBetting )
{
    sw::PokerSettings settings;
    settings._smallBlind = 10;
    settings._bigBlind   = 20;
    sw::PokerTable table;
    SW_REQUIRE_TRUE( table.initialize( settings, { 45, 1000, 1000, 1000, 32 } ) ); // 버튼 0, 스몰 1, 빅 2, 첫 차례 3, 다음 4
    sw::GameRandom random( 7u );
    SW_REQUIRE_TRUE( table.startHand( random ) );
    SW_REQUIRE_TRUE( table.act( 3, sw::PokerActionKind::Call ) );
    SW_REQUIRE_TRUE( table.act( 4, sw::PokerActionKind::AllIn ) ); // 32(+12)
    SW_REQUIRE_TRUE( table.act( 0, sw::PokerActionKind::AllIn ) ); // 45(+13)
    SW_REQUIRE_TRUE( table.act( 1, sw::PokerActionKind::Fold ) );
    SW_REQUIRE_TRUE( table.act( 2, sw::PokerActionKind::Call ) );
    SW_REQUIRE_EQUAL( 3, table.getCurrentSeat() );
    SW_EXPECT_TRUE( table.canRaise( 3 ) ); // 25 를 맞춰야 한다 — 레이즈 20 이상이다
    SW_EXPECT_EQUAL( 65, table.getMinRaiseTo() );
    SW_EXPECT_TRUE( table.act( 3, sw::PokerActionKind::Raise, 65 ) );
}

/**
 * @brief [PokerTableTest] 다시 연 테이블은 새 테이블과 같은 상태다 — 앞 판의 큰 베팅이 남으면 칩이 적은 새 테이블의 저장을 읽기가 거절한다
 */
SW_TEST_CASE( PokerTableTest, ReinitializedTableMatchesAFreshOne )
{
    sw::PokerTable table;
    SW_REQUIRE_TRUE( table.initialize( sw::PokerSettings{}, { 5000, 5000 } ) );
    sw::GameRandom random( 7u );
    SW_REQUIRE_TRUE( table.startHand( random ) );
    SW_REQUIRE_TRUE( table.act( table.getCurrentSeat(), sw::PokerActionKind::AllIn ) );
    SW_REQUIRE_TRUE( table.act( table.getCurrentSeat(), sw::PokerActionKind::Fold ) );

    sw::PokerTable fresh;
    sw::PokerTable restored;
    SW_REQUIRE_TRUE( table.initialize( sw::PokerSettings{}, { 100, 100 } ) );
    SW_REQUIRE_TRUE( fresh.initialize( sw::PokerSettings{}, { 100, 100 } ) );
    SW_REQUIRE_TRUE( restored.initialize( sw::PokerSettings{}, { 100, 100 } ) );
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::hasSameState( table, fresh ) );
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::roundTrips( table, restored ) );
}
