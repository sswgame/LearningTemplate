// GuildServiceTest — 길드 로직(메모리 저장소): 만들기 · 이름 유일, 초대 · 수락 · 만료 · 정원, 떠나기 · 해산, 내보내기 · 역할 · 길드장 넘기기, 공지, 창고(맡기기 · 꺼내기 · 멱등 · 해산 막기,
// 이전 불가 자산의 맡긴 사람별 몫 · 꺼내기 허용 · 떠날 때 돌려주기),
// 공개 길드 신청, 경험치 · 레벨, 알림과 이벤트, 저장소 실패. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceReadSetConnection.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordReader.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

using namespace sw;

namespace
{
    constexpr AccountID kMaster  = 1;
    constexpr AccountID kOfficer = 2;
    constexpr AccountID kMember  = 3;
    constexpr AccountID kOutside = 4;
    constexpr int64     kNowMs   = 1700000000000ll;

    /** @brief "cur.gem" 만 이전 불가(유료 재화)인 정책입니다. */
    class PaidGemPolicy final : public ILedgerPolicy
    {
    public:
        int64 getBalanceCap( string_view /*assetID*/ ) const override { return 0; }
        bool  isTransferable( string_view assetID ) const override { return assetID != "cur.gem"; }
    };

    /** @brief "gem." 으로 시작하는 자산만 이전 불가인 정책입니다(맡긴 사람별 몫을 여러 자산으로 채운다). */
    class GemPrefixPolicy final : public ILedgerPolicy
    {
    public:
        int64 getBalanceCap( string_view /*assetID*/ ) const override { return 0; }
        bool  isTransferable( string_view assetID ) const override { return assetID.substr( 0, 4 ) != "gem."; }
    };

    /** @brief 이벤트를 모읍니다. */
    struct GuildEventSink
    {
        vector<GuildEvent> _listEvent{};

        void onEvent( const GuildEvent& event ) { _listEvent.push_back( event ); }
    };

    struct GuildFixture
    {
        MemoryServiceDatabase _database;
        MemoryServiceStore    _store;
        GuildService          _service;
        GuildEventSink        _sink;
        Subscription          _eventSubscription;
        uint64                _nextTag{ 1 };

        GuildFixture()
            : _database{}
            , _store{ &_database }
            , _service{}
            , _sink{}
            , _eventSubscription{}
        {
            _service.initialize( &_store );
            _eventSubscription = _service.bindEventDelegate( GuildEventDelegate::create<&GuildEventSink::onEvent>( &_sink ) );
        }

        GuildCompletion run( GuildOperation operation, AccountID accountID, AccountID targetID = kInvalidAccountID, string_view text = "", uint64 guildID = 0, int64 nowMs = kNowMs )
        {
            GuildRequest request;
            request._operation = operation;
            request._accountID = accountID;
            request._targetID  = targetID;
            request._text      = string( text );
            request._guildID   = guildID;
            request._nowMs     = UnixMilliseconds{ nowMs };
            return runRequest( request );
        }

        GuildCompletion runRequest( const GuildRequest& request )
        {
            const uint64 tag = _nextTag++;
            _service.submit( request, tag );
            while ( _store.pollCompletions() > 0 )
            {
            }
            vector<GuildCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            if ( listCompletion.empty() )
                return GuildCompletion{};
            SW_EXPECT_EQUAL( tag, listCompletion[0]._requestTag );
            return std::move( listCompletion[0] );
        }

        /** @brief 창고 한 쪽을 읽습니다. @p cursor 는 앞 쪽의 `_storageNextCursor` 이고 처음은 빈 커서입니다. */
        GuildCompletion listStorage( AccountID accountID, const GuildStorageCursor& cursor = GuildStorageCursor{}, int32 maxCount = GuildLimit::kMaxStoragePage )
        {
            GuildRequest request;
            request._operation       = GuildOperation::ListStorage;
            request._accountID       = accountID;
            request._storageCursor   = cursor;
            request._storageMaxCount = maxCount;
            request._nowMs           = UnixMilliseconds{ kNowMs };
            return runRequest( request );
        }

        SocialServiceResult setRole( AccountID accountID, AccountID targetID, GuildRole role )
        {
            GuildRequest request;
            request._operation = GuildOperation::SetRole;
            request._accountID = accountID;
            request._targetID  = targetID;
            request._role      = role;
            request._nowMs     = UnixMilliseconds{ kNowMs };
            return runRequest( request )._result;
        }

        GuildCompletion transfer( GuildOperation operation, AccountID accountID, int64 amount, string_view journalKey, string_view assetID = "cur.gold",
                                  AccountID depositorID = kInvalidAccountID )
        {
            GuildRequest request;
            request._operation   = operation;
            request._accountID   = accountID;
            request._assetID     = string( assetID );
            request._amount      = amount;
            request._journalKey  = string( journalKey );
            request._depositorID = depositorID;
            request._nowMs       = UnixMilliseconds{ kNowMs };
            return runRequest( request );
        }

        /** @brief 꺼내기 허용을 주거나(GrantWithdrawal) 거둡니다(RevokeWithdrawal). @p keyLow 는 멱등 키(하위 64 비트)이며 0 이면 키가 없습니다. */
        SocialServiceResult setGrant( GuildOperation operation, AccountID accountID, AccountID targetID, int64 amount, uint64 keyLow, string_view assetID = "cur.gem" )
        {
            GuildRequest request;
            request._operation      = operation;
            request._accountID      = accountID;
            request._targetID       = targetID;
            request._assetID        = string( assetID );
            request._amount         = amount;
            request._idempotencyKey = NetIdempotencyKey{ 0, keyLow };
            request._nowMs          = UnixMilliseconds{ kNowMs };
            return runRequest( request )._result;
        }

        /** @brief 분개 키를 붙인 떠나기(@p targetID 없음)나 내보내기입니다. 맡긴 몫을 돌려줄 때 키가 필요합니다. */
        SocialServiceResult runKeyed( GuildOperation operation, AccountID accountID, AccountID targetID, string_view journalKey )
        {
            GuildRequest request;
            request._operation  = operation;
            request._accountID  = accountID;
            request._targetID   = targetID;
            request._journalKey = string( journalKey );
            request._nowMs      = UnixMilliseconds{ kNowMs };
            return runRequest( request )._result;
        }

        SocialServiceResult setOpen( AccountID accountID, bool bOpen )
        {
            GuildRequest request;
            request._operation = GuildOperation::SetJoinPolicy;
            request._accountID = accountID;
            request._bOpen     = bOpen ? SW_TRUE : SW_FALSE;
            request._nowMs     = UnixMilliseconds{ kNowMs };
            return runRequest( request )._result;
        }

        /** @brief 길드장 · 임원 · 멤버 셋의 길드를 만들고 알림 · 이벤트를 비웁니다. 길드 id 입니다. */
        uint64 makeGuild()
        {
            const uint64 guildID = run( GuildOperation::Create, kMaster, kInvalidAccountID, "Guild" )._info._guildID;
            SW_EXPECT_TRUE( guildID != 0 );
            for ( const AccountID accountID : { kOfficer, kMember } )
            {
                SW_EXPECT_TRUE( run( GuildOperation::Invite, kMaster, accountID )._result == SocialResult::Ok );
                SW_EXPECT_TRUE( run( GuildOperation::Accept, accountID, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
            }
            SW_EXPECT_TRUE( setRole( kMaster, kOfficer, GuildRole::Officer ) == SocialResult::Ok );
            clearSignals();
            return guildID;
        }

        void clearSignals()
        {
            vector<SocialNotification> listNotification;
            _service.pollNotifications( listNotification );
            _sink._listEvent.clear();
        }

        void grant( AccountID accountID, int64 amount, string_view assetID = "cur.gold" )
        {
            LedgerTransferRequest request;
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountID ), string( assetID ), amount } );
            request._journalKey = "test/grant/" + string( assetID ) + "/" + to_string( accountID ) + "/" + to_string( amount );
            request._reason     = "test.grant";
            LedgerTransferResult result;
            SW_EXPECT_TRUE( Ledger::executeTransfer( _database, request, result ) == LedgerResult::Ok );
        }

        int64 getBalanceOf( AccountID accountID, string_view assetID = "cur.gold" )
        {
            LedgerBalance balance;
            SW_EXPECT_TRUE( Ledger::readBalance( _database, LedgerHolder::makeAccount( accountID ), assetID, balance ) == ServiceStoreResult::Ok );
            return balance._amount;
        }
    };
} // namespace

