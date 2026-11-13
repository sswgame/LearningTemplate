#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/AdminCommandProvider.h"

#include <limits>

// FakeAdminProviderTest — 서버 없이 도는 메모리 제공자가 서버 규칙(`AdminStoreLogic`)의 일부를 같은 답으로 지키는지 본다: 등급, 메모, 잔액, 빚, 상한, 자산 이름, 멱등 키, 감사(에디터 케이스).

using namespace sw;
using namespace sw::editor;

namespace
{
    constexpr int64 kNowMs = 1000000;

    /** @brief 답을 담아 둡니다. */
    struct ReplyRecorder
    {
        AdminClient::Reply _last{};
        int32              _count{ 0 };

        void onReply( const AdminClient::Reply& reply )
        {
            _last = reply;
            ++_count;
        }
    };

    struct FakeAdminProviderTestUtil
    {
        static AdminRequest makeAdjust( int64 amount, string_view memo = "ticket" )
        {
            AdminRequest request;
            request._accountID = FakeAdminProvider::kSeedPlayerID;
            request._assetID   = "cur.gold";
            request._amount    = amount;
            request._memo      = string( memo );
            return request;
        }

        static AdminRequest makeSanction( ServiceSanctionKind kind, int64 untilMs )
        {
            AdminRequest request;
            request._accountID    = FakeAdminProvider::kSeedPlayerID;
            request._sanctionKind = kind;
            request._untilMs      = untilMs;
            request._reasonCode   = "gm.notice";
            request._memo         = "ticket";
            return request;
        }

        /** @brief 명령 하나를 보내고 결과를 돌려줍니다. 답은 정확히 한 번이어야 합니다. */
        static AdminServiceResult send( FakeAdminProvider& provider, uint16 method, const AdminRequest& request, const NetIdempotencyKey& key = NetIdempotencyKey{} )
        {
            ReplyRecorder recorder;
            provider.sendCommand( method, request, key, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
            SW_EXPECT_EQUAL( 1, recorder._count );
            return recorder._last._errorCode != OnlineError::kOk ? AdminServiceResult::fail( recorder._last._errorCode ) : AdminServiceResult{ recorder._last._reply._result };
        }

        static int64 findGold( const FakeAdminProvider& provider )
        {
            const FakeAdminAccount* pAccount = provider.findAccount( FakeAdminProvider::kSeedPlayerID );
            for ( const LedgerBalance& balance : pAccount->_listBalance )
            {
                if ( balance._assetID == "cur.gold" )
                    return balance._amount;
            }
            return 0;
        }
    };
} // namespace

SW_TEST_CASE( FakeAdminProviderTest, LookupFindsByIDOrNameAndReportsUnknown )
{
    FakeAdminProvider provider{ kNowMs };
    AdminRequest      request;
    request._accountID = FakeAdminProvider::kSeedPlayerID;
    ReplyRecorder recorder;
    provider.sendCommand( AdminMethod::LookupAccount::kID, request, NetIdempotencyKey{}, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_REQUIRE_TRUE( recorder._last._reply._result == AdminResult::Ok );
    SW_EXPECT_EQUAL( string( "Player One" ), recorder._last._reply._identity._displayName );
    SW_EXPECT_EQUAL( size_t( 1 ), recorder._last._reply._listBalance.size() );

    AdminRequest byName;
    byName._displayName = "Player Two";
    provider.sendCommand( AdminMethod::LookupAccount::kID, byName, NetIdempotencyKey{}, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_EXPECT_EQUAL( AccountID( FakeAdminProvider::kSeedOtherID ), recorder._last._reply._identity._accountID );

    AdminRequest unknown;
    unknown._accountID = 0x999;
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::LookupAccount::kID, unknown ) == AdminResult::UnknownAccount );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::LookupAccount::kID, AdminRequest{} ) == AdminResult::UnknownAccount );
}

SW_TEST_CASE( FakeAdminProviderTest, RolesGateCommands )
{
    FakeAdminProvider provider{ kNowMs };
    AdminRequest      lookup;
    lookup._accountID = FakeAdminProvider::kSeedPlayerID;

    provider.setActingRole( AdminRole::None );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::LookupAccount::kID, lookup ).hasError( OnlineError::kForbidden ) );

    provider.setActingRole( AdminRole::Viewer );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::LookupAccount::kID, lookup ) == AdminResult::Ok );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1 ) ).hasError( OnlineError::kForbidden ) );

    provider.setActingRole( AdminRole::Support );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::ChatMute, kNowMs + 1000 ) ) == AdminResult::Ok );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Suspend, kNowMs + 1000 ) ).hasError( OnlineError::kForbidden ) );

    provider.setActingRole( AdminRole::Operator );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Ban, kNowMs + 1000 ) ).hasError( OnlineError::kForbidden ) );

    provider.setActingRole( AdminRole::Super );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Ban, kNowMs + 1000 ) ) == AdminResult::Ok );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, 0xFFFF, lookup ).hasError( OnlineError::kForbidden ) ); // 모르는 메서드는 누구도 못 한다
}

