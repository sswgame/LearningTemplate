#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardBattle/CardBattle.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Klondike/KlondikeGame.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Matgo/HwatuDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Matgo/MatgoGame.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Poker/PokerTable.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Uno/UnoGame.h"

// CardGameReadStateTest — 카드 게임 상태 바이트의 신뢰 경계: 더미의 빈 카드 · 겹친 번호, 더미 사이의 복제 카드, 덱에 없는 카드, 음수 · 범위 밖 값을 거절하고 상태를 그대로 둔다.
// 꾸민 바이트는 `writeState` 의 형식을 그대로 손으로 적는다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨린다. 키트 GF_CardGame(nogpu).

namespace
{
    struct CardGameReadStateTestUtil
    {
        static sw::CardPile makeStandardPile( const sw::vector<uint16>& listID )
        {
            sw::CardPile pile;
            for ( const uint16 cardID : listID )
            {
                pile.push( sw::Card{ cardID, static_cast<uint8>( cardID / 13 ), static_cast<uint8>( cardID % 13 + 1 ) } );
            }
            return pile;
        }

        static sw::CardPile makeHwatuPile( const sw::vector<uint16>& listID )
        {
            sw::CardPile pile;
            for ( const uint16 cardID : listID )
            {
                pile.push( sw::HwatuDeck::makeCard( cardID / 4 + 1, cardID % 4 ) );
            }
            return pile;
        }

        template <typename TGame>
        static bool readInto( TGame& inoutGame, const sw::Archive& written )
        {
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutGame.readState( reading );
        }
    };

    /** @brief `KlondikeGame::writeState` 의 판 하나(되돌리기 기록 없음)를 꾸밉니다. */
    struct KlondikeFixture
    {
        sw::KlondikeState _state{};

        bool readInto( sw::KlondikeGame& inoutGame ) const
        {
            sw::Archive written;
            for ( int32 column = 0; column < sw::KlondikeState::kColumnCount; ++column )
            {
                _state._arrTableau[column].writeState( written );
                written << _state._arrFaceDownCount[column];
            }
            for ( const sw::CardPile& foundation : _state._arrFoundation )
            {
                foundation.writeState( written );
            }
            _state._stock.writeState( written );
            _state._waste.writeState( written );
            written << _state._recycleCount;
            written << _state._moveCount;
            written << static_cast<uint32>( 0 );
            return CardGameReadStateTestUtil::readInto( inoutGame, written );
        }
    };

    /** @brief `UnoGame::writeState` 의 2 인 판을 꾸밉니다. */
    struct UnoFixture
    {
        sw::CardPile _hand0{};
        sw::CardPile _hand1{};
        sw::CardPile _drawPile{};
        sw::CardPile _discardPile{};
        int32        _currentPlayer{ 0 };
        int32        _direction{ 1 };
        int32        _pendingDraw{ 0 };
        int32        _unoTarget{ -1 };
        int32        _winner{ -1 };
        uint8        _color{ 0 };

        bool readInto( sw::UnoGame& inoutGame ) const
        {
            sw::Archive written;
            written << static_cast<uint32>( 2 );
            _hand0.writeState( written );
            _hand1.writeState( written );
            _drawPile.writeState( written );
            _discardPile.writeState( written );
            sw::StateIO::writeRandom( written, sw::GameRandom( 9u ) );
            written << _currentPlayer;
            written << _direction;
            written << _pendingDraw;
            written << _unoTarget;
            written << _winner;
            written << _color;
            return CardGameReadStateTestUtil::readInto( inoutGame, written );
        }
    };

    /** @brief `MatgoGame::writeState` 의 2 인 판을 꾸밉니다. */
    struct MatgoFixture
    {
        sw::CardPile _hand0{};
        sw::CardPile _captured0{};
        sw::CardPile _floor{};
        sw::CardPile _drawPile{};
        int32        _goCount0{ 0 };
        int32        _scoreAtLastGo0{ 0 };
        int32        _settlement0{ 0 };
        int32        _settlement1{ 0 };
        int32        _currentPlayer{ 0 };
        int32        _winner{ -1 };
        uint8        _phase{ static_cast<uint8>( sw::MatgoPhase::Play ) };

