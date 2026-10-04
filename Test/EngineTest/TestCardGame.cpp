#include "pch.h"

#include "GameFramework/Kits/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Casual/CardGame/DeckBattle.h"
#include "GameFramework/Kits/Casual/CardGame/HwatuDeck.h"
#include "GameFramework/Kits/Casual/CardGame/KlondikeGame.h"
#include "GameFramework/Kits/Casual/CardGame/MatgoGame.h"
#include "GameFramework/Kits/Casual/CardGame/PokerHand.h"
#include "GameFramework/Kits/Casual/CardGame/PokerTable.h"
#include "GameFramework/Kits/Casual/CardGame/UnoGame.h"

#include "TestFramework/TestFramework.h"

// 카드 게임 키트 — 씨앗 셔플 · 나누기 · 행동 바이트, 포커 족보 · 키커 · 사이드 팟 · 베팅, 맞고 점수 · 뻑 · 싹쓸이 · 고 배수, 솔리테어 규칙 · 되돌리기 · 자동 완료,
// 우노 스킵 · 리버스 · +4 · 우노 벌칙 · +2 쌓기, 덱 빌딩 전투 한 턴.

using namespace sw;

namespace
{
    /** @brief "As" · "Td" · "9h" → 트럼프 카드(번호는 `CardDeckUtil::makeStandardDeck` 과 같다). */
    Card makePlayingCard( const utf8* pText )
    {
        const utf8 rankChar = pText[0];
        const utf8 suitChar = pText[1];
        int32      rank     = 0;
        if ( rankChar == 'A' )
            rank = 1;
        else if ( rankChar == 'T' )
            rank = 10;
        else if ( rankChar == 'J' )
            rank = 11;
        else if ( rankChar == 'Q' )
            rank = 12;
        else if ( rankChar == 'K' )
            rank = 13;
        else
            rank = rankChar - '0';
        int32 suit = StandardSuit::kSpades;
        if ( suitChar == 'c' )
            suit = StandardSuit::kClubs;
        else if ( suitChar == 'd' )
            suit = StandardSuit::kDiamonds;
        else if ( suitChar == 'h' )
            suit = StandardSuit::kHearts;
        return Card{ static_cast<uint16>( suit * 13 + rank - 1 ), static_cast<uint8>( suit ), static_cast<uint8>( rank ) };
    }

    PokerHandValue evaluateText( std::initializer_list<const utf8*> listText )
    {
        vector<Card> listCard;
        for ( const utf8* pText : listText )
            listCard.push_back( makePlayingCard( pText ) );
        return PokerHandEvaluator::evaluateBest( listCard.data(), static_cast<int32>( listCard.size() ) );
    }

    CardPile makePlayingPile( std::initializer_list<const utf8*> listText )
    {
        CardPile pile;
        for ( const utf8* pText : listText )
            pile.push( makePlayingCard( pText ) );
        return pile;
    }

    Card makeHwatu( int32 month, int32 indexInMonth )
    {
        return HwatuDeck::makeCard( month, indexInMonth );
    }

    CardPile makeHwatuPile( std::initializer_list<std::pair<int32, int32>> listCard )
    {
        CardPile pile;
        for ( const std::pair<int32, int32>& entry : listCard )
            pile.push( makeHwatu( entry.first, entry.second ) );
        return pile;
    }

    Card makeUno( UnoColor color, UnoValue value, uint16 cardId )
    {
        return Card{ cardId, static_cast<uint8>( color ), static_cast<uint8>( value ) };
    }

    Card makeUnoNumber( UnoColor color, int32 number, uint16 cardId )
    {
        return Card{ cardId, static_cast<uint8>( color ), static_cast<uint8>( number ) };
    }

    template <typename TEvent>
    int32 countEvents( const vector<TEvent>& listEvent, typename TEvent::Kind kind )
    {
        int32 count = 0;
        for ( const TEvent& event : listEvent )
            count += event._kind == kind ? 1 : 0;
        return count;
    }
} // namespace

SW_TEST_CASE( CardGameTest, ShuffleIsSeededDealsRoundRobinAndActionsRoundTrip )
{
    CardPile deckA;
    CardPile deckB;
    CardPile deckC;
    CardDeckUtil::makeStandardDeck( deckA );
    CardDeckUtil::makeStandardDeck( deckB );
    CardDeckUtil::makeStandardDeck( deckC );
    SW_EXPECT_EQUAL( deckA.getCount(), 52 );
    GameRandom randomA( 42 );
    GameRandom randomB( 42 );
    GameRandom randomC( 43 );
    deckA.shuffle( randomA );
    deckB.shuffle( randomB );
    deckC.shuffle( randomC );
    bool bSameAsB = true;
    bool bSameAsC = true;
    bool arrSeen[52]{};
    for ( int32 index = 0; index < deckA.getCount(); ++index )
    {
        bSameAsB                          = bSameAsB && deckA.getAt( index ) == deckB.getAt( index );
        bSameAsC                          = bSameAsC && deckA.getAt( index ) == deckC.getAt( index );
        arrSeen[deckA.getAt( index )._id] = true;
    }
    SW_EXPECT_TRUE( bSameAsB );  // 같은 씨앗 → 같은 순서
    SW_EXPECT_FALSE( bSameAsC ); // 다른 씨앗 → 다른 순서
    bool bAllSeen = true;
    for ( const bool bSeen : arrSeen )
        bAllSeen = bAllSeen && bSeen;
    SW_EXPECT_TRUE( bAllSeen ); // 섞어도 52 장 그대로

    // 나누기는 맨 위부터 한 장씩 돌아간다.
    const Card       top    = deckA.getTop();
    const Card       second = deckA.getAt( deckA.getCount() - 2 );
    vector<CardPile> listHand( 3 );
    SW_EXPECT_TRUE( deckA.deal( listHand, 5 ) );
    SW_EXPECT_EQUAL( listHand[0].getCount(), 5 );
    SW_EXPECT_EQUAL( listHand[2].getCount(), 5 );
    SW_EXPECT_EQUAL( deckA.getCount(), 37 );
    SW_EXPECT_TRUE( listHand[0].getAt( 0 ) == top );
    SW_EXPECT_TRUE( listHand[1].getAt( 0 ) == second );
    SW_EXPECT_FALSE( deckA.deal( listHand, 13 ) ); // 37 장으로 3 × 13 은 모자란다

    // 행동 바이트 — 턴 중계에 싣는 꼴.
    CardAction action;
    action._kind     = static_cast<uint8>( PokerActionKind::Raise );
    action._cardId   = 0x1234;
    action._targetId = 47;
    action._amount   = -123456;
    vector<uint8> buffer;
    CardActionUtil::encodeAction( action, buffer );
    SW_EXPECT_EQUAL( static_cast<int32>( buffer.size() ), CardActionUtil::kEncodedSize );
    CardAction decoded;
    SW_EXPECT_TRUE( CardActionUtil::decodeAction( buffer, decoded ) );
    SW_EXPECT_EQUAL( decoded._kind, action._kind );
    SW_EXPECT_EQUAL( decoded._cardId, action._cardId );
    SW_EXPECT_EQUAL( decoded._targetId, action._targetId );
    SW_EXPECT_EQUAL( decoded._amount, action._amount );
    buffer[0] = 0x00;
    SW_EXPECT_FALSE( CardActionUtil::decodeAction( buffer, decoded ) ); // 표지가 다르다
    buffer.pop_back();
    SW_EXPECT_FALSE( CardActionUtil::decodeAction( buffer, decoded ) ); // 크기가 다르다
}