SW_TEST_CASE( FakeAdminProviderTest, MutatingCommandsNeedAMemo )
{
    FakeAdminProvider provider{ kNowMs };
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1, "" ) ).hasError( OnlineError::kInvalidRequest ) );
    const string tooLong( static_cast<size_t>( AdminProtocol::kMaxMemoSize ) + 1, 'm' );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1, tooLong ) ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( int64( 100 ), FakeAdminProviderTestUtil::findGold( provider ) );
    SW_EXPECT_TRUE( provider.getAuditLog().empty() );
}

SW_TEST_CASE( FakeAdminProviderTest, RevokeStopsAtTheBalanceUnlessRefund )
{
    FakeAdminProvider provider{ kNowMs };
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( -101 ) ) == AdminResult::InsufficientFunds );
    SW_EXPECT_EQUAL( int64( 100 ), FakeAdminProviderTestUtil::findGold( provider ) );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( -100 ) ) == AdminResult::Ok ); // 잔액까지는 된다
    SW_EXPECT_EQUAL( int64( 0 ), FakeAdminProviderTestUtil::findGold( provider ) );

    AdminRequest refund = FakeAdminProviderTestUtil::makeAdjust( -30 );
    refund._bRefund     = SW_TRUE;
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, refund ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( int64( -30 ), FakeAdminProviderTestUtil::findGold( provider ) ); // 환불 회수는 빚으로 남는다
}

/**
 * @brief 상한과 극단의 양입니다.
 * @details 결함: `-amount` 로 크기를 구해 INT64_MIN 에서 정의되지 않은 동작이었다. 같은 모양이 서버 `AdminStoreLogic` 에도 있다(보고함).
 */
SW_TEST_CASE( FakeAdminProviderTest, AmountLimits )
{
    FakeAdminProvider provider{ kNowMs };
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 0 ) ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( FakeAdminProvider::kMaxAdjustAmount + 1 ) ) == AdminResult::CapExceeded );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( -FakeAdminProvider::kMaxAdjustAmount - 1 ) ) == AdminResult::CapExceeded );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( std::numeric_limits<int64>::min() ) ) == AdminResult::CapExceeded );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( std::numeric_limits<int64>::max() ) ) == AdminResult::CapExceeded );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( FakeAdminProvider::kMaxAdjustAmount ) ) == AdminResult::Ok );
}

/** @brief 결함: 길이만 봐 서버가 거절할 자산 이름(대문자 · 공백)이 메모리 제공자에서는 잔액 줄이 되었다 — 서버 없이 시험한 절차가 서버에서 진다. */
SW_TEST_CASE( FakeAdminProviderTest, AssetIDFollowsTheServerRule )
{
    FakeAdminProvider provider{ kNowMs };
    AdminRequest      request = FakeAdminProviderTestUtil::makeAdjust( 5 );
    request._assetID          = "Bad Asset";
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, request ).hasError( OnlineError::kInvalidRequest ) );
    request._assetID = "";
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, request ).hasError( OnlineError::kInvalidRequest ) );
    request._assetID = "cur.silver";
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, request ) == AdminResult::Ok ); // 없던 자산의 줄이 생긴다
    SW_EXPECT_EQUAL( size_t( 2 ), provider.findAccount( FakeAdminProvider::kSeedPlayerID )->_listBalance.size() );
}

/** @brief 같은 키로 다시 보내면 처음 답을 돌려주고 다시 적용하지 않는다. 키가 없으면 새로 만들고, 실패한 명령은 기록하지 않아 고쳐 다시 보낼 수 있다. */
SW_TEST_CASE( FakeAdminProviderTest, IdempotencyKeyReplaysTheFirstReply )
{
    FakeAdminProvider       provider{ kNowMs };
    const NetIdempotencyKey key = NetIdempotencyKey::makeRandom();
    ReplyRecorder           recorder;
    provider.sendCommand( AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 10 ), key, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_EXPECT_EQUAL( SW_FALSE, recorder._last._reply._bReplayed );
    provider.sendCommand( AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 10 ), key, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_EXPECT_EQUAL( SW_TRUE, recorder._last._reply._bReplayed );
    SW_EXPECT_TRUE( recorder._last._reply._result == AdminResult::Ok );
    SW_EXPECT_EQUAL( int64( 110 ), FakeAdminProviderTestUtil::findGold( provider ) );
    SW_EXPECT_EQUAL( 1, provider.getRecordedKeyCount() );

    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1 ) ) == AdminResult::Ok ); // 빈 키
    SW_EXPECT_EQUAL( 2, provider.getRecordedKeyCount() );

    const NetIdempotencyKey failedKey = NetIdempotencyKey::makeRandom();
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1, "" ), failedKey ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1 ), failedKey ) == AdminResult::Ok );
}

