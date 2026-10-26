// 길드 — 만들기(이름 유일 · 대소문자 무시 · 한 계정 한 길드 · 이름 규칙), 초대 → 수락(정원 · 만료), 역할(임원만 초대, 길드장 넘기기), 내보내기,
// 떠나기(길드장은 혼자일 때 해산 — 이름이 다시 비게), 사건(가입 · 떠남 · 로그인한 회원) · 알림, 저장소 실패는 아무것도 쓰지 않음,
// 길드 창고(맡기기 · 임원만 꺼내기 · 멱등 · 잔액 부족 · 남은 창고는 해산 막음), 레벨(경험치와 레벨 표), 공개 길드 가입 신청(받아들이기 · 지우기 · 비공개 거절).
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct GuildNode
    {
        MemoryServiceStore         _store;
        GuildService               _service;
        vector<GuildCompletion>    _listCompletion;
        vector<GuildEvent>         _listEvent;
        vector<SocialNotification> _listNotification;

        explicit GuildNode( MemoryServiceDatabase* pDatabase )
            : _store{ pDatabase }
            , _service{}
            , _listCompletion{}
            , _listEvent{}
            , _listNotification{}
        {
            _service.initialize( &_store );
            _eventSubscription = _service.bindEventDelegate( GuildEventDelegate::create<&GuildNode::onEvent>( this ) );
        }

        Subscription _eventSubscription; ///< 길드 이벤트 칸 구독(노드와 함께 풀린다)

        void onEvent( const GuildEvent& event ) { _listEvent.push_back( event ); }

        SocialResult run( GuildOperation operation, AccountID accountID, int64 nowMs, AccountID targetID = kInvalidAccountID, uint64 guildID = 0,
                          string_view text = string_view{}, GuildRole role = GuildRole::Member )
        {
            GuildRequest request;
            request._operation = operation;
            request._accountID = accountID;
            request._targetID  = targetID;
            request._guildID   = guildID;
            request._text      = string( text );
            request._role      = role;
            request._nowMs     = nowMs;
            _service.submit( request, 1 );
            (void)_store.pollCompletions();
            _listCompletion.clear();
            _service.drainCompletions( _listCompletion );
            _service.drainNotifications( _listNotification );
            return _listCompletion.empty() ? SocialResult::Count : _listCompletion.back()._result;
        }

        uint64 getLastGuildID() const { return _listCompletion.back()._info._guildID; }

        /** @brief 창고 이동 하나입니다. @p keyLow 가 0 이면 분개 키 없이 보냅니다. */
        SocialResult runStorage( GuildOperation operation, AccountID accountID, const utf8* pAsset, int64 amount, uint64 keyLow )
        {
            GuildRequest request;
            request._operation = operation;
            request._accountID = accountID;
            request._assetID   = pAsset;
            request._amount    = amount;
            request._nowMs     = 1000;
            if ( keyLow != 0 )
                request._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( accountID ), 1, keyLow );
            _service.submit( request, 1 );
            (void)_store.pollCompletions();
            _listCompletion.clear();
            _service.drainCompletions( _listCompletion );
            _service.drainNotifications( _listNotification );
            return _listCompletion.empty() ? SocialResult::Count : _listCompletion.back()._result;
        }
    };

    void grant( MemoryServiceDatabase& database, AccountID accountID, const utf8* pAsset, int64 amount )
    {
        LedgerTransferRequest request;
        request._journalKey = string( "test/grant." ) + ServiceKeyUtil::makeHex64( accountID );
        request._reason     = "test.grant";
        request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountID ), pAsset, amount } );
        LedgerTransferOutcome outcome;
        (void)Ledger::executeTransfer( database, request, outcome );
    }

    int64 readAmount( MemoryServiceDatabase& database, const LedgerHolder& holder, const utf8* pAsset )
    {
        LedgerBalance balance;
        (void)Ledger::readBalance( database, holder, pAsset, balance ); // 메모리 저장소라 실패하지 않고, 레코드가 없으면 0 이 남는다
        return balance._amount;
    }
} // namespace