SW_TEST_CASE( CardGameTest, PokerRanksHandsComparesKickersAndPicksBestFiveOfSeven )
{
    const PokerHandValue royal         = evaluateText( { "As", "Ks", "Qs", "Js", "Ts" } );
    const PokerHandValue straightFlush = evaluateText( { "9h", "8h", "7h", "6h", "5h" } );
    const PokerHandValue quads         = evaluateText( { "9c", "9d", "9h", "9s", "2c" } );
    const PokerHandValue fullHouse     = evaluateText( { "3c", "3d", "3h", "2s", "2c" } );
    const PokerHandValue flush         = evaluateText( { "Kd", "Td", "7d", "4d", "2d" } );
    const PokerHandValue straight      = evaluateText( { "6c", "5d", "4h", "3s", "2c" } );
    const PokerHandValue wheel         = evaluateText( { "Ac", "2d", "3h", "4s", "5c" } );
    const PokerHandValue trips         = evaluateText( { "Qc", "Qd", "Qh", "9s", "2c" } );
    const PokerHandValue twoPair       = evaluateText( { "Jc", "Jd", "4h", "4s", "Ac" } );
    const PokerHandValue onePair       = evaluateText( { "Ac", "Ad", "Kh", "7s", "2c" } );
    const PokerHandValue highCard      = evaluateText( { "Ac", "Qd", "9h", "7s", "3c" } );

    SW_EXPECT_TRUE( royal._rank == PokerHandRank::RoyalFlush );
    SW_EXPECT_TRUE( straightFlush._rank == PokerHandRank::StraightFlush );
    SW_EXPECT_TRUE( quads._rank == PokerHandRank::FourOfAKind );
    SW_EXPECT_TRUE( fullHouse._rank == PokerHandRank::FullHouse );
    SW_EXPECT_TRUE( flush._rank == PokerHandRank::Flush );
    SW_EXPECT_TRUE( straight._rank == PokerHandRank::Straight );
    SW_EXPECT_TRUE( wheel._rank == PokerHandRank::Straight ); // A-2-3-4-5 도 스트레이트
    SW_EXPECT_TRUE( trips._rank == PokerHandRank::ThreeOfAKind );
    SW_EXPECT_TRUE( twoPair._rank == PokerHandRank::TwoPair );
    SW_EXPECT_TRUE( onePair._rank == PokerHandRank::OnePair );
    SW_EXPECT_TRUE( highCard._rank == PokerHandRank::HighCard );

    const PokerHandValue arrOrder[] = { highCard, onePair, twoPair, trips, wheel, straight, flush, fullHouse, quads, straightFlush, royal };
    for ( size_t index = 1; index < sizeof( arrOrder ) / sizeof( arrOrder[0] ); ++index )
        SW_EXPECT_TRUE( PokerHandEvaluator::compareHands( arrOrder[index], arrOrder[index - 1] ) > 0 );
    SW_EXPECT_EQUAL( wheel._arrTieBreak[0], 5 ); // 휠의 높은 카드는 에이스가 아니라 5

    // 키커 — 같은 원페어면 다음 카드로.
    const PokerHandValue acesKing  = evaluateText( { "Ah", "As", "Kc", "8d", "3c" } );
    const PokerHandValue acesQueen = evaluateText( { "Ac", "Ad", "Qh", "Jd", "9c" } );
    SW_EXPECT_TRUE( PokerHandEvaluator::compareHands( acesKing, acesQueen ) > 0 );
    // 투페어가 같으면 다섯째 카드.
    const PokerHandValue twoPairAce   = evaluateText( { "Jh", "Js", "4c", "4d", "As" } );
    const PokerHandValue twoPairQueen = evaluateText( { "Jh", "Js", "4c", "4d", "Qs" } );
    SW_EXPECT_TRUE( PokerHandEvaluator::compareHands( twoPairAce, twoPairQueen ) > 0 );
    SW_EXPECT_EQUAL( PokerHandEvaluator::compareHands( twoPair, twoPairAce ), 0 ); // 무늬만 다르면 무승부
    // 플러시끼리는 높은 카드부터.
    const PokerHandValue flushQueen = evaluateText( { "Qh", "Jh", "9h", "8h", "7h" } );
    SW_EXPECT_TRUE( PokerHandEvaluator::compareHands( flush, flushQueen ) > 0 );

    // 7 장 중 최고 5 장 — 에이스 셋 + 킹 둘 풀하우스.
    const PokerHandValue sevenBest = evaluateText( { "As", "Ad", "Ah", "Kc", "Kd", "2s", "3h" } );
    SW_EXPECT_TRUE( sevenBest._rank == PokerHandRank::FullHouse );
    SW_EXPECT_EQUAL( sevenBest._arrTieBreak[0], 14 );
    SW_EXPECT_EQUAL( sevenBest._arrTieBreak[1], 13 );
    // 보드가 스트레이트면 홀 카드가 낮은 두 사람은 비긴다.
    const PokerHandValue boardPlaysA = evaluateText( { "2c", "3d", "9s", "Th", "Jc", "Qd", "Kh" } );
    const PokerHandValue boardPlaysB = evaluateText( { "2h", "4d", "9s", "Th", "Jc", "Qd", "Kh" } );
    SW_EXPECT_TRUE( boardPlaysA._rank == PokerHandRank::Straight );
    SW_EXPECT_EQUAL( PokerHandEvaluator::compareHands( boardPlaysA, boardPlaysB ), 0 );
}