SW_TEST_CASE( FakeAdminProviderTest, SanctionRules )
{
    FakeAdminProvider provider{ kNowMs };
    provider.setActingRole( AdminRole::Super );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Ban, kNowMs + 5 ) ) == AdminResult::Ok );
    const FakeAdminAccount* pAccount = provider.findAccount( FakeAdminProvider::kSeedPlayerID );
    SW_EXPECT_EQUAL( ServiceSanctionState::kPermanentMs, pAccount->_sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )] ); // 영구 정지는 길이를 보지 않는다

    AdminRequest lift = FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Ban, 0 );
    lift._reasonCode  = "";
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, lift ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), pAccount->_sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )] );
    SW_EXPECT_EQUAL( string( "gm.notice" ), pAccount->_sanction._reasonCode ); // 결함: 사유 없는 해제가 앞의 사유를 지웠다(서버는 남긴다)

    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Suspend, -1 ) ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::Count, 1 ) ).hasError( OnlineError::kInvalidRequest ) );
    // 결함: `== Count` 만 보아 그 너머의 값이 제재 칸 밖을 읽고 썼다.
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( static_cast<ServiceSanctionKind>( 200 ), 1 ) ).hasError( OnlineError::kInvalidRequest ) );
}

SW_TEST_CASE( FakeAdminProviderTest, AuditRecordsChangesNewestFirst )
{
    FakeAdminProvider provider{ kNowMs };
    SW_REQUIRE_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 5 ) ) == AdminResult::Ok );
    provider.setNowMs( kNowMs + 10 );
    SW_REQUIRE_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::SetSanction::kID, FakeAdminProviderTestUtil::makeSanction( ServiceSanctionKind::ChatMute, kNowMs + 1000 ) ) == AdminResult::Ok );
    AdminRequest other = FakeAdminProviderTestUtil::makeAdjust( 5 );
    other._accountID   = FakeAdminProvider::kSeedOtherID;
    SW_REQUIRE_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::AdjustAsset::kID, other ) == AdminResult::Ok );

    const vector<ServiceAuditEntry>& listAudit = provider.getAuditLog();
    SW_REQUIRE_EQUAL( size_t( 3 ), listAudit.size() );
    SW_EXPECT_EQUAL( string( "admin.adjust" ), listAudit[0]._action );
    SW_EXPECT_EQUAL( string( "100" ), listAudit[0]._before );
    SW_EXPECT_EQUAL( string( "105" ), listAudit[0]._after );
    SW_EXPECT_EQUAL( string( "ticket" ), listAudit[0]._memo );
    SW_EXPECT_EQUAL( kNowMs, listAudit[0]._timeMs );
    SW_EXPECT_EQUAL( kNowMs + 10, listAudit[1]._timeMs );

    ReplyRecorder recorder;
    AdminRequest  listAll;
    listAll._maxCount = 2;
    provider.sendCommand( AdminMethod::ListAudit::kID, listAll, NetIdempotencyKey{}, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), recorder._last._reply._listAudit.size() );
    SW_EXPECT_EQUAL( string( "admin.adjust" ), recorder._last._reply._listAudit[0]._action ); // 최근 것
    SW_EXPECT_EQUAL( string( "admin.sanction" ), recorder._last._reply._listAudit[1]._action );

    AdminRequest byAccount;
    byAccount._subject = "acct.0000000000000101/";
    provider.sendCommand( AdminMethod::ListAudit::kID, byAccount, NetIdempotencyKey{}, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_EXPECT_EQUAL( size_t( 1 ), recorder._last._reply._listAudit.size() );
}

/** @brief 메모리 제공자가 처리하지 않는 명령은 있는 척하지 않는다. */
SW_TEST_CASE( FakeAdminProviderTest, CommandsThatNeedTheServerAreUnavailable )
{
    FakeAdminProvider provider{ kNowMs };
    AdminRequest      notice;
    notice._memo = "ticket";
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::PostAnnouncement::kID, notice ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( FakeAdminProviderTestUtil::send( provider, AdminMethod::ListLiveEvents::kID, AdminRequest{} ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( 0, provider.getRecordedKeyCount() );
}

/** @brief 빈 답 칸(`ReplyDelegate` 없음)도 죽지 않고, 묶지 않은 실제 제공자는 Unavailable 로 답한다. */
SW_TEST_CASE( FakeAdminProviderTest, LiveProviderWithoutAClientAnswersUnavailable )
{
    FakeAdminProvider provider{ kNowMs };
    provider.sendCommand( AdminMethod::AdjustAsset::kID, FakeAdminProviderTestUtil::makeAdjust( 1 ), NetIdempotencyKey{}, AdminClient::ReplyDelegate{} );
    SW_EXPECT_EQUAL( int64( 101 ), FakeAdminProviderTestUtil::findGold( provider ) );

    AdminClientProvider live;
    ReplyRecorder       recorder;
    live.sendCommand( AdminMethod::LookupAccount::kID, AdminRequest{}, NetIdempotencyKey{}, AdminClient::ReplyDelegate::create<&ReplyRecorder::onReply>( &recorder ) );
    SW_EXPECT_EQUAL( 1, recorder._count );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, recorder._last._errorCode );
    SW_EXPECT_TRUE( live.isLive() );
}