/** @brief [GuildServiceTest] 만들기: id 는 차례대로, 만든 사람이 길드장이고 Joined 이벤트가 난다. 이름은 대소문자를 무시하고 유일하며 한 계정은 한 길드에만 든다 */
SW_TEST_CASE( GuildServiceTest, CreateRegistersUniqueNames )
{
    GuildFixture          rig;
    const GuildCompletion created = rig.run( GuildOperation::Create, kMaster, kInvalidAccountID, "Dragons" );
    SW_EXPECT_TRUE( created._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( uint64( 1 ), created._info._guildID );
    SW_EXPECT_EQUAL( string( "Dragons" ), created._info._name );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sink._listEvent.size() );
    SW_EXPECT_TRUE( rig._sink._listEvent[0]._kind == GuildEvent::Kind::Joined );

    const GuildCompletion got = rig.run( GuildOperation::Get, kMaster );
    SW_EXPECT_EQUAL( kMaster, got._info._masterID );
    SW_EXPECT_EQUAL( 1, got._info._memberCount );
    SW_REQUIRE_EQUAL( size_t( 1 ), got._info._listMember.size() );
    SW_EXPECT_TRUE( got._info._listMember[0]._role == GuildRole::Master );
    SW_EXPECT_EQUAL( kNowMs, got._info._listMember[0]._joinedMs );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Create, kMaster, kInvalidAccountID, "Other" )._result == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Create, kMember, kInvalidAccountID, "dRAGONS" )._result == SocialResult::GuildNameTaken );
    SW_EXPECT_EQUAL( uint64( 2 ), rig.run( GuildOperation::Create, kMember, kInvalidAccountID, "용의 둥지" )._info._guildID );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kOutside )._result == SocialResult::NotInGuild );
}

/** @brief [GuildServiceTest] 이름 규칙: 1 ~ 24 바이트, 앞뒤 공백 · 제어 문자 · 깨진 UTF-8 은 안 된다 */
SW_TEST_CASE( GuildServiceTest, NameRules )
{
    SW_EXPECT_TRUE( GuildService::isValidName( "Dragons 2" ) );
    SW_EXPECT_TRUE( GuildService::isValidName( string( static_cast<size_t>( GuildLimit::kMaxNameSize ), 'x' ) ) );
    SW_EXPECT_FALSE( GuildService::isValidName( string( static_cast<size_t>( GuildLimit::kMaxNameSize ) + 1, 'x' ) ) );
    SW_EXPECT_FALSE( GuildService::isValidName( "" ) );
    SW_EXPECT_FALSE( GuildService::isValidName( " lead" ) );
    SW_EXPECT_FALSE( GuildService::isValidName( "trail " ) );
    SW_EXPECT_FALSE( GuildService::isValidName( "tab\tname" ) );
    SW_EXPECT_FALSE( GuildService::isValidName( "bad\xFFutf8" ) );
}

/** @brief [GuildServiceTest] TTL 규칙: 만료된 초대는 기간 뒤에 지우고, 다시 보내 늦춰진 초대는 낡은 색인 줄로 지우지 않는다(감사 O-9) */
SW_TEST_CASE( GuildServiceTest, ExpiredInvitesAreSwept )
{
    GuildFixture rig;
    (void)rig.makeGuild();
    const hashed_string inviteTable{ "social_guild_invite" };
    const hashed_string expiryTable{ "social_guild_invite_expiry" };
    // makeGuild 의 초대 둘은 수락돼 초대는 지워졌지만 색인 줄은 남는다(TTL 색인은 줄을 따로 지우지 않고 쓸기가 원본 없는 줄을 지운다)
    SW_REQUIRE_EQUAL( 0, rig._database.countRecords( inviteTable ) );
    SW_REQUIRE_EQUAL( 2, rig._database.countRecords( expiryTable ) );
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOutside )._result == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOutside, "", 0, kNowMs + 1000 )._result == SocialResult::Ok ); // 다시 보내 만료가 늦춰졌다
    SW_REQUIRE_EQUAL( 1, rig._database.countRecords( inviteTable ) );
    SW_REQUIRE_EQUAL( 4, rig._database.countRecords( expiryTable ) );

    ServiceTTLRule        rule = GuildService::makeInviteTTLRule( 1000 );
    ServiceTTLSweepReport report;
    const int64           firstExpiryMs = kNowMs + GuildLimit::kInviteLifetimeMs;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 1500, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, report._deletedCount );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( inviteTable ) );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( expiryTable ) ); // 낡은 줄(수락된 초대 둘 · 다시 보내기 전 줄)만 지웠다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 3000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( inviteTable ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( expiryTable ) );
}

/** @brief [GuildServiceTest] TTL 규칙: 만료된 가입 신청은 기간 뒤에 지우고, 다시 낸 신청은 낡은 색인 줄로 지우지 않는다(감사 O-9 — 해산한 길드의 신청은 runApply 가 다시 돌지 않는다) */
SW_TEST_CASE( GuildServiceTest, ExpiredApplicationsAreSwept )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    SW_REQUIRE_TRUE( rig.setOpen( kMaster, true ) == SocialResult::Ok );
    constexpr AccountID kLate = 900;
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Apply, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Apply, kLate, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Apply, kLate, kInvalidAccountID, "", guildID, kNowMs + 1000 )._result == SocialResult::Ok ); // 다시 내 만료가 늦춰졌다
    const hashed_string applicationTable{ "social_guild_application" };
    const hashed_string expiryTable{ "social_guild_application_expiry" };
    const hashed_string reverseTable{ "social_account_application" };
    SW_REQUIRE_EQUAL( 2, rig._database.countRecords( applicationTable ) );
    SW_REQUIRE_EQUAL( 2, rig._database.countRecords( reverseTable ) );
    SW_REQUIRE_EQUAL( 3, rig._database.countRecords( expiryTable ) );

    ServiceTTLRule        rule = GuildService::makeApplicationTTLRule( 1000 );
    ServiceTTLSweepReport report;
    const int64           firstExpiryMs = kNowMs + GuildLimit::kApplicationLifetimeMs;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 1500, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount ); // 처음 신청만 기간이 지났다
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( applicationTable ) );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( expiryTable ) );  // 다시 낸 신청의 낡은 줄도 지웠다
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( reverseTable ) ); // 지운 신청의 역색인도 같은 트랜잭션에서 지웠다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 3000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( applicationTable ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( expiryTable ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( reverseTable ) );
}

/** @brief [GuildServiceTest] 초대 · 수락: 임원 이상만 초대하고, 자신 · 다른 길드 사람은 초대할 수 없다. 초대 없는 수락 · 만료된 수락은 지고, 수락하면 알림 · 이벤트가 난다 */
SW_TEST_CASE( GuildServiceTest, InviteAndAccept )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();

    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kMember, kOutside )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOfficer )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kOfficer, kInvalidAccountID )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kOfficer, kMember )._result == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kOutside, kMember )._result == SocialResult::NotInGuild );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::NotFound );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOutside )._result == SocialResult::Ok );
    vector<SocialNotification> listNotification;
    rig._service.pollNotifications( listNotification );
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._kind == SocialNotificationKind::GuildInvited );
    SW_EXPECT_EQUAL( kOutside, listNotification[0]._recipientID );
    SW_EXPECT_EQUAL( guildID, listNotification[0]._guildID );

    const int64 expiredMs = kNowMs + GuildLimit::kInviteLifetimeMs;
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOutside, kInvalidAccountID, "", guildID, expiredMs )._result == SocialResult::InviteExpired );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOutside, kInvalidAccountID, "", guildID, expiredMs - 1 )._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sink._listEvent.size() );
    SW_EXPECT_TRUE( rig._sink._listEvent[0]._kind == GuildEvent::Kind::Joined );
    SW_EXPECT_EQUAL( kOutside, rig._sink._listEvent[0]._accountID );

    listNotification.clear();
    rig._service.pollNotifications( listNotification );
    SW_EXPECT_EQUAL( size_t( 3 ), listNotification.size() ); // 들어온 사람을 뺀 멤버 셋이 GuildChanged 를 받는다
    SW_EXPECT_EQUAL( 4, rig.run( GuildOperation::Get, kMaster )._info._memberCount );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::AlreadyInGuild );
}

/** @brief [GuildServiceTest] 정원: 100 명이면 초대받은 사람도 들어오지 못한다(GuildFull). 한 명이 나가면 들어온다 */
SW_TEST_CASE( GuildServiceTest, GuildCapacityIsEnforced )
{
    GuildFixture rig;
    const uint64 guildID = rig.run( GuildOperation::Create, kMaster, kInvalidAccountID, "Full" )._info._guildID;
    const auto   nLast   = static_cast<AccountID>( GuildLimit::kMaxMember ) + 10;
    for ( AccountID accountID = 11; accountID <= nLast; ++accountID )
    {
        SW_REQUIRE_TRUE( rig.run( GuildOperation::Invite, kMaster, accountID )._result == SocialResult::Ok );
    }
    for ( AccountID accountID = 11; accountID < nLast; ++accountID ) // 길드장 + 99 명
    {
        SW_REQUIRE_TRUE( rig.run( GuildOperation::Accept, accountID, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    }
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, nLast, kInvalidAccountID, "", guildID )._result == SocialResult::GuildFull );
    const GuildCompletion full = rig.run( GuildOperation::Get, kMaster );
    SW_EXPECT_EQUAL( GuildLimit::kMaxMember, full._info._memberCount );
    SW_EXPECT_EQUAL( static_cast<size_t>( GuildLimit::kMaxMember ), full._info._listMember.size() );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, 11 )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, nLast, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
}

