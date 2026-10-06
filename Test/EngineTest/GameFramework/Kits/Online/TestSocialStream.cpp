// 친구 · 길드 끝단(루프백) — 로그인 전 요청은 NotSignedIn, 신청 알림이 상대 연결에 오고 수락 뒤 목록 · 친구 접속 상태 알림, 길드 만들기 · 초대 알림 · 수락 · 조회,
// 막힌 사람의 신청은 성공처럼 보이지만 알림이 없다, 다른 서버에 붙은 계정에게 가는 알림은 접속 상태 창구(원격 알림)로 간다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Kits/Online/Server/Social/GuildService.h"
#include "GameFramework/Kits/Online/Server/Social/SocialServer.h"
#include "GameFramework/Kits/Online/Server/Social/SocialService.h"
#include "GameFramework/Kits/Online/Social/SocialClient.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct SocialRecorder
    {
        vector<SocialClientReply> _listReply{};

        void onReply( const SocialClientReply& reply ) { _listReply.push_back( reply ); }

        SocialReplyDelegate makeDelegate() { return SocialReplyDelegate::create<&SocialRecorder::onReply>( this ); }

        const SocialClientReply& getLast() const { return _listReply.back(); }
    };

    /** @brief 서버 하나의 친구 · 길드(로직 + 바인딩) — `start` 가 서버에 올려 서버가 내려가기 전에 `stop` 한다. */
    struct SocialOnServer final : public test::IOnlineTestKit
    {
        SocialService _socialService;
        GuildService  _guildService;
        SocialServer  _binding;

        SocialOnServer()
            : _socialService{}
            , _guildService{}
            , _binding{}
        {
        }

        /** @brief 서버가 내려가기 전에 부른다(호스트 · 저장소 · 접속 상태가 살아 있다). 두 번 불려도 된다. */
        void stop() override
        {
            _binding.shutdown();
            _guildService.shutdown();
            _socialService.shutdown();
        }

        void start( test::OnlineTestServer& server )
        {
            SW_EXPECT_TRUE( server._host.registerService( &_binding ) );
            server.start();
            server.addKit( this );
            SocialServiceDependencies dependencies;
            dependencies._pStore  = &server._store;
            dependencies._pRouter = server._host.getEphemeralRouter();
            dependencies._pBus    = &server._bus;
            _socialService.initialize( dependencies );
            _guildService.initialize( &server._store );
            _binding.initialize( &_socialService, &_guildService, &server._presence );
        }
    };

    struct SocialStreamTestInternal
    {
        static int32 countKind( const vector<SocialNotification>& listNotification, SocialNotificationKind kind )
        {
            int32 count = 0;
            for ( const SocialNotification& notification : listNotification )
                count += notification._kind == kind ? 1 : 0;
            return count;
        }
    };
} // namespace

