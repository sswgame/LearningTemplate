// HubCardTableTest — 카드 탁자의 차례 규칙: 남의 차례 · 손에 없는 패 · 깨진 바이트는 거절하고, 받은 행동 뒤의 차례는 판이 정하며, 같은 방의 행동을 다시 돌리면 같은 판이다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"

using namespace sw;

namespace
{
    struct HubCardTableTestUtil
    {
        static vector<uint8> encode( const CardAction& action )
        {
            vector<uint8> buffer;
            CardAction::encodeAction( action, buffer );
            return buffer;
        }

        static vector<uint8> makeDraw()
        {
            CardAction action;
            action._kind = static_cast<uint8>( SheddingCardActionKind::Draw );
            return encode( action );
        }

        /** @brief 정책이 허락한 행동을 중계 서버처럼 목록에 넣고 적용합니다. */
        static void commit( const HubCardTurnPolicy& policy, TurnRoom& room, int32 seat, const vector<uint8>& buffer )
        {
            SW_REQUIRE_TRUE( policy.isActionAllowed( room, seat, buffer ) );
            room._listAction.push_back( TurnAction{ buffer, seat } );
            policy.applyAction( room, seat, buffer );
        }
    };
    using Util = HubCardTableTestUtil;
} // namespace

/** @brief [HubCardTableTest] 차례가 아닌 자리 · 손에 없는 패 · 깨진 바이트는 거절한다 */
SW_TEST_CASE( HubCardTableTest, ForgedActionsAreRefused )
{
    const HubCardTurnPolicy policy;
    TurnRoom                room;
    room._roomID = 7;
    room._listSeat.resize( HubCardTurnPolicy::kSeatCount );
    SheddingCardGame game;
    SW_REQUIRE_TRUE( HubCardTurnPolicy::replay( room._roomID, room._listAction, game ) );
    const int32 first = game.getCurrentPlayer();
    const int32 other = 1 - first;

    SW_EXPECT_FALSE( policy.isActionAllowed( room, other, Util::makeDraw() ) ); // 남의 차례
    SW_EXPECT_FALSE( policy.isActionAllowed( room, first, vector<uint8>{ 1, 2, 3 } ) );
    SW_EXPECT_FALSE( policy.isActionAllowed( room, first, vector<uint8>{} ) );
    CardAction stolen;
    stolen._kind   = static_cast<uint8>( SheddingCardActionKind::Play );
    stolen._cardID = game.getHand( other ).getAt( 0 )._id; // 남의 손패
    SW_EXPECT_FALSE( policy.isActionAllowed( room, first, Util::encode( stolen ) ) );
}

/** @brief [HubCardTableTest] 받은 행동 뒤의 차례는 판이 정하고, 행동을 다시 돌린 두 판은 같다 */
SW_TEST_CASE( HubCardTableTest, ReplayFollowsTheAppliedActions )
{
    const HubCardTurnPolicy policy;
    TurnRoom                room;
    room._roomID = 7;
    room._listSeat.resize( HubCardTurnPolicy::kSeatCount );
    SheddingCardGame game;
    SW_REQUIRE_TRUE( HubCardTurnPolicy::replay( room._roomID, room._listAction, game ) );
    int32 seat = game.getCurrentPlayer();
    for ( int32 turn = 0; turn < 6; ++turn )
    {
        Util::commit( policy, room, seat, Util::makeDraw() );
        seat = room._currentSeat;
    }
    SheddingCardGame left;
    SheddingCardGame right;
    SW_REQUIRE_TRUE( HubCardTurnPolicy::replay( room._roomID, room._listAction, left ) );
    SW_REQUIRE_TRUE( HubCardTurnPolicy::replay( room._roomID, room._listAction, right ) );
    SW_EXPECT_EQUAL( left.getCurrentPlayer(), room._currentSeat );
    SW_EXPECT_EQUAL( left.getHand( 0 ).getCount(), right.getHand( 0 ).getCount() );
    SW_EXPECT_TRUE( left.getTopCard() == right.getTopCard() );
    SW_EXPECT_EQUAL( 20, left.getHand( 0 ).getCount() + left.getHand( 1 ).getCount() ); // 일곱 장씩 + 뽑은 여섯 장
}
