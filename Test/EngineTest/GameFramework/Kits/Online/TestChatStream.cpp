// 채팅 끝단(루프백) — 두 계정이 채널에 들어가 말하면 상대 · 자기에게 알림이 오고 되울림이 답에 실리며, 업무 결과(들어가지 않은 채널)는 답 몸으로, 로그인 없는 연결은
// 공통 오류로, 끊긴 계정은 채널에서 빠진다. 서버 둘: 귓속말이 접속 상태 → 버스로 다른 서버 계정에게 가고, 기록은 두 사람 모두 읽으며, GM 이 다른 서버에서 건 채팅 금지가
// 버스로 와서 묵힌 제재를 바로 버린다.
#include "pch.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Kits/Online/Chat/ChatClient.h"
#include "GameFramework/Kits/Online/Chat/ChatProtocol.h"
#include "GameFramework/Kits/Online/Server/Chat/ChatServer.h"
#include "GameFramework/Kits/Online/Server/Chat/ChatService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 이 서버 호스트에 로그인해 붙은 계정 — 이름은 시험이 적는다. */
    class HostAccountDirectory final : public IAccountDirectory
    {
    public:
        unordered_map<AccountId, string> _mapName{};
        const OnlineServiceHost*         _pHost{ nullptr };

        bool findIdentity( AccountId accountId, AccountIdentity& outIdentity ) const override
        {
            const auto nameIt = _mapName.find( accountId );
            if ( nameIt == _mapName.end() || isAccountOnline( accountId ) == false )
                return false;
            outIdentity._accountId   = accountId;
            outIdentity._displayName = nameIt->second;
            return true;
        }

        bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override
        {
            (void)displayName;
            (void)outIdentity;
            return false;
        }

        bool isAccountOnline( AccountId accountId ) const override
        {
            StreamConnectionHandle connection;
            return _pHost != nullptr && _pHost->findConnection( accountId, connection );
        }
    };

    /** @brief 서버 하나의 채팅(로직 + 바인딩) — `start` 가 서버에 올려 서버가 내려가기 전에 `stop` 한다. */
    struct ChatOnServer final : public test::IOnlineTestKit
    {
        HostAccountDirectory _directory;
        ChatService          _service;
        ChatServer           _binding;

        /** @brief 서버를 띄우기 전에 바인딩을 올리고, 띄운 뒤 로직을 호스트의 저장소 · 버스 · 접속 상태에 잇습니다. */
        void start( test::OnlineTestServer& server )
        {
            SW_EXPECT_TRUE( server._host.registerService( &_binding ) );
            server.start();
            server.addKit( this );
            _directory._pHost = &server._host;
            ChatServiceDependencies dependencies;
            dependencies._pStore     = &server._store;
            dependencies._pBus       = &server._bus;
            dependencies._pPresence  = &server._presence;
            dependencies._pDirectory = &_directory;
            dependencies._serverId   = server.getServerId();
            SW_EXPECT_TRUE( _service.initialize( dependencies, ChatSettings{} ) );
            _binding.initialize( &_service );
        }

        /** @brief 서버가 내려가기 전에 부른다(호스트 · 저장소 · 접속 상태가 살아 있다). 두 번 불려도 된다. */
        void stop() override
        {
            _binding.shutdown();
            _service.shutdown();
        }
    };

    struct ChatReplyRecorder
    {
        vector<ChatClientReply> _listReply{};

        void                      onReply( const ChatClientReply& reply ) { _listReply.push_back( reply ); }
        ChatClient::ReplyDelegate makeDelegate() { return ChatClient::ReplyDelegate::create<&ChatReplyRecorder::onReply>( this ); }
    };
} // namespace