SW_TEST_CASE( CardGameTest, PokerSidePotsGoToTheRightPlayersAndOddChipsSplitFromButton )
{
    // 0 은 50, 1 은 100 에서 올인, 2 · 3 은 200 — 3 은 폴드.
    const vector<int32> listContribution = { 50, 100, 200, 200 };
    const vector<uint8> listFolded       = { SW_FALSE, SW_FALSE, SW_FALSE, SW_TRUE };
    vector<PokerPot>    listPot;
    PokerPotUtil::makePots( listContribution, listFolded, listPot );
    SW_ASSERT_TRUE( listPot.size() == 3 );
    SW_EXPECT_EQUAL( listPot[0]._amount, 200 ); // 50 × 4
    SW_EXPECT_EQUAL( static_cast<int32>( listPot[0]._listEligibleSeat.size() ), 3 );
    SW_EXPECT_EQUAL( listPot[1]._amount, 150 ); // 50 × 3
    SW_EXPECT_EQUAL( static_cast<int32>( listPot[1]._listEligibleSeat.size() ), 2 );
    SW_EXPECT_EQUAL( listPot[2]._amount, 200 ); // 2 · 3 의 나머지 — 3 은 폴드했으니 2 혼자
    SW_EXPECT_EQUAL( static_cast<int32>( listPot[2]._listEligibleSeat.size() ), 1 );

    // 짧은 스택(0)이 가장 세면 메인 팟만, 사이드는 그다음 센 1 이, 꼭대기는 2 가 돌려받는다.
    const vector<uint32> listScore = { 900, 500, 100, 0 };
    vector<int32>        listWon;
    PokerPotUtil::distributePots( listPot, listScore, 1, listWon );
    SW_EXPECT_EQUAL( listWon[0], 200 );
    SW_EXPECT_EQUAL( listWon[1], 150 );
    SW_EXPECT_EQUAL( listWon[2], 200 );
    SW_EXPECT_EQUAL( listWon[3], 0 );

    // 무승부 — 101 칩을 둘이 나누면 남는 1 칩은 버튼 왼쪽(firstSeat)부터.
    vector<PokerPot> listSplit( 1 );
    listSplit[0]._amount           = 101;
    listSplit[0]._listEligibleSeat = { 0, 1, 2 };
    const vector<uint32> listTie   = { 700, 700, 300 };
    PokerPotUtil::distributePots( listSplit, listTie, 1, listWon );
    SW_EXPECT_EQUAL( listWon[1], 51 );
    SW_EXPECT_EQUAL( listWon[0], 50 );
    SW_EXPECT_EQUAL( listWon[2], 0 );
}

SW_TEST_CASE( CardGameTest, PokerTableEnforcesBlindsMinRaiseAllInAndShowdown )
{
    PokerSettings settings;
    settings._smallBlind = 5;
    settings._bigBlind   = 10;
    PokerTable table;
    table.initialize( settings, { 100, 50, 200 } );

    // 버튼 0 · SB 1 · BB 2. 나누기는 SB 부터 한 바퀴씩 — 1, 2, 0, 1, 2, 0, 그다음 보드 다섯.
    CardPile deck;
    CardDeckUtil::makePileInDrawOrder( { makePlayingCard( "Ks" ), makePlayingCard( "7c" ), makePlayingCard( "As" ), makePlayingCard( "Kd" ), makePlayingCard( "2d" ),
                                         makePlayingCard( "Ad" ), makePlayingCard( "3h" ), makePlayingCard( "8s" ), makePlayingCard( "9c" ), makePlayingCard( "Jd" ),
                                         makePlayingCard( "4c" ) },
                                       deck );
    SW_ASSERT_TRUE( table.startHandWithDeck( deck ) );
    SW_EXPECT_EQUAL( table.getButton(), 0 );
    SW_EXPECT_EQUAL( table.getSeat( 1 )._committed, 5 );
    SW_EXPECT_EQUAL( table.getSeat( 2 )._committed, 10 );
    SW_EXPECT_EQUAL( table.getCurrentSeat(), 0 ); // BB 다음이 먼저
    SW_EXPECT_TRUE( table.getSeat( 0 )._arrHole[0] == makePlayingCard( "As" ) );

    SW_EXPECT_FALSE( table.act( 1, PokerActionKind::Call ) );      // 차례가 아니다
    SW_EXPECT_FALSE( table.act( 0, PokerActionKind::Check ) );     // 10 을 내야 한다
    SW_EXPECT_FALSE( table.act( 0, PokerActionKind::Raise, 15 ) ); // 최소 레이즈는 20
    SW_EXPECT_EQUAL( table.getMinRaiseTo(), 20 );

    // 중계로 온 행동 — 바이트를 풀어 넣는다.
    CardAction raise;
    raise._kind   = static_cast<uint8>( PokerActionKind::Raise );
    raise._amount = 30;
    vector<uint8> buffer;
    CardActionUtil::encodeAction( raise, buffer );
    CardAction received;
    SW_ASSERT_TRUE( CardActionUtil::decodeAction( buffer, received ) );
    SW_EXPECT_TRUE( table.applyAction( 0, received ) );
    SW_EXPECT_EQUAL( table.getCurrentBet(), 30 );
    SW_EXPECT_EQUAL( table.getMinRaiseTo(), 50 ); // 직전 증액 20 만큼 더

    SW_EXPECT_TRUE( table.act( 1, PokerActionKind::AllIn ) ); // 5 + 45 = 50
    SW_EXPECT_TRUE( table.getSeat( 1 )._bAllIn == SW_TRUE );
    SW_EXPECT_TRUE( table.act( 2, PokerActionKind::Call ) );
    SW_EXPECT_EQUAL( table.getSeat( 2 )._committed, 50 );
    SW_EXPECT_EQUAL( table.getCurrentSeat(), 0 );                  // 레이즈가 다시 열렸다
    SW_EXPECT_TRUE( table.act( 0, PokerActionKind::Raise, 100 ) ); // 남은 칩 전부 — 올인
    SW_EXPECT_TRUE( table.act( 2, PokerActionKind::Call ) );

    // 남은 사람이 하나뿐이라 보드를 끝까지 깔고 쇼다운 — 메인 150 · 사이드 100 모두 에이스 페어가.
    SW_EXPECT_TRUE( table.getStreet() == PokerStreet::HandOver );
    SW_EXPECT_EQUAL( table.getBoard().getCount(), 5 );
    SW_ASSERT_TRUE( table.getLastPots().size() == 2 );
    SW_EXPECT_EQUAL( table.getLastPots()[0]._amount, 150 );
    SW_EXPECT_EQUAL( table.getLastPots()[1]._amount, 100 );
    SW_EXPECT_EQUAL( table.getSeat( 0 )._stack, 250 );
    SW_EXPECT_EQUAL( table.getSeat( 1 )._stack, 0 );
    SW_EXPECT_EQUAL( table.getSeat( 2 )._stack, 100 );

    // 다음 판 — 칩 있는 둘만. 버튼(2)이 SB 이고 먼저 행동한다. 폴드하면 BB 가 블라인드를 가져간다.
    GameRandom random( 7 );
    SW_ASSERT_TRUE( table.startHand( random ) );
    SW_EXPECT_EQUAL( table.getButton(), 2 );
    SW_EXPECT_EQUAL( table.getCurrentSeat(), 2 );
    SW_EXPECT_TRUE( table.getSeat( 1 )._bInHand == SW_FALSE );
    SW_EXPECT_TRUE( table.act( 2, PokerActionKind::Fold ) );
    SW_EXPECT_TRUE( table.getStreet() == PokerStreet::HandOver );
    SW_EXPECT_EQUAL( table.getSeat( 0 )._stack, 255 );
    SW_EXPECT_EQUAL( table.getSeat( 2 )._stack, 95 );
    vector<PokerEvent> listEvent;
    table.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, PokerEvent::Kind::Win ), 2 );
}

