#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminToolSession.h"

#include <limits>

// AdminToolSessionTest — GM 도구 패널의 상태: 명령 뒤 새로 고침이 어느 계정을 읽는지, 결과 칸과 상태 줄이 누구의 답인지, 기간 검사, 연결 없는 실제 제공자, 로그인 넘기기(에디터 케이스).

using namespace sw;
using namespace sw::editor;

namespace
{
    struct AdminToolSessionTestUtil
    {
        static constexpr AccountID kPlayer = FakeAdminProvider::kSeedPlayerID;
        static constexpr AccountID kOther  = FakeAdminProvider::kSeedOtherID;

        static int64 findLookupGold( const AdminToolSession& session )
        {
            for ( const LedgerBalance& balance : session.getLookup()._listBalance )
            {
                if ( balance._assetID == "cur.gold" )
                    return balance._amount;
            }
            return 0;
        }
    };

    /** @brief 처리기 자리에 거는 기록 장치입니다. */
    struct AdminCredentialsRecorder
    {
        int32 _callCount{ 0 };

        bool accept( string_view loginName, string_view password )
        {
            (void)loginName;
            (void)password;
            ++_callCount;
            return true;
        }
    };
} // namespace

SW_TEST_CASE( AdminToolSessionTest, StartsOnTheInMemoryProviderWithNoResults )
{
    AdminToolSession session;
    SW_EXPECT_FALSE( session.getProvider().isLive() );
    SW_EXPECT_FALSE( session.hasLookup() );
    SW_EXPECT_EQUAL( 0, session.getPendingCount() );
    SW_EXPECT_EQUAL( 0, session.getCompletedCount() );
    SW_EXPECT_TRUE( session.getConnection().canOpen() );
    SW_EXPECT_TRUE( session.getAuditRows().empty() );
}

SW_TEST_CASE( AdminToolSessionTest, FailedLookupKeepsTheShownAccount )
{
    AdminToolSession session;
    session.requestLookup( AdminToolSessionTestUtil::kPlayer );
    SW_REQUIRE_TRUE( session.hasLookup() );
    SW_EXPECT_EQUAL( AccountID( AdminToolSessionTestUtil::kPlayer ), session.getLookupAccountID() );
    SW_EXPECT_EQUAL( string( "LookupAccount: Ok" ), session.getStatusLine() );

    session.requestLookup( 0x999 );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::UnknownAccount );
    SW_EXPECT_EQUAL( AccountID( AdminToolSessionTestUtil::kPlayer ), session.getLookupAccountID() );
    SW_EXPECT_EQUAL( string( "LookupAccount: UnknownAccount" ), session.getStatusLine() );
}

/**
 * @brief 지급 뒤의 결과 칸과 상태 줄은 지급의 답이다.
 * @details 결함: 새로 고침 조회가 같은 길로 답해 상태 줄이 언제나 "LookupAccount: Ok" 였고 `getLastMethod` · `wasLastReplayed` 도 조회의 것이었다. GM 이 명령의 결과를 볼 수 없었다.
 */
SW_TEST_CASE( AdminToolSessionTest, RefreshDoesNotOverwriteTheCommandsResult )
{
    AdminToolSession session;
    session.requestLookup( AdminToolSessionTestUtil::kPlayer );
    session.requestAdjust( AdminToolSessionTestUtil::kPlayer, "cur.gold", 50, false, "ticket" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
    SW_EXPECT_EQUAL( AdminMethod::AdjustAsset::kID, session.getLastMethod() );
    SW_EXPECT_EQUAL( string( "AdjustAsset: Ok" ), session.getStatusLine() );
    SW_EXPECT_EQUAL( int64( 150 ), AdminToolSessionTestUtil::findLookupGold( session ) ); // 새로 고침은 돌았다
    SW_EXPECT_EQUAL( 3, session.getCompletedCount() );
    SW_EXPECT_EQUAL( 0, session.getPendingCount() );
}

/**
 * @brief 새로 고침은 명령의 대상을 읽는다.
 * @details 결함: 보이던 계정을 다시 읽어, 다른 계정에 지급하면 화면에 그 결과가 나오지 않았고 조회 전의 지급은 아무것도 읽지 않았다.
 */
SW_TEST_CASE( AdminToolSessionTest, RefreshReadsTheAccountActedOn )
{
    AdminToolSession session;
    session.requestAdjust( AdminToolSessionTestUtil::kPlayer, "cur.gold", 1, false, "ticket" ); // 조회 전
    SW_REQUIRE_TRUE( session.hasLookup() );
    SW_EXPECT_EQUAL( AccountID( AdminToolSessionTestUtil::kPlayer ), session.getLookupAccountID() );

    session.requestAdjust( AdminToolSessionTestUtil::kOther, "cur.gold", 7, false, "ticket" );
    SW_EXPECT_EQUAL( AccountID( AdminToolSessionTestUtil::kOther ), session.getLookupAccountID() );
    SW_EXPECT_EQUAL( int64( 7 ), AdminToolSessionTestUtil::findLookupGold( session ) );

    session.requestSanction( AdminToolSessionTestUtil::kPlayer, ServiceSanctionKind::ChatMute, 600, "gm.notice", "ticket" );
    SW_EXPECT_EQUAL( AccountID( AdminToolSessionTestUtil::kPlayer ), session.getLookupAccountID() );
    SW_EXPECT_TRUE( session.getLookup()._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] != 0 );
}