        bool readInto( sw::MatgoGame& inoutGame ) const
        {
            sw::Archive written;
            written << static_cast<uint32>( 2 );
            _hand0.writeState( written );
            _captured0.writeState( written );
            written << _goCount0;
            written << _scoreAtLastGo0;
            written << _settlement0;
            sw::CardPile{}.writeState( written );
            sw::CardPile{}.writeState( written );
            written << static_cast<int32>( 0 );
            written << static_cast<int32>( 0 );
            written << _settlement1;
            _floor.writeState( written );
            _drawPile.writeState( written );
            written << _currentPlayer;
            written << _winner;
            written << _phase;
            return CardGameReadStateTestUtil::readInto( inoutGame, written );
        }
    };

    /** @brief `PokerTable::writeState` 의 2 인 판(끝난 판이 기본)을 꾸밉니다. */
    struct PokerFixture
    {
        sw::PokerSeat            _seat0{};
        sw::PokerSeat            _seat1{};
        sw::vector<sw::PokerPot> _listPot{};
        sw::CardPile             _deck{};
        sw::CardPile             _board{};
        int32                    _button{ 0 };
        int32                    _currentSeat{ -1 };
        int32                    _currentBet{ 0 };
        int32                    _lastRaiseSize{ 2 };
        uint8                    _street{ static_cast<uint8>( sw::PokerStreet::HandOver ) };

        static void writeSeat( sw::Archive& outArchive, const sw::PokerSeat& seat )
        {
            for ( const sw::Card& card : seat._arrHole )
            {
                outArchive << card._id;
                outArchive << card._suit;
                outArchive << card._rank;
            }
            outArchive << seat._stack;
            outArchive << seat._committed;
            outArchive << seat._contributed;
            outArchive << seat._won;
            outArchive << seat._bInHand;
            outArchive << seat._bFolded;
            outArchive << seat._bAllIn;
            outArchive << seat._bActed;
        }

        bool readInto( sw::PokerTable& inoutTable ) const
        {
            sw::Archive written;
            written << static_cast<uint32>( 2 );
            writeSeat( written, _seat0 );
            writeSeat( written, _seat1 );
            written << static_cast<uint32>( _listPot.size() );
            for ( const sw::PokerPot& pot : _listPot )
            {
                written << static_cast<uint32>( pot._listEligibleSeat.size() );
                for ( const int32 seat : pot._listEligibleSeat )
                {
                    written << seat;
                }
                written << pot._amount;
            }
            _deck.writeState( written );
            _board.writeState( written );
            written << _button;
            written << _currentSeat;
            written << _currentBet;
            written << _lastRaiseSize;
            written << _street;
            return CardGameReadStateTestUtil::readInto( inoutTable, written );
        }
    };

    constexpr const utf8* kCardBattleXML = R"(<CardBattleCatalog>
  <Card id="strike" name="Strike" cost="1" effects="Damage:6"/>
</CardBattleCatalog>)";

    /** @brief `CardBattle::writeState` 의 카드 두 장짜리 판을 꾸밉니다. 번호 0, 1 은 처음에 뽑을 더미에 있습니다. */
    struct CardBattleFixture
    {
        sw::CardPile _drawPile{};
        sw::CardPile _hand{};
        int32        _intentAmount{ 3 };
        int32        _enemyHP{ 10 };
        int32        _enemyBlock{ 0 };
        int32        _intentIndex{ 0 };
        int32        _playerHP{ 50 };
        int32        _playerBlock{ 0 };
        int32        _energy{ 3 };
        int32        _turn{ 1 };
        uint8        _phase{ static_cast<uint8>( sw::CardBattlePhase::PlayerTurn ) };

        CardBattleFixture()
        {
            _drawPile.push( sw::Card{ 0, 0, 0 } );
            _drawPile.push( sw::Card{ 1, 0, 0 } );
        }

        bool readInto( sw::CardBattle& inoutBattle ) const
        {
            sw::Archive written;
            written << static_cast<uint32>( 2 );
            sw::StateIO::writeName( written, "strike" );
            sw::StateIO::writeName( written, "strike" );
            _drawPile.writeState( written );
            _hand.writeState( written );
            sw::CardPile{}.writeState( written );
            sw::CardPile{}.writeState( written );
            written << static_cast<uint32>( 1 );
            written << static_cast<uint8>( sw::CardBattleEffectKind::Damage );
            written << _intentAmount;
            written << _enemyHP;
            written << _enemyBlock;
            written << _intentIndex;
            sw::StateIO::writeRandom( written, sw::GameRandom( 9u ) );
            written << _playerHP;
            written << _playerBlock;
            written << _energy;
            written << _turn;
            written << _phase;
            return CardGameReadStateTestUtil::readInto( inoutBattle, written );
        }
    };
} // namespace