SW_TEST_CASE( SocialStreamTest, FriendRequestNotificationAndGuildRoundTrip )
{
    using Internal = SocialStreamTestInternal;
    LoopbackStreamNetwork   network( 7u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    SocialOnServer          social;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    social.start( server );

    SocialClient            clientOfFirst;
    SocialClient            clientOfSecond;
    test::OnlineTestClients clients( network );
    const int32             firstIndex  = clients.connect( server._port, { &clientOfFirst } );
    const int32             secondIndex = clients.connect( server._port, { &clientOfSecond } );
    clientOfFirst.initialize( &clients.getClient( firstIndex ) );
    clientOfSecond.initialize( &clients.getClient( secondIndex ) );
    test::tickAll( { &server }, clients, 0 );

    SocialRecorder recorder;
    (void)clientOfFirst.listLinks( recorder.makeDelegate() ); // 로그인 전
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_EQUAL( recorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder.getLast()._errorCode, OnlineError::kUnauthenticated );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == SocialResult::NotSignedIn );

    clients.login( firstIndex, 1 );
    clients.login( secondIndex, 2 );
    test::tickAll( { &server }, clients, 0 );
    (void)clientOfFirst.listLinks( recorder.makeDelegate() ); // 관계를 서버 메모리에 올린다
    (void)clientOfSecond.listLinks( recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );

    // 1) 신청 → 상대 연결에 알림
    (void)clientOfFirst.requestFriend( 2, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 10 );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == SocialResult::Ok );
    vector<SocialNotification> listNotificationOfSecond;
    clientOfSecond.drainNotifications( listNotificationOfSecond );
    SW_ASSERT_EQUAL( listNotificationOfSecond.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listNotificationOfSecond[0]._kind == SocialNotificationKind::FriendRequested );
    SW_EXPECT_EQUAL( listNotificationOfSecond[0]._otherId, AccountId( 1 ) );

    // 2) 수락 → 신청한 쪽에 FriendAdded, 목록에 친구 하나, 접속 상태가 친구에게 알려진다
    (void)clientOfSecond.respondFriend( 1, true, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 20 );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == SocialResult::Ok );
    vector<SocialNotification> listNotificationOfFirst;
    clientOfFirst.drainNotifications( listNotificationOfFirst );
    SW_EXPECT_EQUAL( Internal::countKind( listNotificationOfFirst, SocialNotificationKind::FriendAdded ), 1 );
    listNotificationOfSecond.clear();
    clientOfSecond.drainNotifications( listNotificationOfSecond );
    SW_EXPECT_EQUAL( Internal::countKind( listNotificationOfSecond, SocialNotificationKind::FriendAdded ), 1 ); // 수락한 쪽도 안다
    (void)clientOfFirst.listLinks( recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 30 );
    SW_ASSERT_EQUAL( recorder.getLast()._reply._listLink.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder.getLast()._reply._listLink[0]._state == SocialLinkState::Friend );
    (void)clientOfSecond.setPresence( SocialPresenceStatus::InGame, "arena", recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 40 );
    listNotificationOfFirst.clear();
    clientOfFirst.drainNotifications( listNotificationOfFirst );
    SW_ASSERT_EQUAL( listNotificationOfFirst.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listNotificationOfFirst[0]._kind == SocialNotificationKind::PresenceChanged );
    SW_EXPECT_STREQ( listNotificationOfFirst[0]._presence._activity.c_str(), "arena" );
    (void)clientOfFirst.queryFriendPresence( recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 40 );
    SW_ASSERT_EQUAL( recorder.getLast()._reply._listPresence.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder.getLast()._reply._listPresence[0]._status == SocialPresenceStatus::InGame );

    // 3) 길드 — 만들기 · 초대(알림) · 수락 · 조회
    (void)clientOfFirst.createGuild( "Knights", recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 50 );
    SW_ASSERT_TRUE( recorder.getLast()._reply._result == SocialResult::Ok );
    const uint64 guildId = recorder.getLast()._reply._guild._guildId;
    SW_EXPECT_NOT_EQUAL( guildId, uint64( 0 ) );
    (void)clientOfFirst.inviteToGuild( 2, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 60 );
    listNotificationOfSecond.clear();
    clientOfSecond.drainNotifications( listNotificationOfSecond );
    SW_ASSERT_EQUAL( listNotificationOfSecond.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listNotificationOfSecond[0]._kind == SocialNotificationKind::GuildInvited );
    SW_EXPECT_EQUAL( listNotificationOfSecond[0]._guildId, guildId );
    (void)clientOfSecond.acceptGuildInvite( guildId, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 70 );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == SocialResult::Ok );
    (void)clientOfFirst.requestGuild( recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 80 );
    SW_EXPECT_STREQ( recorder.getLast()._reply._guild._name.c_str(), "Knights" );
    SW_EXPECT_EQUAL( recorder.getLast()._reply._guild._listMember.size(), size_t( 2 ) );

    // 4) 막힌 사람의 신청 — 응답은 Ok, 막은 쪽에 새 알림 없음
    (void)clientOfSecond.block( 1, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 90 );
    listNotificationOfSecond.clear();
    clientOfSecond.drainNotifications( listNotificationOfSecond );
    (void)clientOfFirst.requestFriend( 2, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 100 );
    SW_EXPECT_EQUAL( recorder.getLast()._errorCode, OnlineError::kOk );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == SocialResult::Ok );
    listNotificationOfSecond.clear();
    clientOfSecond.drainNotifications( listNotificationOfSecond );
    SW_EXPECT_EQUAL( Internal::countKind( listNotificationOfSecond, SocialNotificationKind::FriendRequested ), 0 );

    // 5) 다른 서버에 붙은 계정에게 신청 — 알림은 접속 상태 창구로
    server._presence.setOnline( AccountIdentity{ string( "carol" ), 3, SW_FALSE }, 2 );
    (void)clientOfFirst.requestFriend( 3, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 110 );
    SW_EXPECT_TRUE( recorder.getLast()._reply._result == SocialResult::Ok );
    SW_ASSERT_EQUAL( server._presence._listRemotePush.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( server._presence._listRemotePush[0]._accountId, AccountId( 3 ) );
    SW_EXPECT_EQUAL( server._presence._listRemotePush[0]._kind, SocialMethod::kPushNotification );
}