SW_TEST_CASE( AdminToolSessionTest, FailedCommandDoesNotRefresh )
{
    AdminToolSession session;
    session.requestLookup( AdminToolSessionTestUtil::kPlayer );
    const int32 completedBefore = session.getCompletedCount();
    session.requestAdjust( AdminToolSessionTestUtil::kOther, "cur.gold", 1, false, "" ); // 메모 없음
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( completedBefore + 1, session.getCompletedCount() );
    SW_EXPECT_EQUAL( AccountID( AdminToolSessionTestUtil::kPlayer ), session.getLookupAccountID() );
}

/**
 * @brief 음수 기간은 보내지 않는다.
 * @details 결함: 음수 기간이 0 과 같은 뜻이 되어 제재를 거는 명령이 해제로, 점검 · 공지는 "끝날 때까지" 로 나갔다. 큰 기간은 넘쳐 음수 시각이 되었다.
 */
SW_TEST_CASE( AdminToolSessionTest, BadDurationsAreRefusedBeforeSending )
{
    AdminToolSession session;
    session.requestSanction( AdminToolSessionTestUtil::kPlayer, ServiceSanctionKind::ChatMute, 600, "gm.notice", "ticket" );
    const int64 untilMs = session.getLookup()._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )];
    SW_REQUIRE_TRUE( untilMs != 0 );
    const int32 completedBefore = session.getCompletedCount();

    session.requestSanction( AdminToolSessionTestUtil::kPlayer, ServiceSanctionKind::ChatMute, -1, "gm.notice", "ticket" );
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( AdminMethod::SetSanction::kID, session.getLastMethod() );
    session.requestSanction( AdminToolSessionTestUtil::kPlayer, ServiceSanctionKind::ChatMute, std::numeric_limits<int64>::max(), "gm.notice", "ticket" );
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( completedBefore, session.getCompletedCount() ); // 제공자에 닿지 않았다
    SW_EXPECT_EQUAL( 0, session.getPendingCount() );
    SW_EXPECT_EQUAL( untilMs, session.getLookup()._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] );

    session.requestSetMaintenance( "all", "maintenance.default", -5, "ticket" );
    SW_EXPECT_EQUAL( AdminMethod::SetMaintenance::kID, session.getLastMethod() );
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kInvalidRequest ) );
    session.requestPostAnnouncement( 1, "text", 0, -5, "ticket" );
    SW_EXPECT_EQUAL( AdminMethod::PostAnnouncement::kID, session.getLastMethod() );
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( completedBefore, session.getCompletedCount() );
}

SW_TEST_CASE( AdminToolSessionTest, ZeroDurationLiftsASanction )
{
    AdminToolSession session;
    session.requestSanction( AdminToolSessionTestUtil::kPlayer, ServiceSanctionKind::ChatMute, 600, "gm.notice", "ticket" );
    session.requestSanction( AdminToolSessionTestUtil::kPlayer, ServiceSanctionKind::ChatMute, 0, "gm.notice", "ticket" );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), session.getLookup()._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] );
}

SW_TEST_CASE( AdminToolSessionTest, AuditRowsComeFromTheLastListing )
{
    AdminToolSession session;
    session.requestAdjust( AdminToolSessionTestUtil::kPlayer, "cur.gold", 1, false, "ticket" );
    session.requestAdjust( AdminToolSessionTestUtil::kOther, "cur.gold", 1, false, "ticket" );
    session.requestAudit( "" );
    SW_EXPECT_EQUAL( size_t( 2 ), session.getAuditRows().size() );
    SW_EXPECT_EQUAL( string( "ListAudit: Ok" ), session.getStatusLine() );
    session.requestAudit( "acct.0000000000000101/" );
    SW_EXPECT_EQUAL( size_t( 1 ), session.getAuditRows().size() );
}