SW_TEST_CASE( CardGameTest, MatgoScoresGwangGodoriDanPiAndAppliesBakAndGoMultiplier )
{
    const MatgoSettings settings = MatgoSettings::makeMatgo();
    // 비광 포함 3 광(2) · 고도리(5) · 홍단(3) · 피 10 장(쌍피 하나 포함, 1).
    const CardPile   winnerPile = makeHwatuPile( {
        { 1, 0},
        { 3, 0},
        {12, 0},
        { 2, 0},
        { 4, 0},
        { 8, 1},
        { 1, 1},
        { 2, 1},
        { 3, 1},
        {11, 1},
        { 5, 2},
        { 5, 3},
        { 6, 2},
        { 6, 3},
        { 7, 2},
        { 7, 3},
        { 9, 2},
        { 9, 3}
    } );
    const HwatuScore winner     = MatgoGame::computeScore( winnerPile, settings );
    SW_EXPECT_EQUAL( winner._gwangCount, 3 );
    SW_EXPECT_EQUAL( winner._gwang, 2 ); // 비광이 끼면 3 광은 2 점
    SW_EXPECT_EQUAL( winner._godori, 5 );
    SW_EXPECT_EQUAL( winner._dan, 3 );
    SW_EXPECT_EQUAL( winner._piCount, 10 );
    SW_EXPECT_EQUAL( winner._pi, 1 );
    SW_EXPECT_EQUAL( winner._total, 11 );

    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  {1, 0},
                                                  {3, 0},
                                                  {8, 0}
    } ),
                                              settings )
                         ._gwang,
                     3 );
    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  { 1, 0},
                                                  { 3, 0},
                                                  { 8, 0},
                                                  {12, 0}
    } ),
                                              settings )
                         ._gwang,
                     4 );
    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  { 1, 0},
                                                  { 3, 0},
                                                  { 8, 0},
                                                  {11, 0},
                                                  {12, 0}
    } ),
                                              settings )
                         ._gwang,
                     15 );
    // 열끗 다섯부터 1 점 · 띠 다섯부터 1 점(단이 아니어도).
    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  { 5, 0},
                                                  { 6, 0},
                                                  { 7, 0},
                                                  {10, 0},
                                                  {12, 1}
    } ),
                                              settings )
                         ._yeol,
                     1 );
    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  { 1, 1},
                                                  { 4, 1},
                                                  { 6, 1},
                                                  { 9, 1},
                                                  {12, 2}
    } ),
                                              settings )
                         ._tti,
                     1 );
    // 국진 열끗 — 설정에 따라 열끗 또는 쌍피.
    MatgoSettings septemberAsPi             = settings;
    septemberAsPi._bSeptemberYeolAsDoublePi = SW_TRUE;
    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  { 9, 0 }
    } ),
                                              settings )
                         ._yeolCount,
                     1 );
    SW_EXPECT_EQUAL( MatgoGame::computeScore( makeHwatuPile( {
                                                  { 9, 0 }
    } ),
                                              septemberAsPi )
                         ._piCount,
                     2 );

    // 진 쪽: 광 없음 · 피 5 장 → 피박 × 광박.
    const HwatuScore  loser  = MatgoGame::computeScore( makeHwatuPile( {
                                                          {10, 2},
                                                          {10, 3},
                                                          { 4, 2},
                                                          { 4, 3},
                                                          { 2, 2}
    } ),
                                                        settings );
    const MatgoPayout payout = MatgoGame::computePayout( winner, loser, 0, settings );
    SW_EXPECT_TRUE( payout._bPibak == SW_TRUE );
    SW_EXPECT_TRUE( payout._bGwangbak == SW_TRUE );
    SW_EXPECT_EQUAL( payout._total, 44 ); // 11 × 2 × 2
    MatgoSettings noPibak = settings;
    noPibak._bPibak       = SW_FALSE;
    SW_EXPECT_EQUAL( MatgoGame::computePayout( winner, loser, 0, noPibak )._total, 22 ); // 피박을 끄면 진다
    // 고 — 1 고 +1, 3 고 +3 에 × 2.
    SW_EXPECT_EQUAL( MatgoGame::computePayout( winner, loser, 1, settings )._total, 48 );  // 12 × 4
    SW_EXPECT_EQUAL( MatgoGame::computePayout( winner, loser, 3, settings )._total, 112 ); // 14 × 2 × 4
    SW_EXPECT_EQUAL( MatgoGame::computePayout( winner, loser, 4, settings )._multiplier, 16 );
}

