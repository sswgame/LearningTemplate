// SocialLinkRulesTest — 관계 바꾸기 규칙: 신청 · 서로 신청(바로 친구) · 수락 · 거절 · 지우기 · 막기(서로 막음은 둔다) · 풀기, 나를 막은 사람에게 신청하면 아무것도 쓰지 않는 Ok,
// 상한(친구 · 신청 · 막음)과 개수 변화가 양쪽에 맞는지. 저장소를 모르는 순수 함수라 표로 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/SocialLinkRules.h"

using namespace sw;

namespace
{
    using Op    = SocialLinkOperation;
    using State = SocialLinkState;

    SocialLinkDecision decide( Op operation, State selfState, State otherState, const SocialCounts& selfCounts = {}, const SocialCounts& otherCounts = {} )
    {
        return SocialLinkRules::decide( operation, selfState, otherState, selfCounts, otherCounts );
    }

    /** @brief 짝인 개수 변화(내 보낸 신청 ↔ 상대 받은 신청, 친구 ↔ 친구)가 같은지 봅니다 — 한쪽만 세면 개수 레코드가 어긋난다. */
    void expectPairedDelta( const SocialLinkDecision& decision )
    {
        SW_EXPECT_EQUAL( decision._selfDelta._friendCount, decision._otherDelta._friendCount );
        SW_EXPECT_EQUAL( decision._selfDelta._outgoingCount, decision._otherDelta._incomingCount );
        SW_EXPECT_EQUAL( decision._selfDelta._incomingCount, decision._otherDelta._outgoingCount );
    }
} // namespace

/** @brief [SocialLinkRulesTest] 처음 신청하면 두 줄(보냄 · 받음)을 쓰고 양쪽 개수를 하나씩 늘리며 상대에게 알린다 */
SW_TEST_CASE( SocialLinkRulesTest, RequestWritesBothRowsAndNotifies )
{
    const SocialLinkDecision decision = decide( Op::Request, State::None, State::None );
    SW_EXPECT_TRUE( decision._result == SocialResult::Ok );
    SW_EXPECT_TRUE( decision._bWrite == SW_TRUE );
    SW_EXPECT_TRUE( decision._newSelfState == State::Outgoing );
    SW_EXPECT_TRUE( decision._newOtherState == State::Incoming );
    SW_EXPECT_EQUAL( decision._selfDelta._outgoingCount, 1 );
    SW_EXPECT_TRUE( decision._bNotifyOtherRequested == SW_TRUE );
    expectPairedDelta( decision );
}

/** @brief [SocialLinkRulesTest] 받은 신청이 있는 쪽이 신청하면(서로 신청) 바로 친구가 되고, 수락과 같은 결정이다 */
SW_TEST_CASE( SocialLinkRulesTest, MutualRequestBecomesFriendsLikeAccept )
{
    const SocialLinkDecision listDecision[] = { decide( Op::Request, State::Incoming, State::Outgoing ), decide( Op::Accept, State::Incoming, State::Outgoing ) };
    for ( const SocialLinkDecision& decision : listDecision )
    {
        SW_EXPECT_TRUE( decision._result == SocialResult::Ok );
        SW_EXPECT_TRUE( decision._newSelfState == State::Friend && decision._newOtherState == State::Friend );
        SW_EXPECT_EQUAL( decision._selfDelta._friendCount, 1 );
        SW_EXPECT_EQUAL( decision._selfDelta._incomingCount, -1 );
        SW_EXPECT_TRUE( decision._bNotifyBothAdded == SW_TRUE );
        expectPairedDelta( decision );
    }
    SW_EXPECT_TRUE( decide( Op::Accept, State::None, State::None )._result == SocialResult::NotFound ); // 받은 신청이 없다
}

/** @brief [SocialLinkRulesTest] 나를 막은 사람에게 신청하면 아무것도 쓰지 않고 Ok 다(막힌 사실을 알리지 않는다). 내가 막은 사람이면 YouBlocked 다 */
SW_TEST_CASE( SocialLinkRulesTest, RequestToABlockerIsASilentOk )
{
    const SocialLinkDecision blockedByOther = decide( Op::Request, State::None, State::Blocked );
    SW_EXPECT_TRUE( blockedByOther._result == SocialResult::Ok );
    SW_EXPECT_TRUE( blockedByOther._bWrite == SW_FALSE );
    SW_EXPECT_TRUE( blockedByOther._bNotifyOtherRequested == SW_FALSE );
    SW_EXPECT_TRUE( blockedByOther._selfDelta.isZero() && blockedByOther._otherDelta.isZero() );
    SW_EXPECT_TRUE( decide( Op::Request, State::Blocked, State::None )._result == SocialResult::YouBlocked );
    SW_EXPECT_TRUE( decide( Op::Request, State::Friend, State::Friend )._result == SocialResult::AlreadyFriend );
    SW_EXPECT_TRUE( decide( Op::Request, State::Outgoing, State::Incoming )._result == SocialResult::AlreadyRequested );
}

