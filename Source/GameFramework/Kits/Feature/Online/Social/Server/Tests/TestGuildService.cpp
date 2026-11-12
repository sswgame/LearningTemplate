// GuildServiceTest — 길드 로직(메모리 저장소): 만들기 · 이름 유일, 초대 · 수락 · 만료 · 정원, 떠나기 · 해산, 내보내기 · 역할 · 길드장 넘기기, 공지, 창고(맡기기 · 꺼내기 · 멱등 · 해산 막기,
// 이전 불가 자산의 맡긴 사람별 몫 · 꺼내기 허용 · 떠날 때 돌려주기),
// 공개 길드 신청, 경험치 · 레벨, 알림과 이벤트, 저장소 실패. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

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
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOutside )._result == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.run( GuildOperation::Invite, kOfficer, kOutside, "", 0, kNowMs + 1000 )._result == SocialResult::Ok ); // 다시 보내 만료가 늦춰졌다
    const hashed_string inviteTable{ "social_guild_invite" };
    const hashed_string expiryTable{ "social_guild_invite_expiry" };
    SW_REQUIRE_EQUAL( 1, rig._database.countRecords( inviteTable ) );
    SW_REQUIRE_EQUAL( 2, rig._database.countRecords( expiryTable ) );

    ServiceTTLRule        rule = GuildService::makeInviteTTLRule( 1000 );
    ServiceTTLSweepReport report;
    const int64           firstExpiryMs = kNowMs + GuildLimit::kInviteLifetimeMs;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 1500, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, report._deletedCount );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( inviteTable ) );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( expiryTable ) ); // 낡은 줄만 지웠다
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
    SW_REQUIRE_EQUAL( 2, rig._database.countRecords( applicationTable ) );
    SW_REQUIRE_EQUAL( 3, rig._database.countRecords( expiryTable ) );

    ServiceTTLRule        rule = GuildService::makeApplicationTTLRule( 1000 );
    ServiceTTLSweepReport report;
    const int64           firstExpiryMs = kNowMs + GuildLimit::kApplicationLifetimeMs;
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 1500, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount ); // 처음 신청만 기간이 지났다
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( applicationTable ) );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( expiryTable ) ); // 다시 낸 신청의 낡은 줄도 지웠다
    SW_REQUIRE_TRUE( ServiceTTLRule::sweep( rig._database, rule, firstExpiryMs + 3000, 100, 100, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, report._deletedCount );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( applicationTable ) );
    SW_EXPECT_EQUAL( 0, rig._database.countRecords( expiryTable ) );
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
SW_TEST_CASE( GuildServiceTest, NoticeIsValidatedAndShared )
{
    GuildFixture rig;
    (void)rig.makeGuild();
    SW_EXPECT_TRUE( rig.run( GuildOperation::SetNotice, kMember, kInvalidAccountID, "hi" )._result == SocialResult::NotAllowed );
    SW_EXPECT_TRUE( rig.run( GuildOperation::SetNotice, kOfficer, kInvalidAccountID, "hello\nworld" )._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( string( "hello\nworld" ), rig.run( GuildOperation::Get, kMember )._info._notice );
    SW_EXPECT_TRUE( rig.run( GuildOperation::SetNotice, kOfficer, kInvalidAccountID, string( static_cast<size_t>( GuildLimit::kMaxNoticeSize ) + 1, 'x' ) )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( string( "hello\nworld" ), rig.run( GuildOperation::Get, kMember )._info._notice );
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

    const GuildCompletion listed = rig.run( GuildOperation::ListStorage, kMember );
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

    const GuildCompletion listed = rig.run( GuildOperation::ListStorage, kOfficer );
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
    SW_EXPECT_TRUE( rig.run( GuildOperation::ListStorage, kMaster )._listStorage.empty() );
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