/** @brief [GuildServiceTest] 떠나기: 멤버는 그냥 떠나고, 길드장은 혼자일 때만 떠나며 그러면 길드가 사라지고 이름이 풀린다. 해산 뒤 남은 초대는 NotFound 다 */
SW_TEST_CASE( GuildServiceTest, LeaveAndDissolve )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();

    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOutside )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMaster )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kOutside )._result == SocialResult::NotInGuild );

    rig.clearSignals();
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMember )._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sink._listEvent.size() );
    SW_EXPECT_TRUE( rig._sink._listEvent[0]._kind == GuildEvent::Kind::Left );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kMember )._result == SocialResult::NotInGuild );
    SW_EXPECT_EQUAL( 2, rig.run( GuildOperation::Get, kMaster )._info._memberCount );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kOfficer )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMaster )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kMaster )._result == SocialResult::NotInGuild );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::NotFound );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Create, kMember, kInvalidAccountID, "Guild" )._result == SocialResult::Ok ); // 이름이 풀렸다
}

/** @brief [GuildServiceTest] 내보내기: 더 높은 역할만 할 수 있고, 내보내진 사람은 알림을 받고 길드가 없어져 다른 길드를 만들 수 있다 */
SW_TEST_CASE( GuildServiceTest, KickFollowsRoleOrder )
{
    GuildFixture rig;
    (void)rig.makeGuild();

    SW_EXPECT_TRUE( rig.run( GuildOperation::Kick, kMember, kOfficer )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Kick, kOfficer, kMaster )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Kick, kMaster, kMaster )._result == SocialResult::NotFound );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Kick, kMaster, kOutside )._result == SocialResult::NotFound );

    rig.clearSignals();
    SW_EXPECT_TRUE( rig.run( GuildOperation::Kick, kOfficer, kMember )._result == SocialResult::Ok );
    vector<SocialNotification> listNotification;
    rig._service.pollNotifications( listNotification );
    bool bKickedNotified = false;
    for ( const SocialNotification& notification : listNotification )
    {
        if ( notification._recipientID == kMember && notification._kind == SocialNotificationKind::GuildChanged )
            bKickedNotified = true;
    }
    SW_EXPECT_TRUE( bKickedNotified );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sink._listEvent.size() );
    SW_EXPECT_EQUAL( kMember, rig._sink._listEvent[0]._accountID );
    SW_EXPECT_TRUE( rig._sink._listEvent[0]._kind == GuildEvent::Kind::Left );
    SW_EXPECT_EQUAL( 2, rig.run( GuildOperation::Get, kMaster )._info._memberCount );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Create, kMember, kInvalidAccountID, "Fresh" )._result == SocialResult::Ok );
}

/** @brief [GuildServiceTest] 역할: 길드장만 바꾼다. 길드장을 넘기면 나는 임원이 되고 길드의 길드장이 바뀐다. 범위 밖 역할은 Invalid 이고 레코드를 망가뜨리지 않는다 */
SW_TEST_CASE( GuildServiceTest, RoleChangesAndMasterTransfer )
{
    GuildFixture rig;
    (void)rig.makeGuild();

    SW_EXPECT_TRUE( rig.setRole( kOfficer, kMember, GuildRole::Officer ) == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.setRole( kMaster, kMaster, GuildRole::Member ) == SocialResult::NotFound );
    SW_EXPECT_TRUE( rig.setRole( kMaster, kMember, GuildRole::Count ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.setRole( kMaster, kMember, static_cast<GuildRole>( 200 ) ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kMember )._result == SocialResult::Ok ); // 레코드가 그대로 읽힌다

    SW_EXPECT_TRUE( rig.setRole( kMaster, kMember, GuildRole::Master ) == SocialResult::Ok );
    const GuildCompletion got = rig.run( GuildOperation::Get, kMember );
    SW_EXPECT_EQUAL( kMember, got._info._masterID );
    for ( const GuildMember& member : got._info._listMember )
    {
        if ( member._accountID == kMember )
            SW_EXPECT_TRUE( member._role == GuildRole::Master );
        if ( member._accountID == kMaster )
            SW_EXPECT_TRUE( member._role == GuildRole::Officer );
    }
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMaster )._result == SocialResult::Ok ); // 넘긴 사람은 이제 떠날 수 있다
}

/** @brief [GuildServiceTest] 공지: 임원 이상이 바꾸고 멤버가 읽는다. 상한을 넘으면 저장하지 않는다(깨진 UTF-8 은 요청 코덱이 막는다) */
SW_TEST_CASE( GuildServiceTest, AnnouncementIsValidatedAndShared )
{
    GuildFixture rig;
    (void)rig.makeGuild();
    SW_EXPECT_TRUE( rig.run( GuildOperation::SetAnnouncement, kMember, kInvalidAccountID, "hi" )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::SetAnnouncement, kOfficer, kInvalidAccountID, "hello\nworld" )._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( string( "hello\nworld" ), rig.run( GuildOperation::Get, kMember )._info._announcement );
    SW_EXPECT_TRUE( rig.run( GuildOperation::SetAnnouncement, kOfficer, kInvalidAccountID, string( static_cast<size_t>( GuildLimit::kMaxAnnouncementSize ) + 1, 'x' ) )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( string( "hello\nworld" ), rig.run( GuildOperation::Get, kMember )._info._announcement );
}

/** @brief [GuildServiceTest] 창고: 맡기기는 누구나, 꺼내기는 임원 이상. 잔액이 모자라면 InsufficientFunds, 분개 키가 없으면 Invalid, 같은 키로 다시 와도 한 번만 움직인다 */
SW_TEST_CASE( GuildServiceTest, StorageMovesFundsOnce )
{
    GuildFixture rig;
    (void)rig.makeGuild();
    rig.grant( kMember, 100 );

    const GuildCompletion deposited = rig.transfer( GuildOperation::Deposit, kMember, 60, "test/dep.1" );
    SW_EXPECT_TRUE( deposited._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), deposited._listStorage.size() );
    SW_EXPECT_EQUAL( int64( 60 ), deposited._listStorage[0]._amount );
    SW_EXPECT_EQUAL( int64( 40 ), rig.getBalanceOf( kMember ) );

    const GuildCompletion replayed = rig.transfer( GuildOperation::Deposit, kMember, 60, "test/dep.1" );
    SW_EXPECT_TRUE( replayed._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), replayed._listStorage.size() );
    SW_EXPECT_EQUAL( int64( 60 ), replayed._listStorage[0]._amount );
    SW_EXPECT_EQUAL( int64( 40 ), rig.getBalanceOf( kMember ) );

    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 61, "test/dep.1" )._result.hasError( OnlineError::kInvalidRequest ) ); // 같은 키에 다른 내용
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 41, "test/dep.2" )._result == SocialResult::InsufficientFunds );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 1, "" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 0, "test/dep.3" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kOutside, 1, "test/dep.4" )._result == SocialResult::NotInGuild );

    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kMember, 1, "test/wd.1" )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 61, "test/wd.2" )._result == SocialResult::InsufficientFunds );
    const GuildCompletion withdrawn = rig.transfer( GuildOperation::Withdraw, kOfficer, 10, "test/wd.3" );
    SW_REQUIRE_EQUAL( size_t( 1 ), withdrawn._listStorage.size() );
    SW_EXPECT_EQUAL( int64( 50 ), withdrawn._listStorage[0]._amount );
    SW_EXPECT_EQUAL( int64( 10 ), rig.getBalanceOf( kOfficer ) );

    const GuildCompletion listed = rig.listStorage( kMember );
    SW_REQUIRE_EQUAL( size_t( 1 ), listed._listStorage.size() );
    SW_EXPECT_EQUAL( string( "cur.gold" ), listed._listStorage[0]._assetID );
    SW_EXPECT_EQUAL( int64( 50 ), listed._listStorage[0]._amount );
}

/** @brief [GuildServiceTest] 창고에 남은 것이 있으면 해산할 수 없고, 다 꺼내면 해산된다 — 재화가 사라지지 않는다 */
SW_TEST_CASE( GuildServiceTest, StorageBlocksDissolution )
{
    GuildFixture rig;
    (void)rig.run( GuildOperation::Create, kMaster, kInvalidAccountID, "Solo" );
    rig.grant( kMaster, 10 );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMaster, 10, "test/dep.1" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMaster )._result == SocialResult::StorageNotEmpty );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kMaster, 10, "test/wd.1" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMaster )._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( int64( 10 ), rig.getBalanceOf( kMaster ) );
}