SW_TEST_CASE( CardGameTest, MatgoPpeokSweepJjokAndGoStopFlow )
{
    // 첫째 판 — 0 이 1 월로 뻑, 1 이 넷째로 뻑을 먹고(피 뺏기) 바닥을 쓸어(피 뺏기), 0 이 쪽.
    MatgoLayout layout;
    layout._listHand = {
        makeHwatuPile( {{ 1, 0 }, { 10, 2 }}
         ), makeHwatuPile( {{ 1, 1 }, { 12, 2 }}
         )
    };
    layout._listCaptured = {
        makeHwatuPile( { { 7, 2 }, { 7, 3 }, { 11, 1 } }
         ), CardPile{}
    };
    layout._floor = makeHwatuPile( {
        {1, 2},
        {6, 2}
    } );
    CardDeckUtil::makePileInDrawOrder( { makeHwatu( 1, 3 ), makeHwatu( 6, 3 ), makeHwatu( 10, 3 ), makeHwatu( 2, 2 ) }, layout._drawPile );
    MatgoGame game;
    game.initializeFromLayout( MatgoSettings::makeMatgo(), layout );

    SW_EXPECT_FALSE( game.playCard( 1, makeHwatu( 1, 1 )._id ) ); // 차례가 아니다
    SW_EXPECT_FALSE( game.playCard( 0, makeHwatu( 5, 0 )._id ) ); // 손에 없다
    SW_EXPECT_TRUE( game.playCard( 0, makeHwatu( 1, 0 )._id ) );
    vector<MatgoEvent> listEvent;
    game.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MatgoEvent::Kind::Ppeok ), 1 );
    SW_EXPECT_EQUAL( game.getFloor().getCount(), 4 ); // 1 월 셋 + 6 월
    SW_EXPECT_EQUAL( game.getPlayer( 0 )._captured.getCount(), 3 );

    SW_EXPECT_TRUE( game.playCard( 1, makeHwatu( 1, 1 )._id ) );
    game.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MatgoEvent::Kind::PpeokEaten ), 1 );
    SW_EXPECT_EQUAL( countEvents( listEvent, MatgoEvent::Kind::Sweep ), 1 );
    SW_EXPECT_EQUAL( countEvents( listEvent, MatgoEvent::Kind::PiStolen ), 2 );
    SW_EXPECT_TRUE( game.getFloor().isEmpty() );
    SW_EXPECT_EQUAL( game.getPlayer( 1 )._captured.getCount(), 8 ); // 1 월 넷 · 6 월 둘 · 뺏은 피 둘
    // 홑피부터 뺏겨 쌍피만 남았다.
    SW_ASSERT_TRUE( game.getPlayer( 0 )._captured.getCount() == 1 );
    SW_EXPECT_TRUE( game.getPlayer( 0 )._captured.getAt( 0 ) == makeHwatu( 11, 1 ) );

    SW_EXPECT_TRUE( game.playCard( 0, makeHwatu( 10, 2 )._id ) ); // 빈 바닥에 놓았는데 뒤집은 패가 같은 월 — 쪽
    game.drainEvents( listEvent );
    SW_EXPECT_EQUAL( countEvents( listEvent, MatgoEvent::Kind::Jjok ), 1 );
    SW_EXPECT_EQUAL( game.getPlayer( 0 )._captured.getCount(), 5 ); // 쌍피 · 10 월 둘 · 쪽 피 · 싹쓸이 피
    SW_EXPECT_TRUE( game.playCard( 1, makeHwatu( 12, 2 )._id ) );
    SW_EXPECT_TRUE( game.getPhase() == MatgoPhase::Finished ); // 손패가 다 떨어졌고 아무도 7 점 아님 — 나가리
    SW_EXPECT_EQUAL( game.getWinner(), -1 );

    // 둘째 판 — 7 점이 나 고, 점수가 더 올라 스톱. 1 고 +1, 광박 × 2.
    MatgoLayout goLayout;
    goLayout._listHand = {
        makeHwatuPile( { { 5, 2 }, { 7, 1 }, { 12, 3 } }
         ), makeHwatuPile( { { 11, 2 }, { 11, 3 } }
         )
    };
    goLayout._listCaptured = {
        makeHwatuPile( { { 1, 0 }, { 3, 0 }, { 8, 0 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 } }
         ), CardPile{}
    };
    goLayout._floor = makeHwatuPile( {
        {5, 1},
        {7, 2}
    } );
    CardDeckUtil::makePileInDrawOrder( { makeHwatu( 9, 2 ), makeHwatu( 10, 2 ), makeHwatu( 6, 2 ), makeHwatu( 6, 3 ) }, goLayout._drawPile );
    MatgoGame goGame;
    goGame.initializeFromLayout( MatgoSettings::makeMatgo(), goLayout );
    SW_EXPECT_TRUE( goGame.playCard( 0, makeHwatu( 5, 2 )._id ) ); // 띠 다섯 → 3 광 3 + 홍단 3 + 띠 1 = 7
    SW_EXPECT_TRUE( goGame.getPhase() == MatgoPhase::GoStopChoice );
    SW_EXPECT_EQUAL( goGame.computePlayerScore( 0 )._total, 7 );
    SW_EXPECT_FALSE( goGame.playCard( 1, makeHwatu( 11, 2 )._id ) ); // 고/스톱을 기다린다
    SW_EXPECT_TRUE( goGame.declareGo( 0 ) );
    SW_EXPECT_EQUAL( goGame.getPlayer( 0 )._goCount, 1 );
    SW_EXPECT_TRUE( goGame.playCard( 1, makeHwatu( 11, 2 )._id ) );
    SW_EXPECT_TRUE( goGame.getPhase() == MatgoPhase::Play );
    SW_EXPECT_TRUE( goGame.playCard( 0, makeHwatu( 7, 1 )._id ) ); // 초단 + 띠 여섯 → 11
    SW_EXPECT_TRUE( goGame.getPhase() == MatgoPhase::GoStopChoice );
    CardAction stop;
    stop._kind = static_cast<uint8>( MatgoActionKind::Stop );
    SW_EXPECT_TRUE( goGame.applyAction( 0, stop ) );
    SW_EXPECT_EQUAL( goGame.getWinner(), 0 );
    SW_EXPECT_EQUAL( goGame.getSettlement()[0], 24 ); // (11 + 1) × 광박 2
    SW_EXPECT_EQUAL( goGame.getSettlement()[1], -24 );

    // 씨앗 판 — 같은 씨앗이면 같은 손패, 고스톱은 셋에게 7 장 · 바닥 6 장.
    MatgoGame seededA;
    MatgoGame seededB;
    seededA.initialize( MatgoSettings::makeGoStop(), 99 );
    seededB.initialize( MatgoSettings::makeGoStop(), 99 );
    SW_EXPECT_EQUAL( seededA.getPlayerCount(), 3 );
    SW_EXPECT_EQUAL( seededA.getPlayer( 2 )._hand.getCount(), 7 );
    SW_EXPECT_EQUAL( seededA.getFloor().getCount(), 6 );
    SW_EXPECT_EQUAL( seededA.getDrawPile().getCount(), 21 );
    SW_EXPECT_TRUE( seededA.getPlayer( 1 )._hand.getAt( 3 ) == seededB.getPlayer( 1 )._hand.getAt( 3 ) );
}

