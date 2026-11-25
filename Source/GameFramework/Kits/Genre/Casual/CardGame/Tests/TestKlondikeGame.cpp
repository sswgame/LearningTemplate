#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/KlondikeGame.h"

// KlondikeGameTest — 클론다이크의 옮기기 규칙: 웨이스트 · 열 · 파운데이션 사이의 옮기기가 규칙(색 번갈아 하나씩 내려가기, 빈 열에는 킹, 파운데이션은 에이스부터)을
// 지키고, 어긋난 옮기기는 판을 바꾸지 않으며, 자동 완료는 되돌리기 한 번으로 묶인다. 판은 `initializeFromState` 로 손으로 놓는다. 키트 GF_CardGame(nogpu).

namespace
{
    struct KlondikeGameTestUtil
    {
        /** @brief 트럼프 덱의 카드입니다(번호는 `CardPile::makeStandardDeck` 의 정의 — 무늬 * 13 + 숫자 - 1). */
        static sw::Card makeCard( uint8 suit, uint8 rank ) { return sw::Card{ static_cast<uint16>( suit * 13 + rank - 1 ), suit, rank }; }
    };

    constexpr uint8 kClubs    = sw::StandardSuit::kClubs;
    constexpr uint8 kDiamonds = sw::StandardSuit::kDiamonds;
    constexpr uint8 kHearts   = sw::StandardSuit::kHearts;
    constexpr uint8 kSpades   = sw::StandardSuit::kSpades;
} // namespace

/** @brief [KlondikeGameTest] 웨이스트 · 열 · 파운데이션 사이의 옮기기: 규칙에 맞는 것만 옮기고, 열의 맨 위가 뒤집힌 패가 되면 저절로 뒤집으며, 되돌리기가 한 수씩 되돌린다. */
SW_TEST_CASE( KlondikeGameTest, MovesFollowTheStackingRules )
{
    sw::KlondikeState state;
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kHearts, 6 ) );
    state._arrTableau[1].push( KlondikeGameTestUtil::makeCard( kClubs, 9 ) ); // 뒤집혀 있다
    state._arrTableau[1].push( KlondikeGameTestUtil::makeCard( kSpades, 5 ) );
    state._arrFaceDownCount[1] = 1;
    state._arrTableau[3].push( KlondikeGameTestUtil::makeCard( kClubs, 2 ) );
    state._arrTableau[4].push( KlondikeGameTestUtil::makeCard( kHearts, 3 ) );
    state._waste.push( KlondikeGameTestUtil::makeCard( kClubs, sw::StandardRank::kAce ) );
    state._waste.push( KlondikeGameTestUtil::makeCard( kHearts, sw::StandardRank::kKing ) ); // 맨 위

    sw::KlondikeGame game;
    SW_REQUIRE_TRUE( game.initializeFromState( sw::KlondikeSettings{}, state ) );

    // 웨이스트 → 열: 킹은 6 위에 못 놓고 빈 열에만 놓는다. 없는 열은 거절한다.
    SW_EXPECT_FALSE( game.moveWasteToTableau( 0 ) );
    SW_EXPECT_FALSE( game.moveWasteToTableau( -1 ) );
    SW_EXPECT_FALSE( game.moveWasteToTableau( sw::KlondikeState::kColumnCount ) );
    SW_EXPECT_EQUAL( 0, game.getUndoCount() ); // 거절은 기록을 남기지 않는다
    SW_REQUIRE_TRUE( game.moveWasteToTableau( 2 ) );
    SW_EXPECT_EQUAL( 1, game.getState()._arrTableau[2].getCount() );

    // 웨이스트 → 파운데이션: 에이스부터 올라간다. 빈 웨이스트는 거절한다.
    SW_REQUIRE_TRUE( game.moveWasteToFoundation() );
    SW_EXPECT_EQUAL( 1, game.getState()._arrFoundation[kClubs].getCount() );
    SW_EXPECT_FALSE( game.moveWasteToFoundation() );

    // 열 → 열: 검은 5 는 빨간 6 위로 간다. 옮긴 열의 맨 위(뒤집힌 9)가 저절로 뒤집힌다.
    SW_REQUIRE_TRUE( game.moveTableauToTableau( 1, 1, 0 ) );
    SW_EXPECT_EQUAL( 2, game.getState()._arrTableau[0].getCount() );
    SW_EXPECT_EQUAL( 0, game.getState()._arrFaceDownCount[1] );
    SW_EXPECT_FALSE( game.moveTableauToTableau( 0, 2, 2 ) ); // 6 은 킹 바로 아래가 아니다
    SW_EXPECT_FALSE( game.moveTableauToTableau( 0, 3, 5 ) ); // 가진 것보다 많이는 못 옮긴다
    SW_EXPECT_FALSE( game.moveTableauToTableau( 0, 1, 0 ) ); // 같은 열

    // 열 → 파운데이션: 다음 숫자만 올라가고, 올라간 열은 빈다.
    SW_EXPECT_FALSE( game.moveTableauToFoundation( 1 ) ); // 9 는 아직 못 올린다
    SW_REQUIRE_TRUE( game.moveTableauToFoundation( 3 ) );
    SW_EXPECT_EQUAL( 2, game.getState()._arrFoundation[kClubs].getCount() );
    SW_EXPECT_TRUE( game.getState()._arrTableau[3].isEmpty() );

    // 파운데이션 → 열: 검은 2 는 빨간 3 위로 내려온다. 빈 파운데이션과 맞지 않는 열은 거절한다.
    SW_EXPECT_FALSE( game.moveFoundationToTableau( kDiamonds, 4 ) );
    SW_EXPECT_FALSE( game.moveFoundationToTableau( kClubs, 0 ) ); // 5 위에 2 는 못 놓는다
    SW_REQUIRE_TRUE( game.moveFoundationToTableau( kClubs, 4 ) );
    SW_EXPECT_EQUAL( 1, game.getState()._arrFoundation[kClubs].getCount() );
    SW_EXPECT_EQUAL( 2, game.getState()._arrTableau[4].getCount() );

    // 성공한 다섯 수가 기록에 있고, 되돌리기는 마지막 한 수만 되돌린다.
    SW_EXPECT_EQUAL( 5, game.getUndoCount() );
    SW_EXPECT_EQUAL( 5, game.getState()._moveCount );
    SW_REQUIRE_TRUE( game.undo() );
    SW_EXPECT_EQUAL( 2, game.getState()._arrFoundation[kClubs].getCount() );
    SW_EXPECT_EQUAL( 1, game.getState()._arrTableau[4].getCount() );
    SW_EXPECT_EQUAL( 4, game.getState()._moveCount );
}

