#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

// CardDeckTest — 카드 게임 공통: 트럼프 덱 구성, 더미의 뽑기 · 옮기기(순서 뒤집힘) · 번호로 빼기 · 맨 아래 끼우기, 씨앗 셔플의 결정성,
// 돌아가며 나누기와 모자란 덱, 행동 바이트(10 B, 리틀 엔디언, 태그) 왕복과 거절, 더미 상태 왕복과 깨진 상태. 키트 GF_CardGame(nogpu).

namespace
{
    sw::Card makeCard( uint16 cardID, uint8 suit, uint8 rank )
    {
        return sw::Card{ cardID, suit, rank };
    }

    sw::vector<uint16> collectIDs( const sw::CardPile& pile )
    {
        sw::vector<uint16> listID;
        for ( const sw::Card& card : pile.getCards() )
        {
            listID.push_back( card._id );
        }
        return listID;
    }
} // namespace

/**
 * @brief [CardDeckTest] 트럼프 덱은 무늬 순 · 숫자 순으로 번호 0..51 이고 조커는 52 부터 무늬 kJoker · 숫자 0 이다. 다시 만들면 비우고 채운다
 */
SW_TEST_CASE( CardDeckTest, StandardDeckHasFiftyTwoOrderedCardsAndJokers )
{
    sw::CardPile pile;
    pile.push( makeCard( 999, 0, 0 ) );
    sw::CardDeckUtil::makeStandardDeck( pile, 2 );
    SW_REQUIRE_EQUAL( 54, pile.getCount() );
    SW_EXPECT_TRUE( pile.getAt( 0 ) == makeCard( 0, sw::StandardSuit::kClubs, sw::StandardRank::kAce ) );
    SW_EXPECT_TRUE( pile.getAt( 12 ) == makeCard( 12, sw::StandardSuit::kClubs, sw::StandardRank::kKing ) );
    SW_EXPECT_TRUE( pile.getAt( 13 ) == makeCard( 13, sw::StandardSuit::kDiamonds, sw::StandardRank::kAce ) );
    SW_EXPECT_TRUE( pile.getAt( 51 ) == makeCard( 51, sw::StandardSuit::kSpades, sw::StandardRank::kKing ) );
    SW_EXPECT_TRUE( pile.getAt( 52 ) == makeCard( 52, sw::StandardSuit::kJoker, 0 ) );
    SW_EXPECT_FALSE( pile.containsID( 999 ) );

    SW_EXPECT_TRUE( sw::CardDeckUtil::isRedSuit( sw::StandardSuit::kDiamonds ) );
    SW_EXPECT_TRUE( sw::CardDeckUtil::isRedSuit( sw::StandardSuit::kHearts ) );
    SW_EXPECT_FALSE( sw::CardDeckUtil::isRedSuit( sw::StandardSuit::kClubs ) );
    SW_EXPECT_FALSE( sw::CardDeckUtil::isRedSuit( sw::StandardSuit::kJoker ) );
}

/**
 * @brief [CardDeckTest] 맨 위는 목록의 끝이다: 뽑기 · 옮기기는 끝에서 가져가고(옮기면 순서가 뒤집힌다), 빈 더미는 뽑지 못하며 빈 카드를 맨 위로 보인다
 */
SW_TEST_CASE( CardDeckTest, PileDrawsFromTopAndDrawIntoReversesOrder )
{
    sw::CardPile pile;
    sw::CardDeckUtil::makePileInDrawOrder( { makeCard( 1, 0, 1 ), makeCard( 2, 0, 2 ), makeCard( 3, 0, 3 ), makeCard( 4, 0, 4 ) }, pile );
    SW_EXPECT_EQUAL( uint16( 1 ), pile.getTop()._id ); // 첫 카드가 먼저 뽑힌다

    sw::CardPile hand;
    SW_EXPECT_EQUAL( 2, pile.drawInto( hand, 2 ) );
    SW_EXPECT_TRUE( collectIDs( hand ) == sw::vector<uint16>( { 1, 2 } ) );
    SW_EXPECT_EQUAL( uint16( 2 ), hand.getTop()._id ); // 나중에 옮긴 것이 위
    SW_EXPECT_EQUAL( 2, pile.drawInto( hand, 10 ) );   // 남은 만큼만
    SW_EXPECT_TRUE( pile.isEmpty() );

    sw::Card drawn;
    SW_EXPECT_FALSE( pile.draw( drawn ) );
    SW_EXPECT_FALSE( pile.getTop().isValid() );
    SW_EXPECT_EQUAL( 0, pile.drawInto( hand, 1 ) );

    hand.pushBottom( makeCard( 9, 1, 9 ) );
    SW_EXPECT_EQUAL( uint16( 9 ), hand.getAt( 0 )._id );
    SW_REQUIRE_TRUE( hand.draw( drawn ) );
    SW_EXPECT_EQUAL( uint16( 4 ), drawn._id );
}