SW_TEST_CASE( CardGameTest, KlondikeRejectsIllegalMovesUndoesAndAutoCompletes )
{
    KlondikeState state;
    state._arrTableau[0]       = makePlayingPile( { "5h", "9s" } );
    state._arrFaceDownCount[0] = 1;
    state._arrTableau[1]       = makePlayingPile( { "8h" } );
    state._arrTableau[2]       = makePlayingPile( { "8c" } );
    state._arrTableau[4]       = makePlayingPile( { "3d", "Ac" } );
    state._arrFaceDownCount[4] = 1;
    state._stock               = makePlayingPile( { "2c", "4d", "6d", "7d" } );
    state._waste               = makePlayingPile( { "Qd" } );
    KlondikeSettings settings;
    settings._drawCount  = 3;
    settings._maxRecycle = 0;
    KlondikeGame game;
    game.initializeFromState( settings, state );

    SW_EXPECT_FALSE( game.moveTableauToTableau( 2, 1, 0 ) ); // 검정 위에 검정
    SW_EXPECT_FALSE( game.moveWasteToTableau( 3 ) );         // 빈 열엔 킹만
    SW_EXPECT_FALSE( game.moveTableauToTableau( 0, 2, 3 ) ); // 뒤집힌 패는 못 옮긴다
    SW_EXPECT_FALSE( game.moveTableauToFoundation( 1 ) );    // 파운데이션은 에이스부터
    SW_EXPECT_EQUAL( game.getUndoCount(), 0 );               // 거절된 옮기기는 기록이 없다

    SW_EXPECT_TRUE( game.moveTableauToTableau( 1, 1, 0 ) ); // 검정 9 위에 빨강 8
    SW_EXPECT_EQUAL( game.getState()._arrTableau[0].getCount(), 3 );
    SW_EXPECT_TRUE( game.undo() );
    SW_EXPECT_EQUAL( game.getState()._arrTableau[0].getCount(), 2 );
    SW_EXPECT_EQUAL( game.getState()._arrTableau[1].getCount(), 1 );

    // 에이스를 올리면 아래 뒤집힌 패가 앞면이 되고, 되돌리면 다시 뒤집힌다.
    SW_EXPECT_TRUE( game.moveTableauToFoundation( 4 ) );
    SW_EXPECT_EQUAL( game.getState()._arrFaceDownCount[4], 0 );
    SW_EXPECT_TRUE( game.getState()._arrFoundation[StandardSuit::kClubs].getTop() == makePlayingCard( "Ac" ) );
    SW_EXPECT_TRUE( game.undo() );
    SW_EXPECT_EQUAL( game.getState()._arrFaceDownCount[4], 1 );
    SW_EXPECT_TRUE( game.getState()._arrFoundation[StandardSuit::kClubs].isEmpty() );

    // 3 장 뽑기 — 4 장 스톡이면 3, 1, 그다음은 되돌리기인데 횟수 0 이라 못 한다.
    SW_EXPECT_TRUE( game.drawStock() );
    SW_EXPECT_EQUAL( game.getState()._waste.getCount(), 4 );
    SW_EXPECT_TRUE( game.getState()._waste.getTop() == makePlayingCard( "4d" ) );
    SW_EXPECT_TRUE( game.drawStock() );
    SW_EXPECT_TRUE( game.getState()._stock.isEmpty() );
    SW_EXPECT_FALSE( game.drawStock() );
    SW_EXPECT_FALSE( game.canAutoComplete() );
    SW_EXPECT_FALSE( game.isWon() );

    // 거의 끝난 판 — 자동 완료로 이긴다.
    KlondikeState endState;
    const utf8*   arrSuit[] = { "c", "d", "h", "s" };
    const utf8*   arrRank   = "A23456789TJQK";
    for ( int32 suit = 0; suit < 4; ++suit )
    {
        const int32 topRank = suit < 2 ? 13 : ( suit == 2 ? 12 : 11 );
        for ( int32 rank = 1; rank <= topRank; ++rank )
        {
            const utf8 arrText[3] = { arrRank[rank - 1], arrSuit[suit][0], 0 };
            endState._arrFoundation[suit].push( makePlayingCard( arrText ) );
        }
    }
    endState._arrTableau[0] = makePlayingPile( { "Ks", "Qs" } );
    endState._arrTableau[2] = makePlayingPile( { "Kh" } );
    KlondikeGame endGame;
    endGame.initializeFromState( KlondikeSettings{}, endState );
    SW_EXPECT_TRUE( endGame.canAutoComplete() );
    SW_EXPECT_EQUAL( endGame.autoComplete(), 3 );
    SW_EXPECT_TRUE( endGame.isWon() );
    SW_EXPECT_TRUE( endGame.undo() ); // 자동 완료는 한 번에 되돌린다
    SW_EXPECT_FALSE( endGame.isWon() );
    SW_EXPECT_EQUAL( endGame.getState()._arrTableau[0].getCount(), 2 );

    // 씨앗 판 — 열 c 에 c + 1 장, 스톡 24 장.
    KlondikeGame seeded;
    seeded.initialize( KlondikeSettings{}, 5 );
    SW_EXPECT_EQUAL( seeded.getState()._arrTableau[6].getCount(), 7 );
    SW_EXPECT_EQUAL( seeded.getState()._arrFaceDownCount[6], 6 );
    SW_EXPECT_EQUAL( seeded.getState()._stock.getCount(), 24 );
}