/** @brief [KlondikeGameTest] 여러 장 옮기기는 앞면인 묶음만 옮기고, 뒤집힌 패는 함께 옮기지 못한다. */
SW_TEST_CASE( KlondikeGameTest, RunMovesOnlyFaceUpCards )
{
    sw::KlondikeState state;
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kDiamonds, 9 ) ); // 뒤집혀 있다
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kSpades, 8 ) );
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kHearts, 7 ) );
    state._arrFaceDownCount[0] = 1;
    state._arrTableau[1].push( KlondikeGameTestUtil::makeCard( kHearts, 9 ) );

    sw::KlondikeGame game;
    SW_REQUIRE_TRUE( game.initializeFromState( sw::KlondikeSettings{}, state ) );
    SW_EXPECT_FALSE( game.moveTableauToTableau( 0, 3, 1 ) ); // 뒤집힌 9 까지는 못 옮긴다
    SW_REQUIRE_TRUE( game.moveTableauToTableau( 0, 2, 1 ) ); // 검은 8 · 빨간 7 묶음이 빨간 9 위로
    SW_EXPECT_EQUAL( 3, game.getState()._arrTableau[1].getCount() );
    SW_EXPECT_TRUE( game.getState()._arrTableau[1].getTop() == KlondikeGameTestUtil::makeCard( kHearts, 7 ) ); // 묶음의 순서가 그대로다
    SW_EXPECT_EQUAL( 1, game.getState()._arrTableau[0].getCount() );
    SW_EXPECT_EQUAL( 0, game.getState()._arrFaceDownCount[0] ); // 남은 9 가 뒤집혔다
}

/** @brief [KlondikeGameTest] 자동 완료는 스톡 · 웨이스트 · 뒤집힌 패가 없을 때만 돌고, 올린 것 전부를 되돌리기 한 번으로 묶는다. 올린 것이 없으면 기록도 없다. */
SW_TEST_CASE( KlondikeGameTest, AutoCompleteIsOneUndoStep )
{
    sw::KlondikeState state;
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kClubs, 3 ) );
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kClubs, 2 ) );
    state._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kClubs, sw::StandardRank::kAce ) );
    state._arrTableau[1].push( KlondikeGameTestUtil::makeCard( kDiamonds, 2 ) );
    state._arrTableau[1].push( KlondikeGameTestUtil::makeCard( kDiamonds, sw::StandardRank::kAce ) );

    sw::KlondikeGame game;
    SW_REQUIRE_TRUE( game.initializeFromState( sw::KlondikeSettings{}, state ) );
    SW_REQUIRE_TRUE( game.canAutoComplete() );
    SW_EXPECT_EQUAL( 5, game.autoComplete() );
    SW_EXPECT_EQUAL( 3, game.getState()._arrFoundation[kClubs].getCount() );
    SW_EXPECT_EQUAL( 2, game.getState()._arrFoundation[kDiamonds].getCount() );
    SW_EXPECT_TRUE( game.getState()._arrTableau[0].isEmpty() );
    SW_EXPECT_EQUAL( 1, game.getUndoCount() );
    SW_REQUIRE_TRUE( game.undo() );
    SW_EXPECT_EQUAL( 3, game.getState()._arrTableau[0].getCount() );
    SW_EXPECT_EQUAL( 0, game.getState()._arrFoundation[kClubs].getCount() );

    // 스톡이 남았으면 돌지 않는다
    sw::KlondikeState withStock = state;
    withStock._stock.push( KlondikeGameTestUtil::makeCard( kSpades, 4 ) );
    SW_REQUIRE_TRUE( game.initializeFromState( sw::KlondikeSettings{}, withStock ) );
    SW_EXPECT_FALSE( game.canAutoComplete() );
    SW_EXPECT_EQUAL( 0, game.autoComplete() );
    SW_EXPECT_EQUAL( 0, game.getUndoCount() );

    // 올릴 것이 없으면 기록을 남기지 않는다
    sw::KlondikeState stuck;
    stuck._arrTableau[0].push( KlondikeGameTestUtil::makeCard( kSpades, 5 ) );
    SW_REQUIRE_TRUE( game.initializeFromState( sw::KlondikeSettings{}, stuck ) );
    SW_EXPECT_EQUAL( 0, game.autoComplete() );
    SW_EXPECT_EQUAL( 0, game.getUndoCount() );
    SW_EXPECT_EQUAL( 0, game.getState()._moveCount );
}