/** @brief [SocialLinkRulesTest] 상한에 닿으면 쓰지 않는다 — 내 보낸 신청 · 상대 받은 신청 · 양쪽 친구 수 · 내 막음 수 */
SW_TEST_CASE( SocialLinkRulesTest, LimitsStopTheWrite )
{
    SocialCounts fullOutgoing;
    fullOutgoing._outgoingCount = SocialLimit::kMaxOutgoing;
    SocialCounts fullIncoming;
    fullIncoming._incomingCount = SocialLimit::kMaxIncoming;
    SocialCounts fullFriends;
    fullFriends._friendCount = SocialLimit::kMaxFriend;
    SocialCounts fullBlocked;
    fullBlocked._blockedCount = SocialLimit::kMaxBlocked;

    const SocialLinkDecision listDecision[] = {
        decide( Op::Request, State::None, State::None, fullOutgoing, {} ),
        decide( Op::Request, State::None, State::None, {}, fullIncoming ),
        decide( Op::Accept, State::Incoming, State::Outgoing, fullFriends, {} ),
        decide( Op::Accept, State::Incoming, State::Outgoing, {}, fullFriends ),
        decide( Op::Block, State::None, State::None, fullBlocked, {} ),
    };
    const SocialOutcome listExpected[] = { SocialResult::PendingLimit, SocialResult::PendingLimit, SocialResult::FriendLimit, SocialResult::FriendLimit, SocialResult::BlockLimit };
    for ( size_t index = 0; index < std::size( listDecision ); ++index )
    {
        SW_EXPECT_TRUE( listDecision[index]._result == listExpected[index] );
        SW_EXPECT_TRUE( listDecision[index]._bWrite == SW_FALSE );
    }
}

/** @brief [SocialLinkRulesTest] 지우기는 친구 · 보냄 · 받음을 모두 지우고(친구였으면 알림) 거절은 받은 신청만 지운다 */
SW_TEST_CASE( SocialLinkRulesTest, RemoveAndDeclineDeleteTheRightRows )
{
    const SocialLinkDecision removeFriend = decide( Op::Remove, State::Friend, State::Friend );
    SW_EXPECT_TRUE( removeFriend._result == SocialResult::Ok );
    SW_EXPECT_TRUE( removeFriend._newSelfState == State::None && removeFriend._newOtherState == State::None );
    SW_EXPECT_EQUAL( removeFriend._selfDelta._friendCount, -1 );
    SW_EXPECT_TRUE( removeFriend._bNotifyOtherRemoved == SW_TRUE );
    expectPairedDelta( removeFriend );

    const SocialLinkDecision cancelRequest = decide( Op::Remove, State::Outgoing, State::Incoming );
    SW_EXPECT_EQUAL( cancelRequest._selfDelta._outgoingCount, -1 );
    SW_EXPECT_TRUE( cancelRequest._bNotifyOtherRemoved == SW_FALSE );
    expectPairedDelta( cancelRequest );

    const SocialLinkDecision decline = decide( Op::Decline, State::Incoming, State::Outgoing );
    SW_EXPECT_EQUAL( decline._selfDelta._incomingCount, -1 );
    expectPairedDelta( decline );
    SW_EXPECT_TRUE( decide( Op::Decline, State::Outgoing, State::Incoming )._result == SocialResult::NotFound ); // 보낸 신청은 거절하는 것이 아니다
    SW_EXPECT_TRUE( decide( Op::Remove, State::None, State::None )._result == SocialResult::NotFound );
}

/** @brief [SocialLinkRulesTest] 막으면 있던 관계를 지우고 막음 하나를 세며, 상대가 이미 나를 막았으면 그 줄은 둔다(서로 막음). 이미 막았으면 쓰지 않는 Ok 다 */
SW_TEST_CASE( SocialLinkRulesTest, BlockClearsTheLinkAndKeepsAMutualBlock )
{
    const SocialLinkDecision blockFriend = decide( Op::Block, State::Friend, State::Friend );
    SW_EXPECT_TRUE( blockFriend._newSelfState == State::Blocked && blockFriend._newOtherState == State::None );
    SW_EXPECT_EQUAL( blockFriend._selfDelta._blockedCount, 1 );
    SW_EXPECT_EQUAL( blockFriend._selfDelta._friendCount, -1 );
    SW_EXPECT_EQUAL( blockFriend._otherDelta._friendCount, -1 );
    SW_EXPECT_TRUE( blockFriend._bNotifyOtherRemoved == SW_TRUE );

    const SocialLinkDecision mutualBlock = decide( Op::Block, State::None, State::Blocked );
    SW_EXPECT_TRUE( mutualBlock._newOtherState == State::Blocked );
    SW_EXPECT_EQUAL( mutualBlock._otherDelta._blockedCount, 0 ); // 상대의 막음 수는 상대 줄이 세고 있다

    const SocialLinkDecision again = decide( Op::Block, State::Blocked, State::None );
    SW_EXPECT_TRUE( again._result == SocialResult::Ok && again._bWrite == SW_FALSE );
}

/** @brief [SocialLinkRulesTest] 풀기는 내 막음 줄만 지우고 상대 줄은 그대로 둔다. 막지 않았으면 NotFound 다 */
SW_TEST_CASE( SocialLinkRulesTest, UnblockLeavesTheOtherRowAlone )
{
    const SocialLinkDecision unblock = decide( Op::Unblock, State::Blocked, State::Blocked );
    SW_EXPECT_TRUE( unblock._result == SocialResult::Ok );
    SW_EXPECT_TRUE( unblock._newSelfState == State::None && unblock._newOtherState == State::Blocked );
    SW_EXPECT_EQUAL( unblock._selfDelta._blockedCount, -1 );
    SW_EXPECT_TRUE( decide( Op::Unblock, State::Friend, State::Friend )._result == SocialResult::NotFound );
}
