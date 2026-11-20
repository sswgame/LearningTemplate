// FriendListTest — 친구 목록 모델: 관계 응답이 친구 줄 · 받은 신청 수를 만들고, 접속 상태 응답이 접속 중인 친구를 앞으로(같은 쪽은 계정 순) 모으며
// 접속 상태 알림이 다시 정렬한다. 오류 응답은 낡은 목록을 지우고 Failed 다. 클라이언트 없이 응답 길로 값을 넣는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/FriendList.h"

using namespace sw;

namespace
{
    struct FriendListTestUtil
    {
        static SocialLink makeLink( AccountID otherID, SocialLinkState state )
        {
            SocialLink link;
            link._otherID = otherID;
            link._state   = state;
            return link;
        }

        static SocialPresence makePresence( AccountID accountID, SocialPresenceStatus status )
        {
            SocialPresence presence;
            presence._accountID = accountID;
            presence._status    = status;
            return presence;
        }
    };
    using Util = FriendListTestUtil;
} // namespace

/** @brief [FriendListTest] 관계 응답은 친구 줄과 받은 신청 수를 만들고, 접속 상태 응답 뒤 접속 중인 친구가 앞(같은 쪽은 계정 순)이며 Loaded 다 */
SW_TEST_CASE( FriendListTest, LinksThenPresenceSortsOnlineFirst )
{
    FriendList       model;
    SocialLinksReply links;
    links._listLink = { Util::makeLink( 30, SocialLinkState::Friend ), Util::makeLink( 10, SocialLinkState::Friend ), Util::makeLink( 20, SocialLinkState::Incoming ),
                        Util::makeLink( 40, SocialLinkState::Friend ), Util::makeLink( 50, SocialLinkState::Blocked ) };
    model.applyLinks( links );
    SW_REQUIRE_EQUAL( size_t( 3 ), model.getEntries().size() );
    SW_EXPECT_EQUAL( 1u, model.getPendingRequestCount() );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Idle ); // 접속 상태가 와야 Loaded

    OnlineResult<SocialPresenceReply> presence;
    presence._reply._listPresence = { Util::makePresence( 40, SocialPresenceStatus::Online ), Util::makePresence( 10, SocialPresenceStatus::InGame ) };
    const uint64 revisionBefore   = model.getRevision();
    model.onPresenceReply( presence );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Loaded );
    SW_EXPECT_TRUE( model.getRevision() > revisionBefore );
    const vector<FriendEntry>& listEntry = model.getEntries();
    SW_EXPECT_EQUAL( AccountID( 10 ), listEntry[0]._accountID );
    SW_EXPECT_EQUAL( AccountID( 40 ), listEntry[1]._accountID );
    SW_EXPECT_EQUAL( AccountID( 30 ), listEntry[2]._accountID ); // 접속하지 않은 친구는 뒤

    SocialNotification offlineNotification;
    offlineNotification._kind     = SocialNotificationKind::PresenceChanged;
    offlineNotification._presence = Util::makePresence( 10, SocialPresenceStatus::Offline );
    model.applyNotification( offlineNotification );
    SW_EXPECT_EQUAL( AccountID( 40 ), model.getEntries()[0]._accountID );
    SW_EXPECT_EQUAL( AccountID( 10 ), model.getEntries()[1]._accountID );
}

/** @brief [FriendListTest] 오류 응답은 낡은 목록과 받은 신청 수를 지우고 Failed 다(낡은 것을 새것처럼 보이지 않는다) */
SW_TEST_CASE( FriendListTest, ErrorReplyClearsTheList )
{
    FriendList       model;
    SocialLinksReply links;
    links._listLink = { Util::makeLink( 10, SocialLinkState::Friend ), Util::makeLink( 20, SocialLinkState::Incoming ) };
    model.applyLinks( links );

    OnlineResult<SocialPresenceReply> failed;
    failed._errorCode = OnlineError::kUnavailable;
    model.onPresenceReply( failed );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, model.getErrorCode() );
    SW_EXPECT_TRUE( model.getEntries().empty() );
    SW_EXPECT_EQUAL( 0u, model.getPendingRequestCount() );
}

/**
 * @brief [FriendListTest] 클라이언트 없이 관계 응답이 오면(응답이 오는 사이 뗐다) 접속 상태를 묻지 못하고 Failed 다
 * @details 예전에는 빈 클라이언트 포인터로 접속 상태를 물어 죽었다. 클라이언트가 없으면 묻기(`refresh`)도 Failed 로 끝난다.
 */
SW_TEST_CASE( FriendListTest, ReplyWithoutAClientFails )
{
    FriendList                     model;
    OnlineResult<SocialLinksReply> links;
    links._reply._listLink = { Util::makeLink( 10, SocialLinkState::Friend ) };
    model.onLinksReply( links );
    SW_EXPECT_TRUE( model.getState() == FriendListState::Failed );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, model.getErrorCode() );

    FriendList idle;
    SW_EXPECT_FALSE( idle.refresh() );
    SW_EXPECT_TRUE( idle.getState() == FriendListState::Failed );
}

/** @brief [FriendListTest] 화면이 든 약한 참조는 모델이 사라지면 빈다 — 화면(엔진 UI 소유)이 게임 쪽 모델보다 오래 살아도 사라진 모델을 부르지 않는다 */
SW_TEST_CASE( FriendListTest, ScreenReferenceEmptiesWhenTheModelIsDestroyed )
{
    WeakRef<FriendList> reference;
    {
        FriendList model;
        reference = WeakRef<FriendList>{ model };
        SW_EXPECT_TRUE( reference.get() == &model );
    }
    SW_EXPECT_NULL( reference.get() );
}