/**
 * @brief [CardGameReadStateTest] 결함: 더미가 빈 카드와 겹친 번호를 그대로 받았다(`takeByID` 는 앞의 것만 찾는다). 거절하고 더미를 둔다. 더미 사이의 겹침은 `hasDuplicateID` 가 찾는다
 */
SW_TEST_CASE( CardGameReadStateTest, PileRefusesEmptyAndRepeatedCards )
{
    const sw::Card card{ 7, 1, 7 };
    sw::CardPile   kept;
    kept.push( sw::Card{ 3, 0, 3 } );

    sw::Archive writtenEmpty;
    writtenEmpty << static_cast<uint32>( 1 );
    writtenEmpty << sw::Card::kNoCard;
    writtenEmpty << card._suit;
    writtenEmpty << card._rank;
    SW_EXPECT_FALSE( CardGameReadStateTestUtil::readInto( kept, writtenEmpty ) );

    sw::CardPile repeated;
    repeated.push( card );
    repeated.push( card );
    sw::Archive writtenRepeated;
    repeated.writeState( writtenRepeated );
    SW_EXPECT_FALSE( CardGameReadStateTestUtil::readInto( kept, writtenRepeated ) );
    SW_REQUIRE_EQUAL( 1, kept.getCount() );
    SW_EXPECT_EQUAL( uint16( 3 ), kept.getTop()._id );

    sw::CardPile other;
    other.push( card );
    sw::CardPile single;
    single.push( card );
    SW_EXPECT_TRUE( sw::CardPile::hasDuplicateID( { &single, &other } ) );
    SW_EXPECT_FALSE( sw::CardPile::hasDuplicateID( { &single, &kept } ) );
}

/**
 * @brief [CardGameReadStateTest] 트럼프 덱의 카드는 번호 규칙으로 무늬와 숫자가 정해진다. 번호만 다른 에이스 · 조커 · 범위 밖 번호는 트럼프가 아니다
 */
SW_TEST_CASE( CardGameReadStateTest, StandardCardMatchesTheDeckRule )
{
    sw::CardPile deck;
    sw::CardPile::makeStandardDeck( deck );
    for ( const sw::Card& card : deck.getCards() )
    {
        SW_EXPECT_TRUE( sw::CardPile::isStandardCard( card ) );
    }
    SW_EXPECT_FALSE( sw::CardPile::isStandardCard( sw::Card{ 5, sw::StandardSuit::kClubs, sw::StandardRank::kAce } ) );
    SW_EXPECT_FALSE( sw::CardPile::isStandardCard( sw::Card{ 52, sw::StandardSuit::kJoker, 0 } ) );
    SW_EXPECT_FALSE( sw::CardPile::isStandardCard( sw::Card{ 60, 0, 1 } ) );
}

/**
 * @brief [CardGameReadStateTest] 클론다이크: 실제 판은 왕복한다. 복제 카드, 트럼프에 없는 카드, 순서가 틀린 파운데이션(가짜 승리), 뒤집힌 맨 위, 음수 · 상한을 넘는 되돌린 수, 음수 옮긴 수를 거절하고 판을 둔다
 */