/** @brief [GuildServiceTest] 신청: 닫힌 길드는 GuildClosed, 길드가 있으면 AlreadyInGuild. 임원 이상이 목록을 보고 받아들이거나 지운다. 받으면 멤버가 되고 신청은 사라진다 */
SW_TEST_CASE( GuildServiceTest, ApplicationsFlow )
{
    GuildFixture        rig;
    const uint64        guildID = rig.makeGuild();
    constexpr AccountID kOther  = 5;

    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::GuildClosed );
    SW_EXPECT_TRUE( rig.setOpen( kMember, true ) == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.setOpen( kOfficer, true ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kMember )._info._bOpenToApply == SW_TRUE );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kOutside, kInvalidAccountID, "", 999 )._result == SocialResult::NotFound );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kMember, kInvalidAccountID, "", guildID )._result == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kOther, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );

    SW_EXPECT_TRUE( rig.run( GuildOperation::ListApplications, kMember )._result == SocialResult::NotAllowed );
    const GuildCompletion listed = rig.run( GuildOperation::ListApplications, kOfficer );
    SW_REQUIRE_EQUAL( size_t( 2 ), listed._listApplication.size() );
    SW_EXPECT_EQUAL( kNowMs, listed._listApplication[0]._appliedMs );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Approve, kMember, kOutside )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Approve, kOfficer, 77 )._result == SocialResult::NotFound );
    rig.clearSignals();
    SW_EXPECT_TRUE( rig.run( GuildOperation::Approve, kOfficer, kOutside )._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sink._listEvent.size() );
    SW_EXPECT_EQUAL( kOutside, rig._sink._listEvent[0]._accountID );
    SW_EXPECT_EQUAL( 4, rig.run( GuildOperation::Get, kOutside )._info._memberCount );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Reject, kOfficer, kOther )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kOther )._result == SocialResult::NotInGuild );
    SW_EXPECT_TRUE( rig.run( GuildOperation::ListApplications, kOfficer )._listApplication.empty() );
}

/** @brief [GuildServiceTest] 신청 만료와 상한: 죽은 신청은 받아들이지 못하고 목록에서 빠지며, 살아 있는 신청이 50 개면 PendingLimit, 죽으면 새 신청이 자리를 얻는다. 그사이 다른 길드에 든 사람은 받을 수 없다 */
SW_TEST_CASE( GuildServiceTest, ApplicationsExpireAndAreCapped )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    SW_EXPECT_TRUE( rig.setOpen( kMaster, true ) == SocialResult::Ok );

    constexpr AccountID kFirstApplicant = 100;
    const auto          nApplicants     = static_cast<AccountID>( GuildLimit::kMaxApplication );
    for ( AccountID offset = 0; offset < nApplicants; ++offset )
    {
        SW_REQUIRE_TRUE( rig.run( GuildOperation::Apply, kFirstApplicant + offset, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    }
    constexpr AccountID kLate = 900;
    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kLate, kInvalidAccountID, "", guildID )._result == SocialResult::PendingLimit );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kFirstApplicant, kInvalidAccountID, "", guildID )._result == SocialResult::Ok ); // 다시 내면 자리를 더 쓰지 않는다

    // 그사이 다른 길드에 들어간 사람
    constexpr AccountID kElsewhere = kFirstApplicant + 1;
    SW_EXPECT_TRUE( rig.run( GuildOperation::Create, kElsewhere, kInvalidAccountID, "Elsewhere" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Approve, kOfficer, kElsewhere )._result == SocialResult::AlreadyInGuild );

    const int64     deadMs = kNowMs + GuildLimit::kApplicationLifetimeMs;
    GuildCompletion dead;
    GuildRequest    listRequest;
    listRequest._operation = GuildOperation::ListApplications;
    listRequest._accountID = kOfficer;
    listRequest._nowMs     = UnixMilliseconds{ deadMs };
    dead                   = rig.runRequest( listRequest );
    SW_EXPECT_TRUE( dead._listApplication.empty() );

    GuildRequest approveRequest = listRequest;
    approveRequest._operation   = GuildOperation::Approve;
    approveRequest._targetID    = kFirstApplicant;
    SW_EXPECT_TRUE( rig.runRequest( approveRequest )._result == SocialResult::InviteExpired );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kLate, kInvalidAccountID, "", guildID, deadMs )._result == SocialResult::Ok );
    listRequest._nowMs = UnixMilliseconds{ deadMs };
    SW_EXPECT_EQUAL( size_t( 1 ), rig.runRequest( listRequest )._listApplication.size() );
}

/** @brief [GuildServiceTest] 경험치와 레벨: 서버 시스템이 늘리고 레벨은 표로 응답 때 정한다 — 표를 바꾸면 저장값 그대로 레벨이 따라간다. 상한에서 멈추고, 틀린 표는 받지 않는다 */
SW_TEST_CASE( GuildServiceTest, ExperienceDrivesLevels )
{
    GuildFixture         rig;
    const uint64         guildID = rig.makeGuild();
    const expected<void> equal   = rig._service.setLevelTable( vector<int64>{ 10, 10 } );
    SW_REQUIRE_FALSE( equal.has_value() );
    SW_EXPECT_FALSE( equal.error().detail().empty() );
    SW_EXPECT_FALSE( rig._service.setLevelTable( vector<int64>{ 0 } ) );
    SW_EXPECT_FALSE( rig._service.setLevelTable( vector<int64>( static_cast<size_t>( GuildLimit::kMaxLevelCount ) + 1, 1 ) ) );
    SW_REQUIRE_TRUE( rig._service.setLevelTable( vector<int64>{ 10, 30 } ) );
    SW_EXPECT_EQUAL( 1, rig._service.computeLevel( 9 ) );
    SW_EXPECT_EQUAL( 2, rig._service.computeLevel( 10 ) );
    SW_EXPECT_EQUAL( 3, rig._service.computeLevel( 1000 ) );

    rig.clearSignals();
    rig._service.addExperience( guildID, 15, UnixMilliseconds{ kNowMs } );
    SW_EXPECT_EQUAL( 1, rig._store.pollCompletions() );
    vector<SocialNotification> listNotification;
    rig._service.pollNotifications( listNotification );
    SW_EXPECT_EQUAL( size_t( 3 ), listNotification.size() ); // 멤버 모두
    vector<GuildCompletion> listCompletion;
    rig._service.takeCompletions( listCompletion );
    SW_EXPECT_TRUE( listCompletion.empty() );

    const GuildCompletion got = rig.run( GuildOperation::Get, kMember );
    SW_EXPECT_EQUAL( int64( 15 ), got._info._experience );
    SW_EXPECT_EQUAL( 2, got._info._level );
    SW_REQUIRE_TRUE( rig._service.setLevelTable( vector<int64>{ 5, 7, 9 } ) );
    SW_EXPECT_EQUAL( 4, rig.run( GuildOperation::Get, kMember )._info._level );

    rig._service.addExperience( guildID, 1ll << 60, UnixMilliseconds{ kNowMs } );
    rig._service.addExperience( guildID, 1ll << 60, UnixMilliseconds{ kNowMs } ); // 두 번 넘겨도 넘치지 않는다
    rig._service.addExperience( guildID, 0, UnixMilliseconds{ kNowMs } );         // 무시된다
    rig._service.addExperience( 999, 5, UnixMilliseconds{ kNowMs } );             // 없는 길드
    while ( rig._store.pollCompletions() > 0 )
    {
    }
    SW_EXPECT_EQUAL( int64( 1 ) << 53, rig.run( GuildOperation::Get, kMember )._info._experience );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [GuildServiceTest] 로그인한 계정의 길드를 찾으면 MemberAttached 이벤트가 나고 완료는 없다. 길드가 없으면 이벤트도 없다 */
SW_TEST_CASE( GuildServiceTest, AttachRaisesEventOnly )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    rig._service.attachAccount( kMember, UnixMilliseconds{ kNowMs } );
    rig._service.attachAccount( kOutside, UnixMilliseconds{ kNowMs } );
    while ( rig._store.pollCompletions() > 0 )
    {
    }
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sink._listEvent.size() );
    SW_EXPECT_TRUE( rig._sink._listEvent[0]._kind == GuildEvent::Kind::MemberAttached );
    SW_EXPECT_EQUAL( kMember, rig._sink._listEvent[0]._accountID );
    SW_EXPECT_EQUAL( guildID, rig._sink._listEvent[0]._guildID );
    vector<GuildCompletion> listCompletion;
    rig._service.takeCompletions( listCompletion );
    SW_EXPECT_TRUE( listCompletion.empty() );
}

/** @brief [GuildServiceTest] 저장소 실패: Unavailable 이고 데이터 · 알림 · 이벤트가 그대로다. 다시 하면 된다. 읽기 실패도 NotInGuild 로 속이지 않는다 */
SW_TEST_CASE( GuildServiceTest, StoreFailuresChangeNothing )
{
    GuildFixture rig;
    (void)rig.makeGuild();
    const uint64 hashBefore = rig._database.computeContentHash();

    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMember )._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( hashBefore, rig._database.computeContentHash() );
    SW_EXPECT_TRUE( rig._sink._listEvent.empty() );
    vector<SocialNotification> listNotification;
    rig._service.pollNotifications( listNotification );
    SW_EXPECT_TRUE( listNotification.empty() );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Get, kMember )._result.hasError( OnlineError::kUnavailable ) );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMember )._result == SocialResult::Ok );
}