SW_TEST_CASE( GuildServiceTest, CreateIsUniqueByNameAndOnePerAccount )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Knights" ) == SocialResult::Ok );
    const uint64 guildID = node.getLastGuildID();
    SW_EXPECT_NOT_EQUAL( guildID, uint64( 0 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountID, 0, "KNIGHTS" ) == SocialResult::GuildNameTaken );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Other" ) == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountID, 0, "" ) == SocialResult::Invalid );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountID, 0, " Lead" ) == SocialResult::Invalid );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountID, 0, "bad\x01name" ) == SocialResult::Invalid );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountID, 0, "\xEA\xB8\xB0\xEC\x82\xAC\xEB\x8B\xA8" ) == SocialResult::Ok ); // "기사단"
    SW_EXPECT_NOT_EQUAL( node.getLastGuildID(), guildID );
    SW_ASSERT_EQUAL( node._listEvent.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( node._listEvent[0]._kind == GuildEvent::Kind::Joined && node._listEvent[0]._guildID == guildID );

    node._service.attachAccount( 1, 10 ); // 로그인한 회원 — 완료 없이 사건만
    (void)node._store.pollCompletions();
    vector<GuildCompletion> listCompletion;
    node._service.drainCompletions( listCompletion );
    SW_EXPECT_EQUAL( listCompletion.size(), size_t( 0 ) );
    SW_EXPECT_TRUE( node._listEvent.back()._kind == GuildEvent::Kind::MemberAttached && node._listEvent.back()._guildID == guildID );
    SW_EXPECT_EQUAL( node._listEvent.back()._nowMs, int64( 10 ) ); // 길드 채널 멤버를 넣을 시각이다
}

SW_TEST_CASE( GuildServiceTest, InviteAcceptRolesKickAndLeave )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Knights" ) == SocialResult::Ok );
    const uint64 guildID = node.getLastGuildID();

    SW_EXPECT_TRUE( node.run( GuildOperation::Invite, 1, 0, 2 ) == SocialResult::Ok );
    SW_ASSERT_TRUE( node._listNotification.empty() == false );
    SW_EXPECT_TRUE( node._listNotification.back()._kind == SocialNotificationKind::GuildInvited && node._listNotification.back()._recipientID == AccountID( 2 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 2, GuildLimit::kInviteLifetimeMs, kInvalidAccountID, guildID ) == SocialResult::InviteExpired );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 2, 100, kInvalidAccountID, guildID ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 2, 100, kInvalidAccountID, guildID ) == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Invite, 2, 200, 3 ) == SocialResult::NotAllowed ); // 회원은 초대 못 함
    SW_EXPECT_TRUE( node.run( GuildOperation::SetNotice, 2, 200, kInvalidAccountID, 0, "hi" ) == SocialResult::NotAllowed );

    node._listNotification.clear();
    SW_EXPECT_TRUE( node.run( GuildOperation::SetRole, 1, 300, 2, 0, string_view{}, GuildRole::Master ) == SocialResult::Ok ); // 길드장 넘기기
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 1 ) );                                                             // 바꾼 이 말고 회원 하나에게
    SW_EXPECT_TRUE( node._listNotification[0]._kind == SocialNotificationKind::GuildChanged && node._listNotification[0]._recipientID == AccountID( 2 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Leave, 2, 400 ) == SocialResult::NotAllowed );   // 새 길드장은 회원이 있어 못 떠남
    SW_EXPECT_TRUE( node.run( GuildOperation::Kick, 1, 450, 2 ) == SocialResult::NotAllowed ); // 임원이 길드장을 내보내지 못함
    SW_EXPECT_TRUE( node.run( GuildOperation::Kick, 2, 500, 1 ) == SocialResult::Ok );         // 길드장이 임원을 내보냄
    SW_EXPECT_TRUE( node._listEvent.back()._kind == GuildEvent::Kind::Left && node._listEvent.back()._accountID == AccountID( 1 ) );
    SW_EXPECT_TRUE( node._listNotification.back()._recipientID == AccountID( 1 ) ); // 내보내진 사람도 안다

    SW_EXPECT_TRUE( node.run( GuildOperation::Get, 2, 600 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._memberCount, 1 );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._masterID, AccountID( 2 ) );
    SW_ASSERT_EQUAL( node._listCompletion.back()._info._listMember.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion.back()._info._listMember[0]._role == GuildRole::Master );
    SW_EXPECT_TRUE( node.run( GuildOperation::Get, 1, 600 ) == SocialResult::NotInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Leave, 2, 700 ) == SocialResult::Ok );                                    // 혼자 — 해산
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 3, 800, kInvalidAccountID, 0, "knights" ) == SocialResult::Ok );  // 이름이 다시 비었다
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 4, 800, kInvalidAccountID, guildID ) == SocialResult::NotFound ); // 초대 없음
}