SW_TEST_CASE( CardGameReadStateTest, KlondikeRefusesImpossibleBoards )
{
    sw::KlondikeSettings settings;
    settings._maxRecycle = 2;
    sw::KlondikeGame game;
    SW_REQUIRE_TRUE( game.initialize( settings, 11u ) );
    SW_REQUIRE_TRUE( game.drawStock() );

    sw::Archive written;
    game.writeState( written );
    sw::KlondikeGame restored;
    SW_REQUIRE_TRUE( restored.initialize( settings, 12u ) );
    SW_REQUIRE_TRUE( CardGameReadStateTestUtil::readInto( restored, written ) );
    SW_EXPECT_EQUAL( 1, restored.getUndoCount() );

    KlondikeFixture valid;
    valid._state._arrTableau[0]       = CardGameReadStateTestUtil::makeStandardPile( { 1, 2 } );
    valid._state._arrFaceDownCount[0] = 1;
    valid._state._arrFoundation[0]    = CardGameReadStateTestUtil::makeStandardPile( { 0 } );
    valid._state._stock               = CardGameReadStateTestUtil::makeStandardPile( { 3 } );
    SW_REQUIRE_TRUE( valid.readInto( restored ) );
    SW_EXPECT_EQUAL( 2, restored.getState()._arrTableau[0].getCount() );

    KlondikeFixture clone = valid;
    clone._state._waste   = CardGameReadStateTestUtil::makeStandardPile( { 1 } );
    SW_EXPECT_FALSE( clone.readInto( restored ) );

    KlondikeFixture joker = valid;
    joker._state._waste.push( sw::Card{ 52, sw::StandardSuit::kJoker, 0 } );
    SW_EXPECT_FALSE( joker.readInto( restored ) );

    KlondikeFixture foundationOrder          = valid;
    foundationOrder._state._arrFoundation[0] = CardGameReadStateTestUtil::makeStandardPile( { 12 } ); // 킹이 맨 아래
    SW_EXPECT_FALSE( foundationOrder.readInto( restored ) );

    KlondikeFixture foundationSuit          = valid;
    foundationSuit._state._arrFoundation[1] = CardGameReadStateTestUtil::makeStandardPile( { 26 } ); // 하트 에이스가 다이아 칸에
    SW_EXPECT_FALSE( foundationSuit.readInto( restored ) );

    KlondikeFixture faceDownTop             = valid;
    faceDownTop._state._arrFaceDownCount[0] = 2;
    SW_EXPECT_FALSE( faceDownTop.readInto( restored ) );

    KlondikeFixture recycleNegative      = valid;
    recycleNegative._state._recycleCount = -1;
    SW_EXPECT_FALSE( recycleNegative.readInto( restored ) );

    KlondikeFixture recycleOver      = valid;
    recycleOver._state._recycleCount = 3;
    SW_EXPECT_FALSE( recycleOver.readInto( restored ) );

    KlondikeFixture moveNegative   = valid;
    moveNegative._state._moveCount = -1;
    SW_EXPECT_FALSE( moveNegative.readInto( restored ) );

    SW_EXPECT_EQUAL( 2, restored.getState()._arrTableau[0].getCount() );
    SW_EXPECT_EQUAL( 1, restored.getState()._stock.getCount() );
}

/**
 * @brief [CardGameReadStateTest] 우노: 실제 판은 왕복한다. 더미 사이의 복제 카드, 덱에 없는 카드(색이 범위 밖), 음수 · 터무니없는 벌칙, 손패가 남은 승자를 거절하고 판을 둔다
 */