/**
 * @brief [GuildServiceTest] 이전 불가 자산(유료 재화)은 맡긴 사람별 몫에 든다 — 다른 임원은 맡긴 사람이 준 허용의 한도 안에서만 꺼내고, 허용을 거두면 못 꺼낸다.
 *        맡긴 본인은 언제나 꺼낸다. 이전 가능 자산은 그대로 공용 창고이다(감사 O-10, 사용자 결정: 맡긴 당사자가 허용한 권한 안에서만)
 */
SW_TEST_CASE( GuildServiceTest, NonTransferableShareNeedsDepositorGrant )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    (void)rig.makeGuild();
    rig.grant( kMember, 100, "cur.gem" );
    rig.grant( kMember, 100 );

    const GuildCompletion deposited = rig.transfer( GuildOperation::Deposit, kMember, 30, "test/gem.dep", "cur.gem" );
    SW_EXPECT_TRUE( deposited._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), deposited._listStorage.size() );
    SW_EXPECT_EQUAL( int64( 30 ), deposited._listStorage[0]._amount );
    SW_EXPECT_EQUAL( kMember, deposited._listStorage[0]._depositorID );
    SW_EXPECT_EQUAL( int64( 70 ), rig.getBalanceOf( kMember, "cur.gem" ) );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 1, "test/gem.dep2", "cur.gem", kMember )._result.hasError( OnlineError::kInvalidRequest ) ); // 몫 주인은 서버가 정한다

    // 허용이 없으면 임원도 못 꺼내고, 몫을 적지 않으면 규칙 밖이다
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 5, "test/gem.wd1", "cur.gem", kMember )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 5, "test/gem.wd2", "cur.gem" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kMaster, 5, "test/gem.wd3", "cur.gem", kMember )._result == SocialResult::NotAllowed );

    // 허용의 규칙: 맡긴 사람이 지금 멤버에게만, 이전 불가 자산에만, 자기 자신은 아니다
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOutside, 10, 101 ) == SocialResult::NotFound );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 10, 102, "cur.gold" ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kMember, 10, 103 ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 10, 0 ).hasError( OnlineError::kInvalidRequest ) );

    // 한도 10 안에서 꺼낸다. 다시 온 같은 부여는 쓴 만큼 줄어든 한도를 되돌리지 않는다
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 10, 1 ) == SocialResult::Ok );
    const GuildCompletion withdrawn = rig.transfer( GuildOperation::Withdraw, kOfficer, 6, "test/gem.wd4", "cur.gem", kMember );
    SW_EXPECT_TRUE( withdrawn._result == SocialResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), withdrawn._listStorage.size() );
    SW_EXPECT_EQUAL( int64( 24 ), withdrawn._listStorage[0]._amount );
    SW_EXPECT_EQUAL( kMember, withdrawn._listStorage[0]._depositorID );
    SW_EXPECT_EQUAL( int64( 6 ), rig.getBalanceOf( kOfficer, "cur.gem" ) );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 10, 1 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 5, "test/gem.wd5", "cur.gem", kMember )._result == SocialResult::NotAllowed ); // 남은 한도 4
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 4, "test/gem.wd6", "cur.gem", kMember )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 4, "test/gem.wd6", "cur.gem", kMember )._result == SocialResult::Ok ); // 한도를 다 쓴 뒤 다시 온 같은 요청
    SW_EXPECT_EQUAL( int64( 10 ), rig.getBalanceOf( kOfficer, "cur.gem" ) );

    // 새 허용을 거두면 다시 못 꺼낸다. 다시 온 회수도 Ok 이다
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 10, 2 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::RevokeWithdrawal, kOfficer, kMember, 0, 10 ) == SocialResult::Ok ); // 남이 준 허용은 거둘 수 없다 — 키의 맡긴 사람이 요청한 계정이라 지울 것이 없다
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::RevokeWithdrawal, kMember, kOfficer, 0, 11 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::RevokeWithdrawal, kMember, kOfficer, 0, 11 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 1, "test/gem.wd7", "cur.gem", kMember )._result == SocialResult::NotAllowed );

    // 맡긴 본인은 허용 없이 꺼낸다(멤버 역할이어도)
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kMember, 5, "test/gem.wd8", "cur.gem", kMember )._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( int64( 75 ), rig.getBalanceOf( kMember, "cur.gem" ) );

    // 이전 가능 자산은 공용 창고 — 몫 주인이 없고 임원이 꺼낸다
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 10, "test/gold.dep" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 1, "test/gold.wd", "cur.gold", kMember )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 1, "test/gold.wd" )._result == SocialResult::Ok );

    const GuildCompletion listed = rig.listStorage( kOfficer );
    SW_REQUIRE_EQUAL( size_t( 2 ), listed._listStorage.size() );
    SW_EXPECT_EQUAL( string( "cur.gold" ), listed._listStorage[0]._assetID );
    SW_EXPECT_EQUAL( int64( 9 ), listed._listStorage[0]._amount );
    SW_EXPECT_EQUAL( kInvalidAccountID, listed._listStorage[0]._depositorID );
    SW_EXPECT_EQUAL( string( "cur.gem" ), listed._listStorage[1]._assetID );
    SW_EXPECT_EQUAL( int64( 15 ), listed._listStorage[1]._amount );
    SW_EXPECT_EQUAL( kMember, listed._listStorage[1]._depositorID );
}

/** @brief [GuildServiceTest] 맡긴 몫이 남은 길드는 해산하지 못하고, 몫의 주인이 꺼내면 해산된다 — 공용 창고만 보던 해산 확인이 맡긴 사람별 몫까지 본다 */
SW_TEST_CASE( GuildServiceTest, DepositorShareBlocksDissolution )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    (void)rig.run( GuildOperation::Create, kMaster, kInvalidAccountID, "Solo" );
    rig.grant( kMaster, 10, "cur.gem" );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMaster, 10, "test/gem.dep", "cur.gem" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.runKeyed( GuildOperation::Leave, kMaster, kInvalidAccountID, "test/leave.1" ) == SocialResult::StorageNotEmpty );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kMaster, 10, "test/gem.wd", "cur.gem", kMaster )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.runKeyed( GuildOperation::Leave, kMaster, kInvalidAccountID, "test/leave.2" ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( int64( 10 ), rig.getBalanceOf( kMaster, "cur.gem" ) );
}

/** @brief [GuildServiceTest] 맡긴 사람이 떠나거나 내보내지면 그의 몫이 같은 트랜잭션에서 그 계정으로 돌아간다 — 멤버가 아니게 된 뒤 아무도 꺼낼 수 없는 몫이 남지 않는다 */
SW_TEST_CASE( GuildServiceTest, DepositorShareReturnsOnLeaveAndKick )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    (void)rig.makeGuild();
    rig.grant( kMember, 20, "cur.gem" );
    rig.grant( kOfficer, 20, "cur.gem" );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 7, "test/gem.dep1", "cur.gem" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kOfficer, 9, "test/gem.dep2", "cur.gem" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kOfficer, kMaster, 5, 1 ) == SocialResult::Ok );

    SW_EXPECT_TRUE( rig.run( GuildOperation::Leave, kMember )._result.hasError( OnlineError::kInvalidRequest ) ); // 돌려주는 이동의 분개 키가 없다
    SW_EXPECT_TRUE( rig.runKeyed( GuildOperation::Leave, kMember, kInvalidAccountID, "test/leave.1" ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( int64( 20 ), rig.getBalanceOf( kMember, "cur.gem" ) );

    SW_EXPECT_TRUE( rig.runKeyed( GuildOperation::Kick, kMaster, kOfficer, "test/kick.1" ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( int64( 20 ), rig.getBalanceOf( kOfficer, "cur.gem" ) );
    SW_EXPECT_TRUE( rig.listStorage( kMaster )._listStorage.empty() );
    SW_EXPECT_TRUE( rig.runKeyed( GuildOperation::Leave, kMaster, kInvalidAccountID, "test/leave.2" ) == SocialResult::Ok ); // 남은 몫 · 허용이 없어 해산된다
}

/** @brief [GuildServiceTest] 허용을 받은 멤버가 떠나면 받은 허용이 같은 트랜잭션에서 지워진다 — 다시 들어와도 맡긴 사람이 모르는 사이 권한이 살아나지 않는다 */
SW_TEST_CASE( GuildServiceTest, ReceivedGrantEndsWithMembership )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    const uint64 guildID = rig.makeGuild();
    rig.grant( kMember, 20, "cur.gem" );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 10, "test/gem.dep", "cur.gem" )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 5, 1 ) == SocialResult::Ok );

    SW_EXPECT_TRUE( rig.runKeyed( GuildOperation::Leave, kOfficer, kInvalidAccountID, "test/leave.1" ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kMaster, kOfficer )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOfficer, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 1, "test/gem.wd", "cur.gem", kMember )._result == SocialResult::NotAllowed );
}

