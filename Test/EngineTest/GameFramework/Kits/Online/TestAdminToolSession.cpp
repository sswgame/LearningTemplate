// GM 도구 패널의 세션과 메모리 제공자 — 조회, 지급 · 회수(메모 필수, 잔액 부족, 환불 회수의 빚), 제재 등급, 같은 키 재시도, 감사 열람, 연결 없는 실제 제공자.
// 에디터 확장(GF_Editor_Admin)의 화면 없는 부분만 본다. 확장 DLL 은 링크하지 않고 두 소스를 EngineTest 가 직접 컴파일한다.
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Shared/Editor/AdminToolSession.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct AdminToolReplyCapture
    {
        AdminClientReply _reply{};
        int32            _count{ 0 };

        void onReply( const AdminClientReply& reply )
        {
            _reply = reply;
            ++_count;
        }

        AdminClient::ReplyDelegate makeDelegate() { return AdminClient::ReplyDelegate::create<&AdminToolReplyCapture::onReply>( this ); }
    };

    int64 findAmount( const AdminReply& reply, string_view assetID )
    {
        for ( const LedgerBalance& balance : reply._listBalance )
        {
            if ( string_view( balance._assetID ) == assetID )
                return balance._amount;
        }
        return 0;
    }
} // namespace

SW_TEST_CASE( AdminToolSessionTest, LookupShowsTheSeedAccount )
{
    AdminToolSession session;
    session.requestLookup( FakeAdminProvider::kSeedPlayerID );
    SW_ASSERT_TRUE( session.hasLookup() );
    SW_EXPECT_EQUAL( session.getLookup()._identity._displayName, string( "Player One" ) );
    SW_EXPECT_EQUAL( findAmount( session.getLookup(), "cur.gold" ), int64( 100 ) );
    session.requestLookup( 0x999 );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::UnknownAccount );
    SW_EXPECT_TRUE( session.getLookup()._identity._accountID == FakeAdminProvider::kSeedPlayerID ); // 실패한 조회는 보이는 결과를 지우지 않는다
    SW_EXPECT_EQUAL( session.getPendingCount(), 0 );
}

SW_TEST_CASE( AdminToolSessionTest, GrantNeedsAMemoAndRefreshesTheBalance )
{
    AdminToolSession session;
    const AccountID  playerID = FakeAdminProvider::kSeedPlayerID;
    session.requestLookup( playerID );
    session.requestAdjust( playerID, "cur.gold", 25, false, "" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::InvalidRequest );
    SW_EXPECT_EQUAL( findAmount( session.getLookup(), "cur.gold" ), int64( 100 ) );
    session.requestAdjust( playerID, "cur.gold", 25, false, "ticket-1" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
    SW_EXPECT_EQUAL( findAmount( session.getLookup(), "cur.gold" ), int64( 125 ) ); // 성공하면 같은 계정을 다시 조회한다
    SW_EXPECT_EQUAL( session.getFakeProvider().getAuditLog().size(), size_t( 1 ) );
}

SW_TEST_CASE( AdminToolSessionTest, RevokeNeverGoesNegativeUnlessRefund )
{
    AdminToolSession session;
    const AccountID  playerID = FakeAdminProvider::kSeedPlayerID;
    session.requestLookup( playerID );
    session.requestAdjust( playerID, "cur.gold", -500, false, "ticket-2" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::InsufficientFunds );
    SW_EXPECT_EQUAL( findAmount( session.getLookup(), "cur.gold" ), int64( 100 ) );
    SW_EXPECT_TRUE( session.getFakeProvider().getAuditLog().empty() ); // 거절된 회수는 감사 줄도 없다
    session.requestAdjust( playerID, "cur.gold", -500, true, "ticket-2" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
    SW_EXPECT_EQUAL( findAmount( session.getLookup(), "cur.gold" ), int64( -400 ) ); // 환불 회수는 빚으로 남는다
    session.requestAdjust( playerID, "cur.gold", FakeAdminProvider::kMaxAdjustAmount + 1, false, "ticket-3" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::CapExceeded );
}

SW_TEST_CASE( AdminToolSessionTest, SanctionNeedsTheRightRole )
{
    AdminToolSession session;
    const AccountID  playerID = FakeAdminProvider::kSeedPlayerID;
    session.getFakeProvider().setActingRole( AdminRole::Support );
    session.requestSanction( playerID, ServiceSanctionKind::ChatMute, 600, "gm.notice", "ticket-4" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
    session.requestSanction( playerID, ServiceSanctionKind::Suspend, 600, "gm.notice", "ticket-4" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Forbidden );
    session.getFakeProvider().setActingRole( AdminRole::Super );
    session.requestSanction( playerID, ServiceSanctionKind::Ban, 1, "gm.notice", "ticket-4" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
    session.requestLookup( playerID );
    SW_EXPECT_EQUAL( session.getLookup()._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )], ServiceSanctionState::kPermanentMs );
    session.requestSanction( playerID, ServiceSanctionKind::Ban, 0, "gm.notice", "ticket-5" ); // 길이 0 은 해제
    session.requestLookup( playerID );
    SW_EXPECT_EQUAL( session.getLookup()._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )], int64( 0 ) );
}