SW_TEST_CASE( CardGameTest, UnoSkipReverseDrawFourUnoPenaltyAndStacking )
{
    UnoLayout layout;
    layout._listHand = { CardPile{}, CardPile{}, CardPile{}, CardPile{} };
    layout._listHand[0].push( makeUno( UnoColor::Red, UnoValue::Skip, 0 ) );
    layout._listHand[0].push( makeUnoNumber( UnoColor::Blue, 7, 1 ) );
    layout._listHand[0].push( makeUno( UnoColor::Wild, UnoValue::WildDrawFour, 2 ) );
    layout._listHand[0].push( makeUnoNumber( UnoColor::Green, 1, 3 ) );
    layout._listHand[1].push( makeUnoNumber( UnoColor::Red, 3, 10 ) );
    layout._listHand[1].push( makeUno( UnoColor::Wild, UnoValue::WildDrawFour, 11 ) );
    layout._listHand[1].push( makeUnoNumber( UnoColor::Green, 8, 12 ) );
    layout._listHand[2].push( makeUno( UnoColor::Red, UnoValue::Reverse, 20 ) );
    layout._listHand[2].push( makeUnoNumber( UnoColor::Green, 2, 21 ) );
    layout._listHand[2].push( makeUnoNumber( UnoColor::Yellow, 9, 22 ) );
    layout._listHand[3].push( makeUnoNumber( UnoColor::Yellow, 1, 30 ) );
    layout._listHand[3].push( makeUnoNumber( UnoColor::Yellow, 2, 31 ) );
    layout._discardPile.push( makeUnoNumber( UnoColor::Red, 5, 40 ) );
    layout._color = UnoColor::Red;
    CardDeckUtil::makePileInDrawOrder( { makeUnoNumber( UnoColor::Green, 5, 50 ), makeUnoNumber( UnoColor::Green, 6, 51 ), makeUnoNumber( UnoColor::Green, 7, 52 ),
                                         makeUnoNumber( UnoColor::Yellow, 7, 53 ), makeUnoNumber( UnoColor::Blue, 1, 54 ), makeUnoNumber( UnoColor::Blue, 2, 55 ),
                                         makeUnoNumber( UnoColor::Red, 9, 56 ) },
                                       layout._drawPile );
    UnoGame game;
    game.initializeFromLayout( UnoSettings{}, layout );

    SW_EXPECT_FALSE( game.playCard( 0, 1 ) ); // 파랑 7 은 빨강 5 위에 못 낸다
    SW_EXPECT_TRUE( game.playCard( 0, 0 ) );  // 빨강 스킵 — 1 을 건너뛴다
    SW_EXPECT_EQUAL( game.getCurrentPlayer(), 2 );
    SW_EXPECT_TRUE( game.playCard( 2, 20 ) ); // 리버스 — 방향이 바뀌어 1 차례
    SW_EXPECT_EQUAL( game.getDirection(), -1 );
    SW_EXPECT_EQUAL( game.getCurrentPlayer(), 1 );
    SW_EXPECT_FALSE( game.playCard( 1, 11, UnoColor::Blue ) ); // 빨강이 있으면 +4 금지
    SW_EXPECT_TRUE( game.playCard( 1, 10 ) );
    SW_EXPECT_EQUAL( game.getCurrentPlayer(), 0 );
    SW_EXPECT_FALSE( game.playCard( 0, 2 ) );                 // 와일드는 색을 골라야 한다
    SW_EXPECT_TRUE( game.playCard( 0, 2, UnoColor::Green ) ); // +4 — 3 이 넷 뽑고 건너뛴다
    SW_EXPECT_EQUAL( game.getHand( 3 ).getCount(), 6 );
    SW_EXPECT_EQUAL( game.getCurrentPlayer(), 2 );
    SW_EXPECT_TRUE( game.getCurrentColor() == UnoColor::Green );

    // 우노를 외치지 않고 한 장 — 다음 사람이 움직이기 전에 잡히면 둘 뽑는다.
    SW_EXPECT_TRUE( game.playCard( 2, 21 ) );
    SW_EXPECT_TRUE( game.callUno( 0 ) );
    SW_EXPECT_EQUAL( game.getHand( 2 ).getCount(), 3 );
    SW_EXPECT_FALSE( game.callUno( 3 ) ); // 한 번만
    // 외치면 잡을 수 없다.
    SW_EXPECT_TRUE( game.playCard( 1, 12, UnoColor::Wild, true ) );
    SW_EXPECT_FALSE( game.callUno( 3 ) );
    SW_EXPECT_TRUE( game.playCard( 0, 3, UnoColor::Wild, true ) );
    SW_EXPECT_TRUE( game.drawCard( 3 ) );
    SW_EXPECT_EQUAL( game.getHand( 3 ).getCount(), 7 );
    SW_EXPECT_TRUE( game.playCard( 2, 54 ) ); // 파랑 1 — 숫자가 같다(초록 1 위)
    // 1 의 마지막 +4 — 파랑이 없으니 낼 수 있고 이긴다.
    SW_EXPECT_TRUE( game.playCard( 1, 11, UnoColor::Red ) );
    SW_EXPECT_EQUAL( game.getWinner(), 1 );
    SW_EXPECT_FALSE( game.drawCard( 0 ) );
    SW_EXPECT_EQUAL( UnoGame::computeHandPoints( game.getHand( 3 ) ), 1 + 2 + 5 + 6 + 7 + 7 + 9 );

    // +2 쌓기(설정) · 둘이면 리버스는 스킵.
    UnoSettings stackSettings;
    stackSettings._playerCount   = 2;
    stackSettings._bStackDrawTwo = SW_TRUE;
    UnoLayout stackLayout;
    stackLayout._listHand = { CardPile{}, CardPile{} };
    stackLayout._listHand[0].push( makeUno( UnoColor::Red, UnoValue::DrawTwo, 0 ) );
    stackLayout._listHand[0].push( makeUnoNumber( UnoColor::Red, 1, 1 ) );
    stackLayout._listHand[0].push( makeUnoNumber( UnoColor::Blue, 4, 2 ) );
    stackLayout._listHand[1].push( makeUno( UnoColor::Green, UnoValue::DrawTwo, 10 ) );
    stackLayout._listHand[1].push( makeUnoNumber( UnoColor::Green, 3, 11 ) );
    stackLayout._listHand[1].push( makeUno( UnoColor::Green, UnoValue::Reverse, 12 ) );
    stackLayout._listHand[1].push( makeUnoNumber( UnoColor::Green, 9, 13 ) );
    stackLayout._discardPile.push( makeUnoNumber( UnoColor::Red, 5, 20 ) );
    stackLayout._color = UnoColor::Red;
    UnoGame::makeDeck( stackLayout._drawPile );
    UnoGame stackGame;
    stackGame.initializeFromLayout( stackSettings, stackLayout, 3 );
    SW_EXPECT_TRUE( stackGame.playCard( 0, 0 ) );
    SW_EXPECT_EQUAL( stackGame.getPendingDraw(), 2 );
    SW_EXPECT_FALSE( stackGame.playCard( 1, 11 ) ); // 벌칙이 쌓였으면 +2 만
    SW_EXPECT_TRUE( stackGame.playCard( 1, 10 ) );
    SW_EXPECT_EQUAL( stackGame.getPendingDraw(), 4 );
    SW_EXPECT_FALSE( stackGame.playCard( 0, 1 ) );
    SW_EXPECT_TRUE( stackGame.drawCard( 0 ) ); // 쌓인 넷을 다 뽑는다
    SW_EXPECT_EQUAL( stackGame.getHand( 0 ).getCount(), 6 );
    SW_EXPECT_EQUAL( stackGame.getPendingDraw(), 0 );
    SW_EXPECT_TRUE( stackGame.playCard( 1, 12 ) ); // 둘이면 리버스 = 다시 내 차례
    SW_EXPECT_EQUAL( stackGame.getCurrentPlayer(), 1 );

    // 씨앗 판 — 108 장, 넷에게 일곱, 첫 장은 와일드가 아니다.
    CardPile fullDeck;
    UnoGame::makeDeck( fullDeck );
    SW_EXPECT_EQUAL( fullDeck.getCount(), 108 );
    UnoGame seeded;
    seeded.initialize( UnoSettings{}, 11 );
    SW_EXPECT_EQUAL( seeded.getHand( 3 ).getCount(), 7 );
    SW_EXPECT_EQUAL( seeded.getDrawPile().getCount(), 108 - 28 - 1 );
    SW_EXPECT_TRUE( seeded.getCurrentColor() != UnoColor::Wild );
}