/** @brief [GuildServiceTest] 회수 뒤 늦게 다시 온 같은 부여(같은 멱등 키)는 처음 결과만 돌려주고 허용을 되살리지 않는다 — 부여 · 회수의 키는 지우지 않는 멱등 기록에 남는다 */
SW_TEST_CASE( GuildServiceTest, LateGrantReplayAfterRevokeChangesNothing )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    (void)rig.makeGuild();
    rig.grant( kMember, 20, "cur.gem" );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 10, "test/gem.dep", "cur.gem" )._result == SocialResult::Ok );

    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 5, 1 ) == SocialResult::Ok ); // 키 A
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::RevokeWithdrawal, kMember, kOfficer, 0, 2 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 5, 1 ) == SocialResult::Ok ); // 키 A 재전송
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 1, "test/gem.wd", "cur.gem", kMember )._result == SocialResult::NotAllowed );
}

namespace
{
    /** @brief @p table 의 키 중 `/` 로 나뉜 칸 하나가 계정 @p accountID(16진)와 같은 것의 수입니다(길드 id 와 계정 id 의 16진이 같아도 칸 위치로 가린다). */
    int32 countAccountField( MemoryServiceDatabase& database, const utf8* pTable, AccountID accountID, int32 fieldIndex )
    {
        vector<ServiceRecord> listRecord;
        SW_EXPECT_TRUE( database.listRecords( hashed_string( pTable ), "", "", 1000, false, listRecord ) == ServiceStoreResult::Ok );
        const string     accountHex = ServiceKeyUtil::makeHex64( accountID );
        constexpr size_t kWidth     = static_cast<size_t>( ServiceKeyUtil::kHexWidth );
        int32            count      = 0;
        for ( const ServiceRecord& record : listRecord )
        {
            const size_t offset = static_cast<size_t>( fieldIndex ) * ( kWidth + 1 );
            count += record._key.size() >= offset + kWidth && record._key.compare( offset, kWidth, accountHex ) == 0 ? 1 : 0;
        }
        return count;
    }
} // namespace

/**
 * @brief [GuildServiceTest] 계정 삭제(길드장) → 길드 키트에 그 계정의 레코드 0: 멤버십 · 맡긴 몫(소각) · 준 허용이 지워지고 길드장은 임원에게 넘어간다(감사 P0-7)
 * @details 변이: 길드장 넘기기를 빼면 길드에 길드장이 없고, 몫 소각을 빼면 맡김 잔액이 남아 진다.
 */
SW_TEST_CASE( GuildServiceTest, AccountDeletionHandsOverTheGuild )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    const uint64 guildID = rig.makeGuild();
    rig.grant( kMaster, 100, "cur.gem" );
    SW_REQUIRE_TRUE( rig.transfer( GuildOperation::Deposit, kMaster, 30, "test/gem.master", "cur.gem" )._result == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMaster, kOfficer, 10, 201 ) == SocialResult::Ok );

    SW_REQUIRE_TRUE( GuildService::eraseAccount( rig._database, kMaster, kNowMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_account_guild", kMaster, 0 ) );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_member", kMaster, 1 ) );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_depositor", kMaster, 1 ) );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_storage_grant", kMaster, 1 ) );
    vector<LedgerBalance> listShare;
    SW_REQUIRE_TRUE( Ledger::listBalances( rig._database, LedgerHolder::makeEscrow( "guild", ServiceKeyUtil::makeHex64( guildID ) + "." + ServiceKeyUtil::makeHex64( kMaster ) ), listShare ) ==
                     ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( listShare.empty() ); // 돌려줄 계정이 없다 — 소각이다
    const GuildCompletion got = rig.run( GuildOperation::Get, kOfficer );
    SW_EXPECT_EQUAL( kOfficer, got._info._masterID );
    SW_EXPECT_EQUAL( 2, got._info._memberCount );
    SW_EXPECT_TRUE( GuildService::eraseAccount( rig._database, kMaster, kNowMs ) == ServiceStoreResult::Ok ); // 멱등
}

/** @brief [GuildServiceTest] 계정 삭제(밖의 사람) → 받은 초대와 그 만료 색인, 낸 신청과 역색인이 0 이다(감사 P0-7) */
SW_TEST_CASE( GuildServiceTest, AccountDeletionErasesInvitesAndApplications )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Invite, kMaster, kOutside )._result == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.setOpen( kOfficer, true ) == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Apply, kOutside, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );

    SW_REQUIRE_TRUE( GuildService::eraseAccount( rig._database, kOutside, kNowMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_invite", kOutside, 0 ) );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_invite_expiry", kOutside, 1 ) ); // 색인 키 `<시각>/<나>/<길드>` — 수락한 초대의 줄은 TTL 쓸기가 지운다
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_application", kOutside, 1 ) );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_account_application", kOutside, 0 ) );
}