SW_TEST_CASE( AdminToolSessionTest, AuditListsNewestFirstAndFiltersBySubject )
{
    AdminToolSession session;
    session.requestAdjust( FakeAdminProvider::kSeedPlayerID, "cur.gold", 5, false, "first" );
    session.requestAdjust( FakeAdminProvider::kSeedOtherID, "cur.gold", 7, false, "second" );
    session.requestAudit( "" );
    SW_ASSERT_EQUAL( session.getAuditRows().size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( session.getAuditRows()[0]._memo, string( "second" ) );
    session.requestAudit( "acct.0000000000000100/" );
    SW_ASSERT_EQUAL( session.getAuditRows().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( session.getAuditRows()[0]._memo, string( "first" ) );
    session.getFakeProvider().setActingRole( AdminRole::None );
    session.requestAudit( "" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Forbidden );
}

SW_TEST_CASE( AdminToolSessionTest, SameKeyIsReplayedWithoutASecondEffect )
{
    FakeAdminProvider     provider;
    AdminToolReplyCapture capture;
    AdminRequest          request;
    request._accountID          = FakeAdminProvider::kSeedPlayerID;
    request._assetID            = "cur.gold";
    request._amount             = 10;
    request._memo               = "ticket-6";
    const NetIdempotencyKey key = NetIdempotencyKey::makeRandom();
    provider.sendCommand( AdminMethod::kAdjustAsset, request, key, capture.makeDelegate() );
    SW_ASSERT_EQUAL( capture._count, 1 );
    SW_EXPECT_TRUE( capture._reply._reply._bReplayed == SW_FALSE );
    provider.sendCommand( AdminMethod::kAdjustAsset, request, key, capture.makeDelegate() );
    SW_ASSERT_EQUAL( capture._count, 2 );
    SW_EXPECT_TRUE( capture._reply._reply._bReplayed == SW_TRUE );
    SW_EXPECT_EQUAL( findAmount( capture._reply._reply, "cur.gold" ), int64( 110 ) );
    SW_EXPECT_EQUAL( provider.getAuditLog().size(), size_t( 1 ) );
}

SW_TEST_CASE( AdminToolSessionTest, LiveProviderWithoutAClientIsUnavailable )
{
    AdminToolSession session;
    session.useLiveClient( nullptr );
    SW_EXPECT_TRUE( session.getProvider().isLive() );
    session.requestLookup( FakeAdminProvider::kSeedPlayerID );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Unavailable );
    SW_EXPECT_FALSE( session.hasLookup() );
    session.useFake();
    SW_EXPECT_FALSE( session.getProvider().isLive() );
}