/**
 * @brief [CardDeckTest] 번호로 빼기 · 위치로 빼기는 나머지의 위아래 순서를 지키고, 없는 번호는 false · -1 이다
 */
SW_TEST_CASE( CardDeckTest, TakeByIDKeepsRemainingOrder )
{
    sw::CardPile pile;
    for ( uint16 cardID = 10; cardID < 15; ++cardID )
    {
        pile.push( makeCard( cardID, 2, static_cast<uint8>( cardID - 9 ) ) );
    }
    sw::Card taken;
    SW_REQUIRE_TRUE( pile.takeByID( 12, taken ) );
    SW_EXPECT_TRUE( taken == makeCard( 12, 2, 3 ) );
    SW_EXPECT_TRUE( collectIDs( pile ) == sw::vector<uint16>( { 10, 11, 13, 14 } ) );
    SW_EXPECT_FALSE( pile.takeByID( 12, taken ) );
    SW_EXPECT_EQUAL( -1, pile.findIndexByID( 12 ) );
    SW_EXPECT_EQUAL( 2, pile.findIndexByID( 13 ) );

    const sw::Card removed = pile.removeAt( 0 );
    SW_EXPECT_EQUAL( uint16( 10 ), removed._id );
    SW_EXPECT_TRUE( collectIDs( pile ) == sw::vector<uint16>( { 11, 13, 14 } ) );
}

/**
 * @brief [CardDeckTest] 같은 씨앗의 셔플은 같은 순서이고, 다른 씨앗은 다른 순서다. 섞어도 카드 집합(번호 0..51 한 번씩)은 그대로다
 */
SW_TEST_CASE( CardDeckTest, ShuffleIsDeterministicPerSeedAndKeepsCards )
{
    sw::CardPile first;
    sw::CardPile second;
    sw::CardPile other;
    sw::CardDeckUtil::makeStandardDeck( first );
    sw::CardDeckUtil::makeStandardDeck( second );
    sw::CardDeckUtil::makeStandardDeck( other );
    sw::GameRandom randomA( 1234u );
    sw::GameRandom randomB( 1234u );
    sw::GameRandom randomC( 4321u );
    first.shuffle( randomA );
    second.shuffle( randomB );
    other.shuffle( randomC );

    SW_EXPECT_TRUE( collectIDs( first ) == collectIDs( second ) );
    SW_EXPECT_FALSE( collectIDs( first ) == collectIDs( other ) );

    sw::CardPile ordered;
    sw::CardDeckUtil::makeStandardDeck( ordered );
    SW_EXPECT_FALSE( collectIDs( first ) == collectIDs( ordered ) ); // 52 장이 그대로일 확률은 무시할 만하다

    bool arrSeen[52] = {};
    for ( const uint16 cardID : collectIDs( first ) )
    {
        SW_REQUIRE_TRUE( cardID < 52 );
        SW_EXPECT_FALSE( arrSeen[cardID] );
        arrSeen[cardID] = true;
    }
    SW_EXPECT_EQUAL( 52, first.getCount() );
}

/**
 * @brief [CardDeckTest] 나누기는 맨 위부터 한 장씩 돌아가며(0 번 손패 먼저) 준다. 덱이 모자라면 false 이고 나눈 데까지는 남는다
 */