/** @brief [GuildServiceTest] 계정 삭제(혼자 남은 길드장) → 공용 창고를 소각하고 길드를 해산한다 — 창고가 차 있으면 떠나기는 막히지만 지운 계정은 돌려받을 사람이 없다(감사 P0-7) */
SW_TEST_CASE( GuildServiceTest, AccountDeletionDissolvesASoloGuild )
{
    GuildFixture rig;
    const uint64 guildID = rig.run( GuildOperation::Create, kOutside, kInvalidAccountID, "Solo" )._info._guildID;
    SW_REQUIRE_TRUE( guildID != 0 );
    rig.grant( kOutside, 100 );
    SW_REQUIRE_TRUE( rig.transfer( GuildOperation::Deposit, kOutside, 50, "test/solo.dep" )._result == SocialResult::Ok );

    SW_REQUIRE_TRUE( GuildService::eraseAccount( rig._database, kOutside, kNowMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( hashed_string( "social_guild" ) ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( hashed_string( "social_guild_name" ) ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( hashed_string( "social_guild_member" ) ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( hashed_string( "social_account_guild" ) ) );
    vector<LedgerBalance> listStorage;
    SW_REQUIRE_TRUE( Ledger::listBalances( rig._database, LedgerHolder::makeEscrow( "guild", ServiceKeyUtil::makeHex64( guildID ) ), listStorage ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( listStorage.empty() );
}

namespace
{
    /**
     * @brief 저장소 앞(front)입니다. 일을 맡자마자 연결 @p connection 에서 돌리고, `pollCompletions` 가 완료를 부릅니다.
     * @details 연결을 바꿔 끼울 수 있어 "읽기와 커밋 사이" 에 다른 서버의 일을 끼울 수 있다(메모리 저장소의 앞은 데이터베이스에 직접 붙는다).
     */
    class ConnectionServiceStore final : public IServiceStore
    {
    public:
        explicit ConnectionServiceStore( IServiceStoreConnection& connection )
            : _connection{ connection }
        {
        }

        void submit( unique_ptr<IServiceStoreJob> job ) override
        {
            job->execute( _connection, _commitStats );
            _listDone.push_back( std::move( job ) );
        }
        int32 pollCompletions() override
        {
            vector<unique_ptr<IServiceStoreJob>> listDone = std::move( _listDone );
            _listDone.clear();
            for ( const unique_ptr<IServiceStoreJob>& job : listDone )
            {
                job->complete();
            }
            return static_cast<int32>( listDone.size() );
        }
        int32 getPendingCount() const override { return static_cast<int32>( _listDone.size() ); }
        void  shutdown() override {}

    private:
        IServiceStoreConnection&             _connection;
        vector<unique_ptr<IServiceStoreJob>> _listDone{};
    };

    /** @brief 첫 커밋 바로 앞에 다른 서버(같은 데이터베이스에 붙은 @p otherServer)의 일 @p cut 을 한 번 끼워 넣는 연결입니다. */
    template <typename TCut>
    class InterleavingConnection final : public IServiceStoreConnection
    {
    public:
        InterleavingConnection( GuildFixture& otherServer, TCut cut )
            : _otherServer{ otherServer }
            , _cut{ std::move( cut ) }
        {
        }

        ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override
        {
            return _otherServer._database.readRecord( table, key, outRecord );
        }
        ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending, vector<ServiceRecord>& outListRecord ) override
        {
            return _otherServer._database.listRecords( table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord );
        }
        ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo = nullptr ) override
        {
            if ( _cutCount == 0 )
            {
                ++_cutCount;
                _cut();
            }
            return _otherServer._database.commit( transaction, pOutInfo );
        }

    private:
        GuildFixture& _otherServer;
        TCut          _cut;
        int32         _cutCount{ 0 };
    };

    /** @brief 두 번째 길드 서비스(서버)가 @p connection 으로 요청 하나를 처리한 완료입니다. */
    GuildCompletion runOnSecondServer( IServiceStoreConnection& connection, const GuildRequest& request )
    {
        ConnectionServiceStore store( connection );
        GuildService           service;
        service.initialize( &store );
        service.submit( request, 1 );
        while ( store.pollCompletions() > 0 )
        {
        }
        vector<GuildCompletion> listCompletion;
        service.takeCompletions( listCompletion );
        service.shutdown();
        SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
        return listCompletion.empty() ? GuildCompletion{} : std::move( listCompletion[0] );
    }
} // namespace

/** @brief [GuildServiceTest] 역할을 읽은 뒤 커밋 전에 멤버로 내려간 임원의 공지는 들어가지 않는다 — 멤버 역할 읽기가 커밋 조건이라 다시 읽고 NotAllowed 다(감사 "다음 할 일" 5 번) */
SW_TEST_CASE( GuildServiceTest, DemotionBetweenReadAndCommitRefusesTheAnnouncement )
{
    GuildFixture rig; // 다른 서버 — 같은 데이터베이스에 붙은 길드 서비스
    (void)rig.makeGuild();
    InterleavingConnection connection( rig, [&rig]
    { SW_EXPECT_TRUE( rig.setRole( kMaster, kOfficer, GuildRole::Member ) == SocialResult::Ok ); } );

    GuildRequest request;
    request._operation = GuildOperation::SetAnnouncement;
    request._accountID = kOfficer;
    request._text      = "officer notice";
    request._nowMs     = UnixMilliseconds{ kNowMs };
    SW_EXPECT_TRUE( runOnSecondServer( connection, request )._result == SocialResult::NotAllowed );
    SW_EXPECT_EQUAL( string(), rig.run( GuildOperation::Get, kMember )._info._announcement );
}

/**
 * @brief [GuildServiceTest] 떠나는 임원이 허용 목록을 읽은 뒤 커밋 전에 그에게 준 허용은 떠나기와 함께 지워진다 — 읽기 집합이 허용 목록을 범위 조건으로 걸어
 *        끼어든 부여(팬텀)에 떠나기 커밋이 Conflict 로 다시 읽는다. 다시 들어와도 그 허용으로 꺼내지 못한다(감사 "다음 할 일" 5 번)
 */
SW_TEST_CASE( GuildServiceTest, GrantBetweenReadAndCommitEndsWithTheLeave )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    const uint64 guildID = rig.makeGuild();
    rig.grant( kMember, 20, "cur.gem" );
    SW_REQUIRE_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 10, "test/gem.dep", "cur.gem" )._result == SocialResult::Ok );
    InterleavingConnection connection( rig, [&rig]
    { SW_EXPECT_TRUE( rig.setGrant( GuildOperation::GrantWithdrawal, kMember, kOfficer, 5, 1 ) == SocialResult::Ok ); } );

    GuildRequest leave;
    leave._operation  = GuildOperation::Leave;
    leave._accountID  = kOfficer;
    leave._journalKey = "test/leave.1";
    leave._nowMs      = UnixMilliseconds{ kNowMs };
    SW_EXPECT_TRUE( runOnSecondServer( connection, leave )._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( 0, countAccountField( rig._database, "social_guild_storage_grant", kOfficer, 2 ) ); // 키 `<길드>/<맡긴 사람>/<받는 계정>/<자산>`
    SW_EXPECT_TRUE( rig.run( GuildOperation::Invite, kMaster, kOfficer )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.run( GuildOperation::Accept, kOfficer, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    SW_EXPECT_TRUE( rig.transfer( GuildOperation::Withdraw, kOfficer, 1, "test/gem.wd", "cur.gem", kMember )._result == SocialResult::NotAllowed );
}

/** @brief [GuildServiceTest] 신청 목록을 읽은 뒤 커밋 전에 다른 신청이 마지막 자리를 채우면 이 신청은 PendingLimit 다 — 신청 목록이 범위 조건이라 다시 세어 상한을 넘지 않는다(감사 "다음 할 일" 5 번) */
SW_TEST_CASE( GuildServiceTest, ApplicationBetweenReadAndCommitKeepsTheCap )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    SW_REQUIRE_TRUE( rig.setOpen( kMaster, true ) == SocialResult::Ok );
    constexpr AccountID kFirstApplicant = 100;
    const auto          nApplicants     = static_cast<AccountID>( GuildLimit::kMaxApplication ) - 1;
    for ( AccountID offset = 0; offset < nApplicants; ++offset )
    {
        SW_REQUIRE_TRUE( rig.run( GuildOperation::Apply, kFirstApplicant + offset, kInvalidAccountID, "", guildID )._result == SocialResult::Ok );
    }
    constexpr AccountID    kOtherApplicant = 900;
    InterleavingConnection connection( rig, [&rig, guildID]
    { SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, kOtherApplicant, kInvalidAccountID, "", guildID )._result == SocialResult::Ok ); } );

    GuildRequest apply;
    apply._operation = GuildOperation::Apply;
    apply._accountID = kOtherApplicant + 1;
    apply._guildID   = guildID;
    apply._nowMs     = UnixMilliseconds{ kNowMs };
    SW_EXPECT_TRUE( runOnSecondServer( connection, apply )._result == SocialResult::PendingLimit );
    SW_EXPECT_EQUAL( static_cast<int32>( GuildLimit::kMaxApplication ), static_cast<int32>( rig._database.countRecords( hashed_string( "social_guild_application" ) ) ) ); // 목록 응답은 상한에서 잘리므로 표를 센다
}

/**
 * @brief [GuildServiceTest] 창고 품목 300 개(공용 창고 291, 맡긴 사람 셋의 몫 9)를 커서로 쪽마다 읽으면 끝까지 빠짐 · 겹침 없이 키 순서로 오고, 마지막 쪽 뒤 커서가 빈다
 * @details 쪽 크기 7 은 보유자 경계(공용 창고 → 몫, 몫 → 몫)를 쪽 가운데에 걸치게 한다. 옛 구현은 상한 128 에서 잘라 뒤 172 개를 보이지 않았다.
 */
SW_TEST_CASE( GuildServiceTest, StoragePagesReachEveryItem )
{
    GemPrefixPolicy policy;
    GuildFixture    rig;
    rig._service.setLedgerPolicy( &policy );
    const uint64 guildID = rig.makeGuild();

    constexpr int32 kCommonCount = 291;
    for ( int32 index = 0; index < kCommonCount; ++index )
    {
        string assetID = to_string( index );
        assetID        = "item.a" + string( 3 - assetID.size(), '0' ) + assetID;
        LedgerTransferRequest request;
        request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeEscrow( "guild", ServiceKeyUtil::makeHex64( guildID ) ), assetID, 1 + index } );
        request._journalKey = "test/storage/" + assetID;
        request._reason     = "test.grant";
        LedgerTransferResult result;
        SW_REQUIRE_TRUE( Ledger::executeTransfer( rig._database, request, result ) == LedgerResult::Ok );
    }
    for ( const AccountID depositorID : { kMaster, kOfficer, kMember } )
    {
        for ( const string_view assetID : { "gem.blue", "gem.green", "gem.red" } )
        {
            rig.grant( depositorID, 5, assetID );
            const string journalKey = "test/share/" + to_string( depositorID ) + "/" + string( assetID );
            SW_REQUIRE_TRUE( rig.transfer( GuildOperation::Deposit, depositorID, 5, journalKey, assetID )._result == SocialResult::Ok );
        }
    }

    vector<GuildStorageItem> listAll;
    GuildStorageCursor       cursor;
    int32                    pageCount = 0;
    while ( true )
    {
        GuildCompletion page = rig.listStorage( kMember, cursor, 7 );
        SW_REQUIRE_TRUE( page._result == SocialResult::Ok );
        SW_REQUIRE_TRUE( page._listStorage.size() <= size_t( 7 ) );
        listAll.insert( listAll.end(), page._listStorage.begin(), page._listStorage.end() );
        ++pageCount;
        SW_REQUIRE_TRUE( pageCount <= 50 ); // 커서가 나아가지 않으면 멈춘다
        if ( page._storageNextCursor.isEnd() )
            break;
        cursor = page._storageNextCursor;
    }
    SW_REQUIRE_EQUAL( size_t( 300 ), listAll.size() );
    for ( size_t index = 1; index < listAll.size(); ++index ) // 키 순서로 엄격히 늘어난다 — 겹침이 없다
    {
        const GuildStorageItem& before     = listAll[index - 1];
        const GuildStorageItem& after      = listAll[index];
        const bool              bAscending = before._depositorID < after._depositorID || ( before._depositorID == after._depositorID && before._assetID < after._assetID );
        SW_EXPECT_TRUE( bAscending );
    }
    for ( int32 index = 0; index < kCommonCount; ++index ) // 공용 창고가 먼저, 양까지 그대로
    {
        SW_EXPECT_EQUAL( kInvalidAccountID, listAll[static_cast<size_t>( index )]._depositorID );
        SW_EXPECT_EQUAL( int64( 1 + index ), listAll[static_cast<size_t>( index )]._amount );
    }
    SW_EXPECT_EQUAL( kMaster, listAll[static_cast<size_t>( kCommonCount )]._depositorID );
    SW_EXPECT_EQUAL( string( "gem.blue" ), listAll[static_cast<size_t>( kCommonCount )]._assetID );
    SW_EXPECT_EQUAL( kMember, listAll.back()._depositorID );
    SW_EXPECT_EQUAL( string( "gem.red" ), listAll.back()._assetID );
}

