// 관계 규칙 — 신청 · 서로 신청 · 수락 · 거절 · 지우기 · 막기(상대 줄 지움, 서로 막음은 둠) · 나를 막은 사람에게 신청은 조용히 Ok, 상한(친구 · 받은 신청 · 보낸 신청 · 막음).
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/SocialLinkRules.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct SocialLinkRulesTestInternal
    {
        static SocialLinkDecision decide( SocialLinkOperation operation, SocialLinkState selfState, SocialLinkState otherState, const SocialCounts& selfCounts = SocialCounts{},
                                          const SocialCounts& otherCounts = SocialCounts{} )
        {
            return SocialLinkRules::decide( operation, selfState, otherState, selfCounts, otherCounts );
        }
    };
} // namespace

SW_TEST_CASE( SocialLinkRulesTest, RequestAcceptAndMutualRequest )
{
    using Internal                   = SocialLinkRulesTestInternal;
    using State                      = SocialLinkState;
    const SocialLinkDecision request = Internal::decide( SocialLinkOperation::Request, State::None, State::None );
    SW_EXPECT_TRUE( request._result == SocialResult::Ok );
    SW_EXPECT_TRUE( request._newSelfState == State::Outgoing && request._newOtherState == State::Incoming );
    SW_EXPECT_EQUAL( request._selfDelta._outgoingCount, 1 );
    SW_EXPECT_EQUAL( request._otherDelta._incomingCount, 1 );
    SW_EXPECT_TRUE( request._bNotifyOtherRequested == SW_TRUE );

    const SocialLinkDecision mutual = Internal::decide( SocialLinkOperation::Request, State::Incoming, State::Outgoing ); // 상대가 먼저 신청해 둠
    SW_EXPECT_TRUE( mutual._newSelfState == State::Friend && mutual._newOtherState == State::Friend );
    SW_EXPECT_EQUAL( mutual._selfDelta._incomingCount, -1 );
    SW_EXPECT_EQUAL( mutual._otherDelta._outgoingCount, -1 );
    SW_EXPECT_EQUAL( mutual._selfDelta._friendCount, 1 );
    SW_EXPECT_TRUE( mutual._bNotifyBothAdded == SW_TRUE );

    const SocialLinkDecision decline = Internal::decide( SocialLinkOperation::Decline, State::Incoming, State::Outgoing );
    SW_EXPECT_TRUE( decline._bWrite == SW_TRUE && decline._newSelfState == State::None && decline._newOtherState == State::None );
    SW_EXPECT_EQUAL( decline._otherDelta._outgoingCount, -1 );

    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Accept, State::None, State::None )._result == SocialResult::NotFound );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Decline, State::Outgoing, State::Incoming )._result == SocialResult::NotFound ); // 내가 보낸 것은 지우기로
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Request, State::Friend, State::Friend )._result == SocialResult::AlreadyFriend );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Request, State::Outgoing, State::Incoming )._result == SocialResult::AlreadyRequested );
}

SW_TEST_CASE( SocialLinkRulesTest, BlockingHidesAndCleansUp )
{
    using Internal                          = SocialLinkRulesTestInternal;
    using State                             = SocialLinkState;
    const SocialLinkDecision blockedRequest = Internal::decide( SocialLinkOperation::Request, State::None, State::Blocked ); // 상대가 나를 막았다
    SW_EXPECT_TRUE( blockedRequest._result == SocialResult::Ok );
    SW_EXPECT_TRUE( blockedRequest._bWrite == SW_FALSE );
    SW_EXPECT_TRUE( blockedRequest._bNotifyOtherRequested == SW_FALSE );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Request, State::Blocked, State::None )._result == SocialResult::YouBlocked );

    const SocialLinkDecision blockFriend = Internal::decide( SocialLinkOperation::Block, State::Friend, State::Friend );
    SW_EXPECT_TRUE( blockFriend._newSelfState == State::Blocked && blockFriend._newOtherState == State::None );
    SW_EXPECT_EQUAL( blockFriend._selfDelta._friendCount, -1 );
    SW_EXPECT_EQUAL( blockFriend._otherDelta._friendCount, -1 );
    SW_EXPECT_EQUAL( blockFriend._selfDelta._blockedCount, 1 );
    SW_EXPECT_TRUE( blockFriend._bNotifyOtherRemoved == SW_TRUE );

    const SocialLinkDecision blockRequester = Internal::decide( SocialLinkOperation::Block, State::Incoming, State::Outgoing );
    SW_EXPECT_EQUAL( blockRequester._selfDelta._incomingCount, -1 );
    SW_EXPECT_EQUAL( blockRequester._otherDelta._outgoingCount, -1 );
    SW_EXPECT_TRUE( blockRequester._bNotifyOtherRemoved == SW_FALSE );

    const SocialLinkDecision mutualBlock = Internal::decide( SocialLinkOperation::Block, State::None, State::Blocked );
    SW_EXPECT_TRUE( mutualBlock._newOtherState == State::Blocked ); // 상대의 차단은 둔다
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Block, State::Blocked, State::None )._bWrite == SW_FALSE );
    const SocialLinkDecision unblock = Internal::decide( SocialLinkOperation::Unblock, State::Blocked, State::Blocked );
    SW_EXPECT_TRUE( unblock._newSelfState == State::None && unblock._newOtherState == State::Blocked );
    SW_EXPECT_EQUAL( unblock._selfDelta._blockedCount, -1 );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Unblock, State::None, State::None )._result == SocialResult::NotFound );
}

SW_TEST_CASE( SocialLinkRulesTest, LimitsAreEnforced )
{
    using Internal = SocialLinkRulesTestInternal;
    using State    = SocialLinkState;
    SocialCounts fullFriend;
    fullFriend._friendCount = SocialLimit::kMaxFriend;
    SocialCounts fullIncoming;
    fullIncoming._incomingCount = SocialLimit::kMaxIncoming;
    SocialCounts fullOutgoing;
    fullOutgoing._outgoingCount = SocialLimit::kMaxOutgoing;
    SocialCounts fullBlocked;
    fullBlocked._blockedCount = SocialLimit::kMaxBlocked;
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Accept, State::Incoming, State::Outgoing, fullFriend, SocialCounts{} )._result == SocialResult::FriendLimit );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Accept, State::Incoming, State::Outgoing, SocialCounts{}, fullFriend )._result == SocialResult::FriendLimit );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Request, State::None, State::None, SocialCounts{}, fullIncoming )._result == SocialResult::PendingLimit );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Request, State::None, State::None, fullOutgoing, SocialCounts{} )._result == SocialResult::PendingLimit );
    SW_EXPECT_TRUE( Internal::decide( SocialLinkOperation::Block, State::None, State::None, fullBlocked, SocialCounts{} )._result == SocialResult::BlockLimit );
}
