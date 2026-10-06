// 길드 — 만들기(이름 유일 · 대소문자 무시 · 한 계정 한 길드 · 이름 규칙), 초대 → 수락(정원 · 만료), 역할(임원만 초대, 길드장 넘기기), 내보내기,
// 떠나기(길드장은 혼자일 때 해산 — 이름이 다시 비게), 사건(가입 · 떠남 · 로그인한 회원) · 알림, 저장소 실패는 아무것도 쓰지 않음.
#include "pch.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Social/GuildService.h"

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
            _service.setEventDelegate( GuildEventDelegate::create<&GuildNode::onEvent>( this ) );
        }

        void onEvent( const GuildEvent& event ) { _listEvent.push_back( event ); }

        SocialResult run( GuildOperation operation, AccountId accountId, int64 nowMs, AccountId targetId = kInvalidAccountId, uint64 guildId = 0,
                          string_view text = string_view{}, GuildRole role = GuildRole::Member )
        {
            GuildRequest request;
            request._operation = operation;
            request._accountId = accountId;
            request._targetId  = targetId;
            request._guildId   = guildId;
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

        uint64 getLastGuildId() const { return _listCompletion.back()._info._guildId; }
    };
} // namespace

SW_TEST_CASE( GuildServiceTest, CreateIsUniqueByNameAndOnePerAccount )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountId, 0, "Knights" ) == SocialResult::Ok );
    const uint64 guildId = node.getLastGuildId();
    SW_EXPECT_NOT_EQUAL( guildId, uint64( 0 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountId, 0, "KNIGHTS" ) == SocialResult::GuildNameTaken );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountId, 0, "Other" ) == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountId, 0, "" ) == SocialResult::Invalid );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountId, 0, " Lead" ) == SocialResult::Invalid );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountId, 0, "bad\x01name" ) == SocialResult::Invalid );
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 2, 0, kInvalidAccountId, 0, "\xEA\xB8\xB0\xEC\x82\xAC\xEB\x8B\xA8" ) == SocialResult::Ok ); // "기사단"
    SW_EXPECT_NOT_EQUAL( node.getLastGuildId(), guildId );
    SW_ASSERT_EQUAL( node._listEvent.size(), size_t( 2 ) );
    SW_EXPECT_TRUE( node._listEvent[0]._kind == GuildEvent::Kind::Joined && node._listEvent[0]._guildId == guildId );

    node._service.attachAccount( 1, 10 ); // 로그인한 회원 — 완료 없이 사건만
    (void)node._store.pollCompletions();
    vector<GuildCompletion> listCompletion;
    node._service.drainCompletions( listCompletion );
    SW_EXPECT_EQUAL( listCompletion.size(), size_t( 0 ) );
    SW_EXPECT_TRUE( node._listEvent.back()._kind == GuildEvent::Kind::MemberAttached && node._listEvent.back()._guildId == guildId );
}

SW_TEST_CASE( GuildServiceTest, InviteAcceptRolesKickAndLeave )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountId, 0, "Knights" ) == SocialResult::Ok );
    const uint64 guildId = node.getLastGuildId();

    SW_EXPECT_TRUE( node.run( GuildOperation::Invite, 1, 0, 2 ) == SocialResult::Ok );
    SW_ASSERT_TRUE( node._listNotification.empty() == false );
    SW_EXPECT_TRUE( node._listNotification.back()._kind == SocialNotificationKind::GuildInvited && node._listNotification.back()._recipientId == AccountId( 2 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 2, GuildLimit::kInviteLifetimeMs, kInvalidAccountId, guildId ) == SocialResult::InviteExpired );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 2, 100, kInvalidAccountId, guildId ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 2, 100, kInvalidAccountId, guildId ) == SocialResult::AlreadyInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Invite, 2, 200, 3 ) == SocialResult::NotAllowed ); // 회원은 초대 못 함
    SW_EXPECT_TRUE( node.run( GuildOperation::SetNotice, 2, 200, kInvalidAccountId, 0, "hi" ) == SocialResult::NotAllowed );

    node._listNotification.clear();
    SW_EXPECT_TRUE( node.run( GuildOperation::SetRole, 1, 300, 2, 0, string_view{}, GuildRole::Master ) == SocialResult::Ok ); // 길드장 넘기기
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 1 ) );                                                             // 바꾼 이 말고 회원 하나에게
    SW_EXPECT_TRUE( node._listNotification[0]._kind == SocialNotificationKind::GuildChanged && node._listNotification[0]._recipientId == AccountId( 2 ) );
    SW_EXPECT_TRUE( node.run( GuildOperation::Leave, 2, 400 ) == SocialResult::NotAllowed );   // 새 길드장은 회원이 있어 못 떠남
    SW_EXPECT_TRUE( node.run( GuildOperation::Kick, 1, 450, 2 ) == SocialResult::NotAllowed ); // 임원이 길드장을 내보내지 못함
    SW_EXPECT_TRUE( node.run( GuildOperation::Kick, 2, 500, 1 ) == SocialResult::Ok );         // 길드장이 임원을 내보냄
    SW_EXPECT_TRUE( node._listEvent.back()._kind == GuildEvent::Kind::Left && node._listEvent.back()._accountId == AccountId( 1 ) );
    SW_EXPECT_TRUE( node._listNotification.back()._recipientId == AccountId( 1 ) ); // 내보내진 사람도 안다

    SW_EXPECT_TRUE( node.run( GuildOperation::Get, 2, 600 ) == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._memberCount, 1 );
    SW_EXPECT_EQUAL( node._listCompletion.back()._info._masterId, AccountId( 2 ) );
    SW_ASSERT_EQUAL( node._listCompletion.back()._info._listMember.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion.back()._info._listMember[0]._role == GuildRole::Master );
    SW_EXPECT_TRUE( node.run( GuildOperation::Get, 1, 600 ) == SocialResult::NotInGuild );
    SW_EXPECT_TRUE( node.run( GuildOperation::Leave, 2, 700 ) == SocialResult::Ok );                                    // 혼자 — 해산
    SW_EXPECT_TRUE( node.run( GuildOperation::Create, 3, 800, kInvalidAccountId, 0, "knights" ) == SocialResult::Ok );  // 이름이 다시 비었다
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 4, 800, kInvalidAccountId, guildId ) == SocialResult::NotFound ); // 초대 없음
}

SW_TEST_CASE( GuildServiceTest, FullGuildAndStoreFailure )
{
    MemoryServiceDatabase database;
    GuildNode             node( &database );
    SW_ASSERT_TRUE( node.run( GuildOperation::Create, 1, 0, kInvalidAccountId, 0, "Big" ) == SocialResult::Ok );
    const uint64 guildId = node.getLastGuildId();
    for ( AccountId member = 2; member <= static_cast<AccountId>( GuildLimit::kMaxMember ); ++member )
    {
        SW_ASSERT_TRUE( node.run( GuildOperation::Invite, 1, 0, member ) == SocialResult::Ok );
        SW_ASSERT_TRUE( node.run( GuildOperation::Accept, member, 0, kInvalidAccountId, guildId ) == SocialResult::Ok );
    }
    SW_ASSERT_TRUE( node.run( GuildOperation::Invite, 1, 0, 999 ) == SocialResult::Ok );
    SW_EXPECT_TRUE( node.run( GuildOperation::Accept, 999, 0, kInvalidAccountId, guildId ) == SocialResult::GuildFull );

    const uint64 hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( node.run( GuildOperation::SetNotice, 1, 0, kInvalidAccountId, 0, "hello" ) == SocialResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
}
