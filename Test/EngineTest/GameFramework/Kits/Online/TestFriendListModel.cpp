// 친구 목록 모델 — 클라이언트가 없을 때 Failed 로 끝나는 길, 관계 목록과 접속 상태 응답으로 친구 줄이 접속 중인 순으로 서는 길, 접속 상태 알림이 줄을 바꾸는 길, 실패 응답이 낡은 목록을 비우는 길.
// 화면(FriendListScreen)은 UI 시스템이 있어야 열려 EngineTest 의 UI 시나리오 쪽에서 본다.
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Shared/FriendListModel.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    SocialLink makeLink( AccountID otherID, SocialLinkState state )
    {
        SocialLink link;
        link._otherID = otherID;
        link._state   = state;
        return link;
    }

    SocialPresence makePresence( AccountID accountID, SocialPresenceStatus status, string_view activity )
    {
        SocialPresence presence;
        presence._accountID = accountID;
        presence._status    = status;
        presence._activity  = string( activity );
        return presence;
    }
} // namespace

/**
 * @brief [FriendListModelTest] 클라이언트 없이 물으면 Failed 로 끝나고 목록은 비어 있으며 판이 올라간다
 */
SW_TEST_CASE( FriendListModelTest, WithoutAClientTheListFailsInsteadOfStayingStale )
{
    FriendListModel model;
    SW_EXPECT_TRUE( model.getState() == FriendListState::Idle );
    SW_EXPECT_TRUE( model.getEntries().empty() );

    const uint64 before = model.getRevision();
    SW_EXPECT_FALSE( model.refresh() );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Failed );
    SW_EXPECT_TRUE( model.getErrorCode() == OnlineError::kUnavailable );
    SW_EXPECT_TRUE( model.getRevision() > before );
}

/**
 * @brief [FriendListModelTest] 관계 목록에서 친구만 줄이 되고 받은 신청은 개수로 세며, 접속 상태 응답이 오면 접속 중인 친구가 앞에 서고 Loaded 가 된다
 */
SW_TEST_CASE( FriendListModelTest, LinksAndPresenceBuildTheOrderedFriendRows )
{
    FriendListModel model;

    SocialReply links;
    links._listLink = { makeLink( 30, SocialLinkState::Friend ), makeLink( 10, SocialLinkState::Friend ), makeLink( 20, SocialLinkState::Friend ), makeLink( 40, SocialLinkState::Incoming ),
                        makeLink( 50, SocialLinkState::Outgoing ), makeLink( 60, SocialLinkState::Blocked ) };
    model.applyLinks( links );
    SW_ASSERT_EQUAL( size_t( 3 ), model.getEntries().size() );
    SW_EXPECT_EQUAL( uint32( 1 ), model.getPendingRequestCount() );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Idle ); // 접속 상태가 와야 Loaded

    SocialClient::Reply presenceReply;
    presenceReply._reply._listPresence = { makePresence( 30, SocialPresenceStatus::InGame, "dungeon 3" ), makePresence( 20, SocialPresenceStatus::Online, "" ) };
    model.onPresenceReply( presenceReply );
    SW_ASSERT_TRUE( model.getState() == FriendListState::Loaded );
    SW_ASSERT_EQUAL( size_t( 3 ), model.getEntries().size() );
    // 접속 중(20, 30)이 앞이고 같은 쪽끼리는 번호 순이다.
    SW_EXPECT_EQUAL( AccountID( 20 ), model.getEntries()[0]._accountID );
    SW_EXPECT_EQUAL( AccountID( 30 ), model.getEntries()[1]._accountID );
    SW_EXPECT_EQUAL( AccountID( 10 ), model.getEntries()[2]._accountID );
    SW_EXPECT_TRUE( model.getEntries()[1]._status == SocialPresenceStatus::InGame );
    SW_EXPECT_TRUE( model.getEntries()[1]._activity == "dungeon 3" );
    SW_EXPECT_TRUE( model.getEntries()[2]._status == SocialPresenceStatus::Offline );
}

/**
 * @brief [FriendListModelTest] 접속 상태 알림은 그 친구의 줄을 바꾸고 순서를 다시 세우며, 목록에 없는 계정의 알림은 판을 올리지 않는다
 */
SW_TEST_CASE( FriendListModelTest, PresenceNotificationUpdatesTheRowAndReorders )
{
    FriendListModel model;
    SocialReply     links;
    links._listLink = { makeLink( 10, SocialLinkState::Friend ), makeLink( 20, SocialLinkState::Friend ) };
    model.applyLinks( links );
    model.onPresenceReply( SocialClient::Reply{} );
    SW_ASSERT_TRUE( model.getState() == FriendListState::Loaded );
    SW_EXPECT_EQUAL( AccountID( 10 ), model.getEntries()[0]._accountID );

    SocialNotification notification;
    notification._kind     = SocialNotificationKind::PresenceChanged;
    notification._presence = makePresence( 20, SocialPresenceStatus::Away, "afk" );
    const uint64 before    = model.getRevision();
    model.applyNotification( notification );
    SW_EXPECT_TRUE( model.getRevision() > before );
    SW_EXPECT_EQUAL( AccountID( 20 ), model.getEntries()[0]._accountID ); // 접속 중이 된 20 이 앞으로
    SW_EXPECT_TRUE( model.getEntries()[0]._activity == "afk" );

    notification._presence = makePresence( 999, SocialPresenceStatus::Online, "" );
    const uint64 unchanged = model.getRevision();
    model.applyNotification( notification );
    SW_EXPECT_EQUAL( unchanged, model.getRevision() );
}

/**
 * @brief [FriendListModelTest] 실패 응답(오류 코드나 결과)은 이전 목록과 신청 수를 비우고 Failed 로 끝난다
 */
SW_TEST_CASE( FriendListModelTest, AFailedReplyClearsTheStaleList )
{
    FriendListModel model;
    SocialReply     links;
    links._listLink = { makeLink( 10, SocialLinkState::Friend ), makeLink( 11, SocialLinkState::Incoming ) };
    model.applyLinks( links );
    model.onPresenceReply( SocialClient::Reply{} );
    SW_ASSERT_EQUAL( size_t( 1 ), model.getEntries().size() );

    SocialClient::Reply failed;
    failed._errorCode     = OnlineError::kUnavailable;
    failed._reply._result = SocialResult::Unavailable;
    model.onPresenceReply( failed );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Failed );
    SW_EXPECT_TRUE( model.getEntries().empty() );
    SW_EXPECT_EQUAL( uint32( 0 ), model.getPendingRequestCount() );
    SW_EXPECT_TRUE( model.getResult() == SocialResult::Unavailable );
}