SW_TEST_CASE( CardDeckTest, DealRoundRobinAndReportsShortDeck )
{
    sw::CardPile deck;
    sw::CardDeckUtil::makePileInDrawOrder( { makeCard( 1, 0, 1 ), makeCard( 2, 0, 2 ), makeCard( 3, 0, 3 ), makeCard( 4, 0, 4 ), makeCard( 5, 0, 5 ) }, deck );
    sw::vector<sw::CardPile> listHand( 2 );
    SW_REQUIRE_TRUE( deck.deal( listHand, 2 ) );
    SW_EXPECT_TRUE( collectIDs( listHand[0] ) == sw::vector<uint16>( { 1, 3 } ) );
    SW_EXPECT_TRUE( collectIDs( listHand[1] ) == sw::vector<uint16>( { 2, 4 } ) );
    SW_EXPECT_EQUAL( 1, deck.getCount() );

    SW_EXPECT_FALSE( deck.deal( listHand, 1 ) ); // 한 장으로 둘에게 줄 수 없다
    SW_EXPECT_EQUAL( 3, listHand[0].getCount() );
    SW_EXPECT_EQUAL( 2, listHand[1].getCount() );
    SW_EXPECT_TRUE( deck.isEmpty() );
}

/**
 * @brief [CardDeckTest] 행동 바이트는 [태그][종류][카드 LE16][대상 LE16][금액 LE32] 10 B 이고 음수 금액까지 되읽는다. 길이 · 태그가 틀리면 거절하고 값은 그대로다
 */
SW_TEST_CASE( CardDeckTest, ActionBytesRoundTripAndRejectBadFrames )
{
    sw::CardAction action;
    action._kind     = 3;
    action._cardID   = 0x1234;
    action._targetID = sw::Card::kNoCard;
    action._amount   = -500;
    sw::vector<uint8> listByte;
    sw::CardActionUtil::encodeAction( action, listByte );
    SW_REQUIRE_EQUAL( size_t( sw::CardActionUtil::kEncodedSize ), listByte.size() );
    SW_EXPECT_EQUAL( sw::CardActionUtil::kTag, listByte[0] );
    SW_EXPECT_EQUAL( uint8( 0x34 ), listByte[2] ); // 리틀 엔디언
    SW_EXPECT_EQUAL( uint8( 0x12 ), listByte[3] );

    sw::CardAction decoded;
    SW_REQUIRE_TRUE( sw::CardActionUtil::decodeAction( listByte, decoded ) );
    SW_EXPECT_EQUAL( uint8( 3 ), decoded._kind );
    SW_EXPECT_EQUAL( uint16( 0x1234 ), decoded._cardID );
    SW_EXPECT_EQUAL( sw::Card::kNoCard, decoded._targetID );
    SW_EXPECT_EQUAL( -500, decoded._amount );

    sw::CardAction untouched;
    untouched._amount            = 77;
    sw::vector<uint8> listBadTag = listByte;
    listBadTag[0]                = 0xCB;
    sw::vector<uint8> listShort( listByte.begin(), listByte.end() - 1 );
    sw::vector<uint8> listLong = listByte;
    listLong.push_back( 0 );
    SW_EXPECT_FALSE( sw::CardActionUtil::decodeAction( listBadTag, untouched ) );
    SW_EXPECT_FALSE( sw::CardActionUtil::decodeAction( listShort, untouched ) );
    SW_EXPECT_FALSE( sw::CardActionUtil::decodeAction( listLong, untouched ) );
    SW_EXPECT_FALSE( sw::CardActionUtil::decodeAction( {}, untouched ) );
    SW_EXPECT_EQUAL( 77, untouched._amount );
}

/**
 * @brief [CardDeckTest] 더미 상태는 맨 아래부터 그대로 되읽히고, 잘린 상태는 거절하며 더미를 바꾸지 않는다
 */
SW_TEST_CASE( CardDeckTest, PileStateRoundTripsAndRejectsTruncation )
{
    sw::CardPile original;
    sw::CardDeckUtil::makeStandardDeck( original, 1 );
    sw::GameRandom random( 77u );
    original.shuffle( random );

    sw::Archive written;
    original.writeState( written );
    sw::CardPile restored;
    sw::Archive  reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getCards() == original.getCards() );

    sw::CardPile kept;
    kept.push( makeCard( 7, 1, 7 ) );
    sw::Archive truncated( written.getData(), written.getSize() - 3 );
    SW_EXPECT_FALSE( kept.readState( truncated ) );
    SW_REQUIRE_EQUAL( 1, kept.getCount() );
    SW_EXPECT_EQUAL( uint16( 7 ), kept.getTop()._id );

    sw::CardPile empty;
    sw::Archive  writtenEmpty;
    empty.writeState( writtenEmpty );
    sw::Archive readingEmpty( writtenEmpty.getData(), writtenEmpty.getSize() );
    SW_REQUIRE_TRUE( kept.readState( readingEmpty ) );
    SW_EXPECT_TRUE( kept.isEmpty() );
}