SW_TEST_CASE( CardGameTest, DeckBattlePlaysATurnWithEnergyBlockExhaustAndReshuffle )
{
    constexpr const utf8* kDeckXml = R"(
<DeckBattleCatalog>
  <Card id="strike" name="Strike" cost="1" effects="Damage:6"/>
  <Card id="defend" name="Defend" cost="1" effects="Block:5"/>
  <Card id="prepare" name="Prepare" cost="0" effects="Draw:2, Energy:1" exhaust="true"/>
</DeckBattleCatalog>
)";
    DeckBattleCatalog     catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kDeckXml, "CardGameTest" ) );
    SW_EXPECT_EQUAL( catalog.getCount(), 3 );
    SW_ASSERT_NOT_NULL( catalog.findCard( "prepare" ) );
    SW_EXPECT_EQUAL( static_cast<int32>( catalog.findCard( "prepare" )->_listEffect.size() ), 2 );

    DeckBattleSettings settings;
    settings._drawPerTurn = 7;
    DeckBattleEnemy enemy;
    enemy._hp         = 30;
    enemy._listIntent = {
        DeckBattleEffect{10, DeckBattleEffectKind::Damage},
        DeckBattleEffect{ 4,  DeckBattleEffectKind::Block}
    };
    const vector<hashed_string> listDeck = { "prepare", "strike", "strike", "strike", "defend", "defend", "defend", "unknown" };
    DeckBattle                  battle;
    SW_ASSERT_TRUE( battle.initialize( &catalog, settings, listDeck, enemy, 2024 ) );
    SW_EXPECT_EQUAL( battle.getHand().getCount(), 7 ); // 모르는 id 는 건너뛴다
    SW_EXPECT_EQUAL( battle.getEnergy(), 3 );

    // 손에서 정의 id 로 찾아 쓴다.
    struct Finder
    {
        static int32 findInHand( const DeckBattle& target, const utf8* pId )
        {
            for ( int32 index = 0; index < target.getHand().getCount(); ++index )
            {
                if ( target.getCardDef( target.getHand().getAt( index ) )->_id == hashed_string( pId ) )
                    return index;
            }
            return -1;
        }
    };
    SW_EXPECT_TRUE( battle.playCard( Finder::findInHand( battle, "prepare" ) ) ); // 0 에너지 · 에너지 +1 · 뽑을 것이 없다 · 소멸
    SW_EXPECT_EQUAL( battle.getEnergy(), 4 );
    SW_EXPECT_EQUAL( battle.getExhaustPile().getCount(), 1 );
    SW_EXPECT_TRUE( battle.playCard( Finder::findInHand( battle, "strike" ) ) );
    SW_EXPECT_TRUE( battle.playCard( Finder::findInHand( battle, "strike" ) ) );
    SW_EXPECT_EQUAL( battle.getEnemy()._hp, 18 );
    SW_EXPECT_TRUE( battle.playCard( Finder::findInHand( battle, "defend" ) ) );
    SW_EXPECT_TRUE( battle.playCard( Finder::findInHand( battle, "defend" ) ) );
    SW_EXPECT_EQUAL( battle.getPlayerBlock(), 10 );
    SW_EXPECT_EQUAL( battle.getEnergy(), 0 );
    SW_EXPECT_FALSE( battle.playCard( Finder::findInHand( battle, "strike" ) ) ); // 에너지가 없다
    SW_EXPECT_EQUAL( battle.getDiscardPile().getCount(), 4 );

    battle.endTurn(); // 적이 10 으로 친다 — 방어 10 이 다 막는다. 버린 더미를 섞어 둘째 턴을 뽑는다.
    SW_EXPECT_EQUAL( battle.getPlayerHp(), 50 );
    SW_EXPECT_EQUAL( battle.getTurn(), 2 );
    SW_EXPECT_EQUAL( battle.getPlayerBlock(), 0 );
    SW_EXPECT_EQUAL( battle.getEnergy(), 3 );
    SW_EXPECT_EQUAL( battle.getHand().getCount(), 6 ); // 소멸한 한 장은 돌아오지 않는다
    SW_EXPECT_EQUAL( battle.getExhaustPile().getCount(), 1 );

    battle.endTurn(); // 적이 방어 4
    SW_EXPECT_EQUAL( battle.getEnemy()._block, 4 );
    SW_EXPECT_TRUE( battle.playCard( Finder::findInHand( battle, "strike" ) ) );
    SW_EXPECT_EQUAL( battle.getEnemy()._block, 0 );
    SW_EXPECT_EQUAL( battle.getEnemy()._hp, 16 ); // 6 − 방어 4
    battle.endTurn();                             // 의도가 돌아 다시 10 — 방어 없이 맞는다
    SW_EXPECT_EQUAL( battle.getPlayerHp(), 40 );

    // 같은 씨앗이면 같은 손.
    DeckBattle twin;
    SW_ASSERT_TRUE( twin.initialize( &catalog, DeckBattleSettings{}, listDeck, enemy, 2024 ) );
    DeckBattle twinB;
    SW_ASSERT_TRUE( twinB.initialize( &catalog, DeckBattleSettings{}, listDeck, enemy, 2024 ) );
    bool bSame = twin.getHand().getCount() == twinB.getHand().getCount();
    for ( int32 index = 0; bSame && index < twin.getHand().getCount(); ++index )
        bSame = twin.getHand().getAt( index ) == twinB.getHand().getAt( index );
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_EQUAL( twin.getDrawPile().getCount(), 2 ); // 7 장 중 다섯을 뽑았다
}