/** @brief 메모리 제공자가 못 하는 명령은 결과만 바꾼다 — 새로 고침을 부르지 않고 앞의 조회를 지우지 않는다(공지 뒤의 쓸데없는 조회가 결함이었다). */
SW_TEST_CASE( AdminToolSessionTest, ServerOnlyCommandsReportUnavailable )
{
    AdminToolSession session;
    session.requestLookup( AdminToolSessionTestUtil::kPlayer );
    session.requestPostAnnouncement( 1, "text", 0, 60, "ticket" );
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( string( "ServerDirectory: Unavailable" ), session.getStatusLine() );
    session.requestReports( true );
    SW_EXPECT_TRUE( session.getReports().empty() );
    session.requestLiveEvents();
    SW_EXPECT_TRUE( session.getLiveEvents().empty() );
    SW_EXPECT_TRUE( session.hasLookup() );
}

/** @brief 클라이언트 없는 실제 제공자는 곧바로 Unavailable 이고 보낸 수가 남지 않는다. 메모리 제공자로 되돌릴 수 있다. */
SW_TEST_CASE( AdminToolSessionTest, LiveProviderWithoutClientAnswersUnavailable )
{
    AdminToolSession session;
    session.useLiveClient( nullptr );
    SW_EXPECT_TRUE( session.getProvider().isLive() );
    session.requestLookup( AdminToolSessionTestUtil::kPlayer );
    SW_EXPECT_TRUE( session.getLastResult().hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( 0, session.getPendingCount() );
    SW_EXPECT_TRUE( session.getStatusLine().find( "error" ) != string::npos ); // 오류 코드를 보인다

    session.useFake();
    session.requestLookup( AdminToolSessionTestUtil::kPlayer );
    SW_EXPECT_TRUE( session.getLastResult() == AdminResult::Ok );
}

/**
 * @brief 주소가 틀린 연결은 실패로 끝나고 메모리 제공자로 돌아오며, 다시 걸 수 있다.
 * @details 결함: 패널이 Failed 를 연결된 것으로 보아 Connect 단추가 사라졌다(`AdminToolConnection::canOpen`). 네트워크를 쓰지 않는 길이다(포트 없는 주소).
 */
SW_TEST_CASE( AdminToolSessionTest, FailedConnectionCanBeRetried )
{
    AdminToolSession session;
    const auto       failed1 = session.connectLive( "nohost", "editor" );
    SW_REQUIRE_FALSE( failed1.has_value() );
    const sw::string error1 = sw::string( failed1.error().message() ) + " " + sw::string( failed1.error().detail() );
    SW_EXPECT_TRUE( error1.empty() == false );
    SW_EXPECT_TRUE( session.getConnection().getState() == AdminToolConnectionState::Failed );
    SW_EXPECT_TRUE( session.getConnection().canOpen() );
    SW_EXPECT_FALSE( session.getProvider().isLive() );
    SW_EXPECT_TRUE( session.getStatusLine().find( "GM connection failed" ) != string::npos );
    SW_EXPECT_NULL( session.getConnection().getAdminClient() );

    session.disconnectLive();
    SW_EXPECT_TRUE( session.getConnection().getState() == AdminToolConnectionState::Disconnected );
    SW_EXPECT_TRUE( session.getConnection().canOpen() );
}

SW_TEST_CASE( AdminToolSessionTest, LoginNeedsAHandlerAndCredentials )
{
    AdminToolSession session;
    const auto       failed1 = session.connectWithLogin( "nohost", "editor", "gm", "pw" );
    SW_REQUIRE_FALSE( failed1.has_value() );
    const sw::string error1 = sw::string( failed1.error().message() ) + " " + sw::string( failed1.error().detail() );
    SW_EXPECT_TRUE( error1.find( "login handler" ) != string::npos );
    SW_EXPECT_FALSE( session.hasCredentialsHandler() );

    AdminCredentialsRecorder recorder;
    const Subscription       subscription = session.bindCredentialsHandler( AdminToolSession::CredentialsHandler::create<&AdminCredentialsRecorder::accept>( &recorder ) );
    SW_EXPECT_TRUE( session.hasCredentialsHandler() );
    SW_EXPECT_FALSE( session.connectWithLogin( "nohost", "editor", "", "pw" ) );
    SW_EXPECT_FALSE( session.connectWithLogin( "nohost", "editor", "gm", "" ) );
    SW_EXPECT_EQUAL( 0, recorder._callCount );
    SW_EXPECT_FALSE( session.connectWithLogin( "nohost", "editor", "gm", "pw" ) ); // 처리기는 받고 연결 단계에서 진다
    SW_EXPECT_EQUAL( 1, recorder._callCount );
}