SW_TEST_CASE( CardGameReadStateTest, UnoRefusesImpossibleTables )
{
    sw::UnoSettings settings;
    settings._playerCount = 2;
    sw::UnoGame game;
    SW_REQUIRE_TRUE( game.initialize( settings, 21u ) );
    sw::Archive written;
    game.writeState( written );
    sw::UnoGame restored;
    SW_REQUIRE_TRUE( restored.initialize( settings, 22u ) );
    SW_REQUIRE_TRUE( CardGameReadStateTestUtil::readInto( restored, written ) );
    SW_EXPECT_TRUE( restored.getHand( 0 ).getCards() == game.getHand( 0 ).getCards() );

    sw::CardPile deck;
    sw::UnoGame::makeDeck( deck );
    UnoFixture valid;
    valid._hand0.push( deck.getAt( 1 ) );
    valid._hand1.push( deck.getAt( 2 ) );
    valid._discardPile.push( deck.getAt( 3 ) );
    SW_REQUIRE_TRUE( valid.readInto( restored ) );
    SW_EXPECT_EQUAL( 1, restored.getHand( 0 ).getCount() );

    UnoFixture clone = valid;
    clone._hand1.push( deck.getAt( 1 ) );
    SW_EXPECT_FALSE( clone.readInto( restored ) );

    UnoFixture badColor = valid;
    badColor._hand1.push( sw::Card{ 4, 200, 1 } );
    SW_EXPECT_FALSE( badColor.readInto( restored ) );

    UnoFixture outOfDeck = valid;
    outOfDeck._hand1.push( sw::Card{ 108, 0, 1 } );
    SW_EXPECT_FALSE( outOfDeck.readInto( restored ) );

    UnoFixture pendingNegative   = valid;
    pendingNegative._pendingDraw = -1;
    SW_EXPECT_FALSE( pendingNegative.readInto( restored ) );

    UnoFixture pendingHuge   = valid;
    pendingHuge._pendingDraw = 0x7FFFFFFF;
    SW_EXPECT_FALSE( pendingHuge.readInto( restored ) );

    UnoFixture winnerWithCards = valid;
    winnerWithCards._winner    = 0;
    SW_EXPECT_FALSE( winnerWithCards.readInto( restored ) );

    SW_EXPECT_EQUAL( 1, restored.getHand( 0 ).getCount() );
    SW_EXPECT_EQUAL( -1, restored.getWinner() );
}

/**
 * @brief [CardGameReadStateTest] 맞고: 실제 판은 왕복한다. 복제 패, 덱에 없는 패, 음수 · 시프트를 넘치는 고 횟수, 음수 점수, 합이 0 이 아닌 정산, 끝나지 않은 판의 승자와 정산을 거절하고 판을 둔다
 */
SW_TEST_CASE( CardGameReadStateTest, MatgoRefusesImpossibleTables )
{
    const sw::MatgoSettings settings = sw::MatgoSettings::makeMatgo();
    sw::MatgoGame           game;
    SW_REQUIRE_TRUE( game.initialize( settings, 31u ) );
    sw::Archive written;
    game.writeState( written );
    sw::MatgoGame restored;
    SW_REQUIRE_TRUE( restored.initialize( settings, 32u ) );
    SW_REQUIRE_TRUE( CardGameReadStateTestUtil::readInto( restored, written ) );
    SW_EXPECT_TRUE( restored.getPlayer( 1 )._hand.getCards() == game.getPlayer( 1 )._hand.getCards() );

    MatgoFixture valid;
    valid._hand0     = CardGameReadStateTestUtil::makeHwatuPile( { 0 } );
    valid._captured0 = CardGameReadStateTestUtil::makeHwatuPile( { 1 } );
    valid._floor     = CardGameReadStateTestUtil::makeHwatuPile( { 2 } );
    valid._drawPile  = CardGameReadStateTestUtil::makeHwatuPile( { 3 } );
    SW_REQUIRE_TRUE( valid.readInto( restored ) );
    SW_EXPECT_EQUAL( 1, restored.getPlayer( 0 )._hand.getCount() );

    MatgoFixture clone = valid;
    clone._floor       = CardGameReadStateTestUtil::makeHwatuPile( { 0 } );
    SW_EXPECT_FALSE( clone.readInto( restored ) );

    MatgoFixture outOfDeck = valid;
    outOfDeck._floor.push( sw::Card{ 48, 0, 1 } );
    SW_EXPECT_FALSE( outOfDeck.readInto( restored ) );

    MatgoFixture wrongMonth = valid;
    wrongMonth._floor.push( sw::Card{ 4, static_cast<uint8>( sw::HwatuKind::Gwang ), 1 } ); // 번호 4 는 2 월이다
    SW_EXPECT_FALSE( wrongMonth.readInto( restored ) );

    MatgoFixture goNegative = valid;
    goNegative._goCount0    = -1;
    SW_EXPECT_FALSE( goNegative.readInto( restored ) );

    MatgoFixture goHuge = valid;
    goHuge._goCount0    = 40;
    SW_EXPECT_FALSE( goHuge.readInto( restored ) );

    MatgoFixture scoreNegative    = valid;
    scoreNegative._scoreAtLastGo0 = -1;
    SW_EXPECT_FALSE( scoreNegative.readInto( restored ) );

    MatgoFixture settleUneven = valid;
    settleUneven._settlement0 = 100;
    settleUneven._winner      = 0;
    settleUneven._phase       = static_cast<uint8>( sw::MatgoPhase::Finished );
    SW_EXPECT_FALSE( settleUneven.readInto( restored ) );

    MatgoFixture settleBeforeEnd = valid;
    settleBeforeEnd._settlement0 = 100;
    settleBeforeEnd._settlement1 = -100;
    SW_EXPECT_FALSE( settleBeforeEnd.readInto( restored ) );

    MatgoFixture winnerBeforeEnd = valid;
    winnerBeforeEnd._winner      = 1;
    SW_EXPECT_FALSE( winnerBeforeEnd.readInto( restored ) );

    SW_EXPECT_EQUAL( 1, restored.getPlayer( 0 )._hand.getCount() );
    SW_EXPECT_EQUAL( -1, restored.getWinner() );
    SW_EXPECT_EQUAL( 0, restored.getSettlement()[0] );
}