SW_TEST_CASE( ChatStreamTest, ChannelChatBetweenTwoClientsWithEchoAndErrors )
{
    LoopbackStreamNetwork   network( 5u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    ChatOnServer            chat;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    chat.start( server );
    chat._directory._mapName[1] = "alice";
    chat._directory._mapName[2] = "bob";

    ChatClient              aliceChat;
    ChatClient              bobChat;
    ChatClient              guestChat;
    test::OnlineTestClients clients( network );
    const int32             alice = clients.connect( server._port, { &aliceChat } );
    const int32             bob   = clients.connect( server._port, { &bobChat } );
    const int32             guest = clients.connect( server._port, { &guestChat } );
    aliceChat.initialize( &clients.getClient( alice ) );
    bobChat.initialize( &clients.getClient( bob ) );
    guestChat.initialize( &clients.getClient( guest ) );
    test::tickAll( { &server }, clients, 0 );
    clients.login( alice, 1 );
    clients.login( bob, 2 );
    test::tickAll( { &server }, clients, 0 );

    ChatReplyRecorder aliceRecorder;
    ChatReplyRecorder bobRecorder;
    ChatReplyRecorder guestRecorder;
    (void)aliceChat.join( "world.kr", aliceRecorder.makeDelegate() );
    (void)bobChat.join( "world.kr", bobRecorder.makeDelegate() );
    test::tickAll( { &server }, clients, 10 );
    (void)aliceChat.send( "world.kr", "hello bob", aliceRecorder.makeDelegate() );
    test::tickAll( { &server }, clients, 20 );

    SW_ASSERT_EQUAL( aliceRecorder._listReply.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( aliceRecorder._listReply[0]._method, ChatMethod::kJoin );
    SW_EXPECT_EQUAL( aliceRecorder._listReply[1]._errorCode, OnlineError::kOk );
    SW_EXPECT_TRUE( aliceRecorder._listReply[1]._reply._result == ChatResult::Ok );
    SW_EXPECT_STREQ( aliceRecorder._listReply[1]._reply._message._text.c_str(), "hello bob" );
    vector<ChatMessage> listBobMessage;
    bobChat.drainMessages( listBobMessage );
    SW_ASSERT_EQUAL( listBobMessage.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( listBobMessage[0]._senderName.c_str(), "alice" );
    vector<ChatMessage> listAliceMessage;
    aliceChat.drainMessages( listAliceMessage );
    SW_EXPECT_EQUAL( listAliceMessage.size(), size_t( 1 ) ); // 보낸 이의 다른 창

    (void)bobChat.send( "custom.nowhere", "hi", bobRecorder.makeDelegate() );
    (void)guestChat.join( "world.kr", guestRecorder.makeDelegate() ); // 로그인 없음
    test::tickAll( { &server }, clients, 30 );
    SW_EXPECT_EQUAL( bobRecorder._listReply.back()._errorCode, OnlineError::kOk ); // 업무 결과는 답 몸으로
    SW_EXPECT_TRUE( bobRecorder._listReply.back()._reply._result == ChatResult::NotMember );
    SW_ASSERT_EQUAL( guestRecorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( guestRecorder._listReply[0]._errorCode, OnlineError::kUnauthenticated );
    SW_EXPECT_TRUE( guestRecorder._listReply[0]._reply._result == ChatResult::NotSignedIn );

    clients.drop( bob ); // 끊김 → 호스트 onAccountLeft → 채널에서 빠짐
    test::tickAll( { &server }, clients, 40 );
    SW_EXPECT_EQUAL( chat._service.getLocalMemberCount( "world.kr" ), 1 );
}

SW_TEST_CASE( ChatStreamTest, WhisperHistoryAndMuteAcrossTwoServers )
{
    LoopbackStreamNetwork   network( 6u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    ChatOnServer            firstChat;
    ChatOnServer            secondChat;
    test::OnlineTestServer  first( network, &database, &cacheDatabase, &hub, 1 );
    test::OnlineTestServer  second( network, &database, &cacheDatabase, &hub, 2 );
    firstChat.start( first );
    secondChat.start( second );
    firstChat._directory._mapName[1]  = "alice";
    secondChat._directory._mapName[2] = "bob";
    first._presence.setOnline( AccountIdentity{ "bob", 2, SW_FALSE }, second.getServerId() ); // 계정 서버(K14)가 쓴 접속 상태

    ChatClient              aliceChat;
    ChatClient              bobChat;
    test::OnlineTestClients clients( network );
    const int32             alice = clients.connect( first._port, { &aliceChat } );
    const int32             bob   = clients.connect( second._port, { &bobChat } );
    aliceChat.initialize( &clients.getClient( alice ) );
    bobChat.initialize( &clients.getClient( bob ) );
    test::tickAll( { &first, &second }, clients, 0 );
    clients.login( alice, 1 );
    clients.login( bob, 2 );
    test::tickAll( { &first, &second }, clients, 0 );

    ChatReplyRecorder aliceRecorder;
    ChatReplyRecorder bobRecorder;
    (void)aliceChat.whisper( 2, "hi bob", aliceRecorder.makeDelegate() );
    test::tickAll( { &first, &second }, clients, 10, 10 );
    SW_ASSERT_EQUAL( aliceRecorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( aliceRecorder._listReply[0]._reply._result == ChatResult::Ok );
    vector<ChatMessage> listBobMessage;
    bobChat.drainMessages( listBobMessage );
    SW_ASSERT_EQUAL( listBobMessage.size(), size_t( 1 ) ); // 접속 상태 → 버스 chat.server.<2> → 알림
    SW_EXPECT_STREQ( listBobMessage[0]._text.c_str(), "hi bob" );
    SW_EXPECT_EQUAL( listBobMessage[0]._senderId, AccountId( 1 ) );

    const string whisperKey = ChatChannelId::makeWhisper( 1, 2 );
    (void)aliceChat.requestHistory( whisperKey, "", 10, aliceRecorder.makeDelegate() );
    (void)bobChat.requestHistory( whisperKey, "", 10, bobRecorder.makeDelegate() ); // 다른 서버 — 같은 저장소
    test::tickAll( { &first, &second }, clients, 20, 10 );
    SW_ASSERT_EQUAL( aliceRecorder._listReply.size(), size_t( 2 ) );
    SW_ASSERT_EQUAL( aliceRecorder._listReply[1]._reply._listHistory.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( bobRecorder._listReply.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( bobRecorder._listReply[0]._reply._listHistory.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( bobRecorder._listReply[0]._reply._listHistory[0]._text.c_str(), "hi bob" );

    {
        ServiceSanctionState state; // GM 이 다른 서버(99)에서 alice 에게 채팅 금지를 걸었다
        state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] = 100000;
        ServiceTransaction transaction;
        ServiceSanction::stageWrite( transaction, 1, state );
        SW_ASSERT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
        LocalServerBus gmBus( &hub, 99 );
        BitWriter      body;
        body.writeVarUint( AccountId( 1 ) );
        gmBus.publish( ServiceSanctionBus::kChangedTopic, body.getBytes().data(), body.getByteCount() );
    }
    test::tickAll( { &first, &second }, clients, 30 ); // 첫 서버가 버스로 받아 묵힌 제재를 버린다(60 초 안이다)
    (void)aliceChat.whisper( 2, "can you hear me", aliceRecorder.makeDelegate() );
    test::tickAll( { &first, &second }, clients, 40, 10 );
    SW_ASSERT_EQUAL( aliceRecorder._listReply.size(), size_t( 3 ) );
    SW_EXPECT_TRUE( aliceRecorder._listReply[2]._reply._result == ChatResult::Muted );
    SW_EXPECT_EQUAL( aliceRecorder._listReply[2]._reply._retryAfterMs, int64( 100000 - 40 ) );
    listBobMessage.clear();
    bobChat.drainMessages( listBobMessage );
    SW_EXPECT_EQUAL( listBobMessage.size(), size_t( 0 ) );
}