/**
 * @brief [GuildServiceTest] 클라이언트가 보낸 쪽 크기는 서버 상한(`kMaxStoragePage`)으로 잘리고, 1 보다 작은 크기 · 자산 id 가 아닌 커서 · 자산 없이 맡긴 사람만 든 커서는 규칙 밖이다
 */
SW_TEST_CASE( GuildServiceTest, StoragePageSizeIsCappedByTheServer )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    for ( int32 index = 0; index < GuildLimit::kMaxStoragePage + 10; ++index )
    {
        const string          assetID = "item.b" + to_string( 100 + index );
        LedgerTransferRequest request;
        request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeEscrow( "guild", ServiceKeyUtil::makeHex64( guildID ) ), assetID, 1 } );
        request._journalKey = "test/storage/" + assetID;
        request._reason     = "test.grant";
        LedgerTransferResult result;
        SW_REQUIRE_TRUE( Ledger::executeTransfer( rig._database, request, result ) == LedgerResult::Ok );
    }

    const GuildCompletion capped = rig.listStorage( kMember, GuildStorageCursor{}, 1000000 );
    SW_REQUIRE_TRUE( capped._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( size_t( GuildLimit::kMaxStoragePage ), capped._listStorage.size() );
    SW_REQUIRE_FALSE( capped._storageNextCursor.isEnd() );
    const GuildCompletion rest = rig.listStorage( kMember, capped._storageNextCursor, 1000000 );
    SW_EXPECT_EQUAL( size_t( 10 ), rest._listStorage.size() );
    SW_EXPECT_TRUE( rest._storageNextCursor.isEnd() );

    SW_EXPECT_TRUE( rig.listStorage( kMember, GuildStorageCursor{}, 0 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.listStorage( kMember, GuildStorageCursor{}, -5 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.listStorage( kMember, GuildStorageCursor{ "BAD/../key", kInvalidAccountID } )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.listStorage( kMember, GuildStorageCursor{ "", kMaster } )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.listStorage( kOutside )._result == SocialResult::NotInGuild );
}

namespace
{
    /** @brief 서비스 레코드를 저장소에 바로 씁니다 — API 로는 만들 수 없는 "상한 한 쪽보다 많은" 상태를 세운다(옛 데이터 · 다른 판 서버 · 불변식이 깨진 뒤). */
    struct GuildRawRecord
    {
        static vector<uint8> encode( std::initializer_list<int64> listValue )
        {
            BitWriter writer;
            writer.writeBits( 1, 8 ); // 서비스의 레코드 형식 판
            for ( const int64 value : listValue )
            {
                writer.writeVarInt( value );
            }
            return writer.getBytes();
        }

        static string makePairKey( uint64 first, uint64 second ) { return ServiceKeyUtil::makeHex64( first ) + "/" + ServiceKeyUtil::makeHex64( second ); }

        static void put( MemoryServiceDatabase& database, const utf8* pTable, const string& key, const vector<uint8>& bytes )
        {
            ServiceTransaction transaction;
            transaction.put( hashed_string( pTable ), key, bytes, ServiceRecord::kAnyVersion );
            SW_REQUIRE_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        static int32 countPrefix( MemoryServiceDatabase& database, const utf8* pTable, const string& prefix )
        {
            int32                    count  = 0;
            const ServiceStoreResult listed = ServiceRecordReader::visitAll( database, hashed_string( pTable ), prefix, 64, [&]( const ServiceRecord& )
            {
                ++count;
                return true;
            } );
            SW_EXPECT_TRUE( listed == ServiceStoreResult::Ok );
            return count;
        }
    };
} // namespace

/**
 * @brief [GuildServiceTest] 죽은 신청 60 개가 키 앞을 채우고 살아 있는 신청이 50 개면, 새 신청은 PendingLimit 이고 목록은 살아 있는 50 개를 모두 보인다
 * @details 옛 구현은 한 쪽(50)만 읽어 앞의 죽은 신청만 보고 살아 있는 수를 0 으로 셌다 — 상한을 넘겨 받고, 목록은 비었다.
 */
SW_TEST_CASE( GuildServiceTest, ApplicationCapCountsPastDeadApplications )
{
    GuildFixture rig;
    const uint64 guildID = rig.makeGuild();
    SW_EXPECT_TRUE( rig.setOpen( kMaster, true ) == SocialResult::Ok );
    for ( AccountID applicant = 10; applicant < 70; ++applicant ) // 죽은 신청(키가 앞이다)
    {
        GuildRawRecord::put( rig._database, "social_guild_application", GuildRawRecord::makePairKey( guildID, applicant ), GuildRawRecord::encode( { kNowMs - 2000, kNowMs - 1000 } ) );
    }
    for ( AccountID applicant = 1000; applicant < 1000 + static_cast<AccountID>( GuildLimit::kMaxApplication ); ++applicant ) // 살아 있는 신청
    {
        GuildRawRecord::put( rig._database, "social_guild_application", GuildRawRecord::makePairKey( guildID, applicant ),
                             GuildRawRecord::encode( { kNowMs, kNowMs + GuildLimit::kApplicationLifetimeMs } ) );
    }

    SW_EXPECT_TRUE( rig.run( GuildOperation::Apply, 5000, kInvalidAccountID, "", guildID )._result == SocialResult::PendingLimit );
    const GuildCompletion listed = rig.run( GuildOperation::ListApplications, kOfficer );
    SW_REQUIRE_TRUE( listed._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( size_t( GuildLimit::kMaxApplication ), listed._listApplication.size() );
}

/**
 * @brief [GuildServiceTest] 맡긴 사람이 떠나면 그가 준 꺼내기 허용을 상한(32)보다 많아도 모두 지운다 — 옛 구현은 한 쪽만 지워 남은 허용이 다시 들어온 멤버에게 살아났다
 */
SW_TEST_CASE( GuildServiceTest, LeavingErasesEveryGrantPastOnePage )
{
    PaidGemPolicy policy;
    GuildFixture  rig;
    rig._service.setLedgerPolicy( &policy );
    const uint64 guildID = rig.makeGuild();
    rig.grant( kMember, 10, "cur.gem" );
    SW_REQUIRE_TRUE( rig.transfer( GuildOperation::Deposit, kMember, 10, "test/grant.dep", "cur.gem" )._result == SocialResult::Ok );
    const string    prefix      = GuildRawRecord::makePairKey( guildID, kMember ) + "/";
    constexpr int32 kGrantCount = GuildLimit::kMaxStorageGrant + 8;
    for ( int32 index = 0; index < kGrantCount; ++index )
    {
        const string key = prefix + ServiceKeyUtil::makeHex64( kOfficer ) + "/cur.g" + to_string( 100 + index );
        GuildRawRecord::put( rig._database, "social_guild_storage_grant", key, GuildRawRecord::encode( { 1 } ) );
    }

    SW_REQUIRE_TRUE( rig.runKeyed( GuildOperation::Leave, kMember, kInvalidAccountID, "test/grant.leave" ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( 0, GuildRawRecord::countPrefix( rig._database, "social_guild_storage_grant", prefix ) );
    SW_EXPECT_EQUAL( int64( 10 ), rig.getBalanceOf( kMember, "cur.gem" ) );
}

/**
 * @brief [GuildServiceTest] 혼자 남은 길드장의 계정을 지우면 맡긴 사람 기록이 한 쪽(100)보다 많아도 모두 태우고 지운다 — 옛 구현은 뒤의 몫을 해산한 길드에 남겼다
 */
SW_TEST_CASE( GuildServiceTest, AccountDeletionBurnsEveryShareRecordPastOnePage )
{
    GuildFixture rig;
    const uint64 guildID = rig.run( GuildOperation::Create, kOutside, kInvalidAccountID, "Solo" )._info._guildID;
    SW_REQUIRE_TRUE( guildID != 0 );
    constexpr AccountID kLastDepositor = 10 + static_cast<AccountID>( GuildLimit::kMaxMember ) + 20;
    for ( AccountID depositorID = 10; depositorID <= kLastDepositor; ++depositorID )
    {
        GuildRawRecord::put( rig._database, "social_guild_depositor", GuildRawRecord::makePairKey( guildID, depositorID ), vector<uint8>{ 1 } );
    }
    // 마지막 기록의 몫에 잔액을 둔다 — 한 쪽 밖이다
    string token = ServiceKeyUtil::makeHex64( guildID ) + ".";
    ServiceKeyUtil::appendHex64( token, kLastDepositor );
    LedgerTransferRequest request;
    request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeEscrow( "guild", token ), "cur.gem", 7 } );
    request._journalKey = "test/share.last";
    request._reason     = "test.grant";
    LedgerTransferResult result;
    SW_REQUIRE_TRUE( Ledger::executeTransfer( rig._database, request, result ) == LedgerResult::Ok );

    SW_REQUIRE_TRUE( GuildService::eraseAccount( rig._database, kOutside, kNowMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( hashed_string( "social_guild_depositor" ) ) );
    vector<LedgerBalance> listShare;
    SW_REQUIRE_TRUE( Ledger::listBalances( rig._database, LedgerHolder::makeEscrow( "guild", token ), listShare ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( listShare.empty() );
}