/**
 * @brief [CardGameReadStateTest] 화투 덱의 카드는 번호로 종류와 월이 정해진다. 범위 밖 번호와 월이 다른 카드는 덱의 카드가 아니다
 */
SW_TEST_CASE( CardGameReadStateTest, HwatuCardMatchesTheDeck )
{
    sw::CardPile deck;
    sw::HwatuDeck::makeDeck( deck );
    SW_REQUIRE_EQUAL( sw::HwatuDeck::kCardCount, deck.getCount() );
    for ( const sw::Card& card : deck.getCards() )
    {
        SW_EXPECT_TRUE( sw::HwatuDeck::isDeckCard( card ) );
    }
    SW_EXPECT_FALSE( sw::HwatuDeck::isDeckCard( sw::Card{ 48, 0, 1 } ) );
    SW_EXPECT_FALSE( sw::HwatuDeck::isDeckCard( sw::Card{ 4, deck.getAt( 4 )._suit, 1 } ) );
}

/**
 * @brief [CardGameReadStateTest] 포커: 진행 중인 실제 판은 왕복한다. 음수 칩, 넘치는 칩, 깨진 자리 표시, 다섯 장을 넘는 보드(쇼다운 배열을 넘어 쓴다), 겹친 · 트럼프에 없는 카드, 진행 중인데 없는 차례 · 홀 카드, 끝난 판의 차례, 겹친 팟 자리를 거절하고 판을 둔다
 */