SW_TEST_CASE( GuildServiceTest, LevelFollowsExperienceAndTheLevelTable )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_EXPECT_FALSE( node._service.setLevelTable( { 100, 100 } ) ); // 늘어나야 한다
    SW_EXPECT_FALSE( node._service.setLevelTable( { 0 } ) );
    SW_ASSERT_TRUE( node._service.setLevelTable( { 100, 300 } ) );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Lead" ) == SocialResult::Ok );
    const uint64 guildID = node.getLastGuildID();
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._level, 1 );
    node._service.addExperience( guildID, 150, 10 );
    (void)node._store.pollCompletions();
    SW_ASSERT_TRUE( node.run( GuildOperation::Get, 1, 20 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._experience, int64( 150 ) );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._level, 2 );
    node._service.addExperience( guildID, 1000, 30 );
    (void)node._store.pollCompletions();
    SW_ASSERT_TRUE( node.run( GuildOperation::Get, 1, 40 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._level, 3 ); // 표의 끝이다
    SW_ASSERT_TRUE( node._service.setLevelTable( { 2000 } ) );      // 표가 바뀌면 저장된 경험치는 그대로이고 레벨만 따라간다
    SW_ASSERT_TRUE( node.run( GuildOperation::Get, 1, 50 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._level, 1 );
}

SW_TEST_CASE( GuildServiceTest, OpenGuildTakesApplicationsThatOfficersApproveOrReject )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Lead" ) == SocialResult::Ok );
    const uint64 guildID = node.getLastGuildID();
    SW_EXPECT_TRUE( node.run( GuildOperation::Apply, 2, 10, kInvalidAccountID, guildID ) == SocialResult::GuildClosed );

    GuildRequest open;
    open._operation = GuildOperation::SetJoinPolicy;
    open._accountID = 1;
    open._bOpen     = SW_TRUE;
    open._nowMs     = 20;
    node._service.submit( open, 1 );
    (void)node._store.pollCompletions();
    SW_EXPECT_TRUE( node.run( GuildOperation::Apply, 2, 30, kInvalidAccountID, guildID ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Apply, 3, 30, kInvalidAccountID, guildID ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Apply, 2, 40, kInvalidAccountID, guildID ) == SocialResult::Ok ); // 다시 내면 기한만 늘어난다
    SW_EXPECT_TRUE( node.run( GuildOperation::Apply, 1, 40, kInvalidAccountID, guildID ) == SocialResult::AlreadyInGuild );

    SW_EXPECT_TRUE( node.run( GuildOperation::ListApplications, 2, 50 ) == SocialResult::NotInGuild ); // 신청자는 멤버가 아니다
    SW_ASSERT_TRUE( node.run( GuildOperation::ListApplications, 1, 50 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listApplication.size(), size_t( 2 ) );

    SW_ASSERT_TRUE( node.run( GuildOperation::Approve, 1, 60, 2 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node._listEvent.back()._kind == GuildEvent::Kind::Joined && node._listEvent.back()._accountID == AccountID( 2 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Approve, 1, 60, 2 ) == SocialResult::NotFound );   // 신청은 지워졌다
    SW_EXPECT_TRUE( node.run( GuildOperation::Approve, 2, 70, 3 ) == SocialResult::NotAllowed ); // 새 멤버는 임원이 아니다
    SW_ASSERT_TRUE( node.run( GuildOperation::Reject, 1, 70, 3 ) == SocialResult::Ok );
    SW_ASSERT_TRUE( node.run( GuildOperation::ListApplications, 1, 80 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node._listCompletion.back()._listApplication.empty() );

    SW_EXPECT_TRUE( node.run( GuildOperation::Apply, 4, 90, kInvalidAccountID, guildID ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Approve, 1, 90 + GuildLimit::kApplicationLifetimeMs, 4 ) == SocialResult::InviteExpired ); // 죽은 신청이다
}

SW_TEST_CASE( GuildServiceTest, FullGuildAndStoreFailure )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Big" ) == SocialResult::Ok );
    const uint64 guildID = node.getLastGuildID();
    for ( AccountID member = 2; member <= static_cast<AccountID>( GuildLimit::kMaxMember ); ++member )
    {
        SW_ASSERT_TRUE( node.run( GuildOperation::Invite, 1, 0, member ) == SocialResult::Ok );
        SW_ASSERT_TRUE( node.run( GuildOperation::Accept, member, 0, kInvalidAccountID, guildID ) == SocialResult::Ok );
    }
    SW_ASSERT_TRUE( node.run( GuildOperation::Invite, 1, 0, 999 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 999, 0, kInvalidAccountID, guildID ) == SocialResult::GuildFull );

    const uint64 hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( node.run( GuildOperation::SetNotice, 1, 0, kInvalidAccountID, 0, "hello" ) == SocialResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
}

SW_TEST_CASE( GuildServiceTest, StorageDepositWithdrawAndDissolveGuard )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountID, 0, "Vault" ) == SocialResult::Ok );
    const uint64       guildID = node.getLastGuildID();
    const LedgerHolder storage = LedgerHolder::makeEscrow( "guild", ServiceKeyUtil::makeHex64( guildID ) );
    SW_ASSERT_TRUE( node.run( GuildOperation::Invite, 1, 0, 2 ) == SocialResult::Ok );
    SW_ASSERT_TRUE( node.run( GuildOperation::Accept, 2, 0, kInvalidAccountID, guildID ) == SocialResult::Ok );
    grant( database, 2, "cur.gold", 100 );

    SW_EXPECT_TRUE( node.runStorage( GuildOperation::Deposit, 2, "cur.gold", 40, 0 ) == SocialResult::Invalid ); // 분개 키 없음
    SW_EXPECT_TRUE( node.runStorage( GuildOperation::Deposit, 2, "cur.gold", 0, 7 ) == SocialResult::Invalid );  // 양 0
    SW_EXPECT_TRUE( node.runStorage( GuildOperation::Deposit, 2, "cur.gold", 500, 7 ) == SocialResult::InsufficientFunds );
    SW_ASSERT_TRUE( node.runStorage( GuildOperation::Deposit, 2, "cur.gold", 40, 8 ) == SocialResult::Ok );
    SW_ASSERT_EQUAL( node._listCompletion.back()._listStorage.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listStorage[0]._amount, int64( 40 ) );
    SW_ASSERT_TRUE( node.runStorage( GuildOperation::Deposit, 2, "cur.gold", 40, 8 ) == SocialResult::Ok ); // 같은 키 — 다시 적용하지 않는다
    SW_EXPECT_EQUAL( readAmount( database, LedgerHolder::makeAccount( 2 ), "cur.gold" ), int64( 60 ) );
    SW_EXPECT_EQUAL( readAmount( database, storage, "cur.gold" ), int64( 40 ) );

    SW_EXPECT_TRUE( node.runStorage( GuildOperation::Withdraw, 2, "cur.gold", 10, 9 ) == SocialResult::NotAllowed ); // 회원은 못 꺼낸다
    SW_EXPECT_TRUE( node.runStorage( GuildOperation::Withdraw, 1, "cur.gold", 50, 9 ) == SocialResult::InsufficientFunds );
    SW_ASSERT_TRUE( node.runStorage( GuildOperation::Withdraw, 1, "cur.gold", 15, 10 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( readAmount( database, LedgerHolder::makeAccount( 1 ), "cur.gold" ), int64( 15 ) );

    SW_ASSERT_TRUE( node.runStorage( GuildOperation::ListStorage, 2, "", 0, 0 ) == SocialResult::Ok );
    SW_ASSERT_EQUAL( node._listCompletion.back()._listStorage.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion.back()._listStorage[0]._assetID == "cur.gold" );
    SW_EXPECT_EQUAL( node._listCompletion.back()._listStorage[0]._amount, int64( 25 ) );

    SW_ASSERT_TRUE( node.run( GuildOperation::Leave, 2, 0 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.runStorage( GuildOperation::Deposit, 2, "cur.gold", 1, 11 ) == SocialResult::NotInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Leave, 1, 0 ) == SocialResult::StorageNotEmpty ); // 혼자 남은 길드장 — 창고가 남아 해산 못 함
    SW_ASSERT_TRUE( node.runStorage( GuildOperation::Withdraw, 1, "cur.gold", 25, 12 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Leave, 1, 0 ) == SocialResult::Ok );
}