SW_TEST_CASE( CardGameReadStateTest, PokerRefusesImpossibleTables )
{
    sw::PokerTable table;
    SW_REQUIRE_TRUE( table.initialize( sw::PokerSettings{}, { 100, 100 } ) );
    sw::GameRandom random( 41u );
    SW_REQUIRE_TRUE( table.startHand( random ) );
    sw::Archive written;
    table.writeState( written );
    sw::PokerTable restored;
    SW_REQUIRE_TRUE( restored.initialize( sw::PokerSettings{}, { 100, 100 } ) );
    SW_REQUIRE_TRUE( CardGameReadStateTestUtil::readInto( restored, written ) );
    SW_EXPECT_EQUAL( table.getCurrentSeat(), restored.getCurrentSeat() );
    SW_EXPECT_EQUAL( table.computePotTotal(), restored.computePotTotal() );

    PokerFixture valid;
    valid._seat0._stack = 70;
    valid._seat1._stack = 130;
    valid._deck         = CardGameReadStateTestUtil::makeStandardPile( { 10, 11 } );
    valid._board        = CardGameReadStateTestUtil::makeStandardPile( { 0, 1, 2, 3, 4 } );
    SW_REQUIRE_TRUE( valid.readInto( restored ) );
    SW_EXPECT_EQUAL( 70, restored.getSeat( 0 )._stack );

    PokerFixture stackNegative  = valid;
    stackNegative._seat0._stack = -1;
    SW_EXPECT_FALSE( stackNegative.readInto( restored ) );

    PokerFixture chipsOverflow  = valid;
    chipsOverflow._seat0._stack = 0x7FFFFFFF;
    SW_EXPECT_FALSE( chipsOverflow.readInto( restored ) );

    PokerFixture contributedLess      = valid;
    contributedLess._seat0._committed = 5;
    SW_EXPECT_FALSE( contributedLess.readInto( restored ) );

    PokerFixture foldedOutside    = valid;
    foldedOutside._seat0._bFolded = SW_TRUE;
    SW_EXPECT_FALSE( foldedOutside.readInto( restored ) );

    PokerFixture betNegative = valid;
    betNegative._currentBet  = -1;
    SW_EXPECT_FALSE( betNegative.readInto( restored ) );

    PokerFixture betOverChips = valid;
    betOverChips._currentBet  = 201;
    SW_EXPECT_FALSE( betOverChips.readInto( restored ) );

    PokerFixture raiseNegative   = valid;
    raiseNegative._lastRaiseSize = -1;
    SW_EXPECT_FALSE( raiseNegative.readInto( restored ) );

    PokerFixture boardOverflow = valid;
    boardOverflow._board       = CardGameReadStateTestUtil::makeStandardPile( { 0, 1, 2, 3, 4, 5 } );
    SW_EXPECT_FALSE( boardOverflow.readInto( restored ) );

    PokerFixture clone = valid;
    clone._deck        = CardGameReadStateTestUtil::makeStandardPile( { 0 } );
    SW_EXPECT_FALSE( clone.readInto( restored ) );

    PokerFixture holeClone       = valid;
    holeClone._seat0._arrHole[0] = holeClone._deck.getAt( 0 );
    SW_EXPECT_FALSE( holeClone.readInto( restored ) );

    PokerFixture joker = valid;
    joker._deck.push( sw::Card{ 52, sw::StandardSuit::kJoker, 0 } );
    SW_EXPECT_FALSE( joker.readInto( restored ) );

    PokerFixture turnAfterEnd = valid;
    turnAfterEnd._currentSeat = 0;
    SW_EXPECT_FALSE( turnAfterEnd.readInto( restored ) );

    PokerFixture runningNoTurn = valid;
    runningNoTurn._street      = static_cast<uint8>( sw::PokerStreet::River );
    SW_EXPECT_FALSE( runningNoTurn.readInto( restored ) );

    PokerFixture runningNoHole    = valid;
    runningNoHole._street         = static_cast<uint8>( sw::PokerStreet::River );
    runningNoHole._currentSeat    = 0;
    runningNoHole._seat0._bInHand = SW_TRUE;
    SW_EXPECT_FALSE( runningNoHole.readInto( restored ) );

    PokerFixture potAmountNegative = valid;
    potAmountNegative._listPot.resize( 1 );
    potAmountNegative._listPot[0]._amount = -1;
    SW_EXPECT_FALSE( potAmountNegative.readInto( restored ) );

    PokerFixture potSeatRepeated = valid;
    potSeatRepeated._listPot.resize( 1 );
    potSeatRepeated._listPot[0]._listEligibleSeat = { 0, 0 };
    SW_EXPECT_FALSE( potSeatRepeated.readInto( restored ) );

    PokerFixture tooManyPots = valid;
    tooManyPots._listPot.resize( 3 );
    SW_EXPECT_FALSE( tooManyPots.readInto( restored ) );

    SW_EXPECT_EQUAL( 70, restored.getSeat( 0 )._stack );
    SW_EXPECT_EQUAL( 5, restored.getBoard().getCount() );
}

/**
 * @brief [CardGameReadStateTest] 덱 빌딩: 실제 판은 왕복한다. 빠진 · 복제 카드, 덱 밖 번호, 손 한도를 넘는 손, 음수 · 최대를 넘는 값, 음수 의도(적이 나를 고친다), 값과 맞지 않는 승패를 거절하고 판을 둔다
 */
SW_TEST_CASE( CardGameReadStateTest, DeckBattleRefusesImpossibleBattles )
{
    sw::CardBattleCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kCardBattleXML, "CardGameReadStateTest" ) );
    sw::CardBattleSettings settings;
    settings._drawPerTurn = 1;
    settings._handLimit   = 1;
    sw::CardBattleEnemy enemy;
    enemy._hp = 10;
    enemy._listIntent.push_back( sw::CardBattleEffect{ 3, sw::CardBattleEffectKind::Damage } );
    sw::CardBattle battle;
    SW_REQUIRE_TRUE( battle.initialize( &catalog, settings, { "strike", "strike", "strike" }, enemy, 51u ) );
    SW_REQUIRE_TRUE( battle.playCard( 0 ) );

    sw::Archive written;
    battle.writeState( written );
    sw::CardBattle restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, settings, { "strike" }, enemy, 52u ) );
    SW_REQUIRE_TRUE( CardGameReadStateTestUtil::readInto( restored, written ) );
    SW_EXPECT_EQUAL( 4, restored.getEnemy()._hp );

    const CardBattleFixture valid;
    SW_REQUIRE_TRUE( valid.readInto( restored ) );
    SW_EXPECT_EQUAL( 10, restored.getEnemy()._hp );

    CardBattleFixture missing = valid;
    missing._drawPile         = sw::CardPile{};
    missing._drawPile.push( sw::Card{ 0, 0, 0 } );
    SW_EXPECT_FALSE( missing.readInto( restored ) );

    CardBattleFixture clone = valid;
    clone._drawPile         = sw::CardPile{};
    clone._drawPile.push( sw::Card{ 0, 0, 0 } );
    clone._hand.push( sw::Card{ 0, 0, 0 } );
    SW_EXPECT_FALSE( clone.readInto( restored ) );

    CardBattleFixture outOfDeck = valid;
    outOfDeck._drawPile         = sw::CardPile{};
    outOfDeck._drawPile.push( sw::Card{ 0, 0, 0 } );
    outOfDeck._drawPile.push( sw::Card{ 2, 0, 0 } );
    SW_EXPECT_FALSE( outOfDeck.readInto( restored ) );

    CardBattleFixture handOverLimit = valid;
    handOverLimit._drawPile         = sw::CardPile{};
    handOverLimit._hand.push( sw::Card{ 0, 0, 0 } );
    handOverLimit._hand.push( sw::Card{ 1, 0, 0 } );
    SW_EXPECT_FALSE( handOverLimit.readInto( restored ) );

    CardBattleFixture intentNegative = valid;
    intentNegative._intentAmount     = -3;
    SW_EXPECT_FALSE( intentNegative.readInto( restored ) );

    CardBattleFixture enemyNegative = valid;
    enemyNegative._enemyHP          = -1;
    SW_EXPECT_FALSE( enemyNegative.readInto( restored ) );

    CardBattleFixture blockNegative = valid;
    blockNegative._enemyBlock       = -1;
    SW_EXPECT_FALSE( blockNegative.readInto( restored ) );

    CardBattleFixture hpOverMax = valid;
    hpOverMax._playerHP         = settings._playerHP + 1;
    SW_EXPECT_FALSE( hpOverMax.readInto( restored ) );

    CardBattleFixture hpNegative = valid;
    hpNegative._playerHP         = -1;
    SW_EXPECT_FALSE( hpNegative.readInto( restored ) );

    CardBattleFixture energyNegative = valid;
    energyNegative._energy           = -1;
    SW_EXPECT_FALSE( energyNegative.readInto( restored ) );

    CardBattleFixture fakeWin = valid;
    fakeWin._phase            = static_cast<uint8>( sw::CardBattlePhase::Won );
    SW_EXPECT_FALSE( fakeWin.readInto( restored ) );

    CardBattleFixture fakeLoss = valid;
    fakeLoss._phase            = static_cast<uint8>( sw::CardBattlePhase::Lost );
    SW_EXPECT_FALSE( fakeLoss.readInto( restored ) );

    SW_EXPECT_EQUAL( 10, restored.getEnemy()._hp );
    SW_EXPECT_EQUAL( 2, restored.getDrawPile().getCount() );
}
