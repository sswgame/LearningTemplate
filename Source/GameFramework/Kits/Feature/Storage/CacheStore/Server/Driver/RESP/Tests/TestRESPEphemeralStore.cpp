#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/RESP/RESPEphemeralStore.h"

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

using namespace sw;

namespace
{
    /** @brief 받은 명령을 기억하고, 맡긴 답을 차례로 돌려주는 RESP 서버입니다. 답이 비면 말없이 기다립니다. */
    class FakeRESPServer final : public IStreamHandler
    {
    public:
        static constexpr uint16 kPort = 6379;

        struct Peer
        {
            StreamConnectionHandle _handle{};
            RESPParser             _parser{};
        };

        unique_ptr<IStreamTransport> _transport{};
        vector<unique_ptr<Peer>>     _listPeer{};
        vector<vector<string>>       _listCommand{};
        deque<string>                _listReply{};
        StreamConnectionHandle       _lastHandle{};
        int32                        _openedCount{ 0 };

        void start( LoopbackStreamNetwork& network )
        {
            _transport = network.createTransport();
            StreamTransportConfig settings;
            settings._ioThreadCount = 0;
            SW_REQUIRE_TRUE( _transport->initialize( this, settings ) );
            SW_REQUIRE_TRUE( _transport->listen( NetAddress::makeLoopback( kPort ) ) );
        }

        void pushAll( std::initializer_list<const utf8*> listReply )
        {
            for ( const utf8* pReply : listReply )
            {
                _listReply.emplace_back( pReply );
            }
        }

        void send( StreamConnectionHandle handle, string_view text ) { (void)_transport->send( handle, reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) ); }

        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            unique_ptr<Peer> peer = make_unique<Peer>();
            peer->_handle         = handle;
            _listPeer.push_back( std::move( peer ) );
            _lastHandle = handle;
            ++_openedCount;
        }

        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            for ( const unique_ptr<Peer>& peer : _listPeer )
            {
                if ( peer->_handle != handle )
                    continue;
                peer->_parser.append( pData, static_cast<size_t>( size ) );
                RESPValue value;
                while ( peer->_parser.next( value ) == RESPParseResult::Complete )
                {
                    vector<string>& listArgument = _listCommand.emplace_back();
                    for ( const RESPValue& element : value._listElement )
                    {
                        listArgument.emplace_back( element.getText() );
                    }
                    if ( _listReply.empty() == false )
                    {
                        const string reply = _listReply.front();
                        _listReply.pop_front();
                        send( handle, reply );
                    }
                }
            }
        }

        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)handle;
            (void)reason;
        }
    };

    /** @brief 저장소와 가짜 서버를 한 망에 잇습니다. 접두어는 `g:` 입니다. */
    struct RESPStoreTest
    {
        ManualClock           _clock;
        LoopbackStreamNetwork _network;
        FakeRESPServer        _server;
        RESPEphemeralStore    _store;

        RESPStoreTest()
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _network{}
            , _server{}
            , _store{ _clock }
        {
            _server.start( _network );
        }

        void start( RESPStoreConfig config = RESPStoreConfig{} )
        {
            config._keyPrefix = "g:";
            config._address   = NetAddress::makeLoopback( FakeRESPServer::kPort );
            StreamTransportConfig settings;
            settings._ioThreadCount = 0;
            string error;
            SW_REQUIRE_TRUE( _store.initialize( _network.createTransport(), settings, config, nullptr ) );
        }

        /** @brief 양쪽을 충분히 돌리고 나온 답을 모읍니다. */
        vector<EphemeralReply> poll()
        {
            vector<EphemeralReply> listReply;
            for ( int32 round = 0; round < 16; ++round )
            {
                (void)_server._transport->pollIO( 0 );
                (void)_store.pollReplies( listReply );
            }
            return listReply;
        }

        vector<EphemeralMessage> collectMessages()
        {
            vector<EphemeralMessage> listMessage;
            for ( int32 round = 0; round < 16; ++round )
            {
                (void)_server._transport->pollIO( 0 );
                (void)_store.pollMessages( listMessage );
            }
            return listMessage;
        }

        /** @brief 요청 하나를 맡기고 답을 받습니다. */
        EphemeralReply run( const EphemeralRequest& request )
        {
            (void)_store.submit( request );
            vector<EphemeralReply> listReply = poll();
            SW_EXPECT_EQUAL( listReply.size(), size_t{ 1 } );
            return listReply.empty() ? EphemeralReply{} : listReply.back();
        }

        [[nodiscard]] static string join( const vector<string>& listArgument )
        {
            string text;
            for ( const string& argument : listArgument )
            {
                text += text.empty() ? "" : " ";
                text += argument;
            }
            return text;
        }

        /** @brief 받은 명령들을 ` | ` 로 이은 글을 돌려주고 비웁니다. */
        [[nodiscard]] string takeCommands()
        {
            string text;
            for ( const vector<string>& command : _server._listCommand )
            {
                text += text.empty() ? "" : " | ";
                text += join( command );
            }
            _server._listCommand.clear();
            return text;
        }

        [[nodiscard]] static vector<uint8> bytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }
    };
} // namespace

// 값 읽기: 있음 · 없음 · 다른 종류의 키 · 이상한 답.
SW_TEST_CASE_F( RESPStoreTest, GetMapsReplies )
{
    start();
    _server.pushAll( { "$1\r\nv\r\n", "$-1\r\n", "-WRONGTYPE x\r\n", ":1\r\n" } );
    const EphemeralReply found = run( EphemeralRequest::makeGet( "k" ) );
    SW_EXPECT_TRUE( found._result == EphemeralResult::Ok );
    SW_EXPECT_TRUE( found._value == bytes( "v" ) );
    SW_EXPECT_EQUAL( takeCommands(), string( "GET g:k" ) ); // 접두어가 붙는다

    SW_EXPECT_TRUE( run( EphemeralRequest::makeGet( "k" ) )._result == EphemeralResult::NotFound );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeGet( "k" ) )._result == EphemeralResult::NotFound ); // 정렬 집합 키는 값으로는 없는 것
    SW_EXPECT_TRUE( run( EphemeralRequest::makeGet( "k" ) )._result == EphemeralResult::Invalid );
}

SW_TEST_CASE_F( RESPStoreTest, SetCarriesOptionsAndMapsReplies )
{
    start();
    _server.pushAll( { "+OK\r\n", "$-1\r\n", "+OK\r\n", "-ERR\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeSet( "k", bytes( "v" ), 500, EphemeralCondition::IfAbsent ) )._result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( takeCommands(), string( "SET g:k v PX 500 NX" ) );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeSet( "k", bytes( "v" ), 0, EphemeralCondition::IfPresent ) )._result == EphemeralResult::Conflict );
    SW_EXPECT_EQUAL( takeCommands(), string( "SET g:k v XX" ) );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeSet( "k", bytes( "v" ), 0 ) )._result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( takeCommands(), string( "SET g:k v" ) );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeSet( "k", bytes( "v" ), 0 ) )._result == EphemeralResult::Invalid );
}

SW_TEST_CASE_F( RESPStoreTest, EraseAndExpire )
{
    start();
    _server.pushAll( { ":1\r\n", ":0\r\n", ":1\r\n", ":1\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeErase( "k" ) )._result == EphemeralResult::Ok );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeErase( "k" ) )._result == EphemeralResult::NotFound );
    SW_EXPECT_EQUAL( takeCommands(), string( "DEL g:k | DEL g:k" ) );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeExpire( "k", 300 ) )._result == EphemeralResult::Ok );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeExpire( "k", 0 ) )._result == EphemeralResult::Ok ); // 시간 0 은 지우기다
    SW_EXPECT_EQUAL( takeCommands(), string( "PEXPIRE g:k 300 | DEL g:k" ) );
}

// 비교 후 쓰기: 값이 맞으면 트랜잭션, 다르면 UNWATCH 와 충돌, 그 사이 바뀌면 EXEC 널로 충돌이다.
SW_TEST_CASE_F( RESPStoreTest, CompareAndSetUsesWatchAndExec )
{
    start();
    _server.pushAll( { "+OK\r\n", "$1\r\na\r\n", "+OK\r\n", "+QUEUED\r\n", "*1\r\n+OK\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeCompareAndSet( "k", bytes( "a" ), bytes( "b" ), 200 ) )._result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( takeCommands(), string( "WATCH g:k | GET g:k | MULTI | SET g:k b PX 200 | EXEC" ) );

    _server.pushAll( { "+OK\r\n", "$1\r\nz\r\n", "+OK\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeCompareAndSet( "k", bytes( "a" ), bytes( "b" ), 0 ) )._result == EphemeralResult::Conflict );
    SW_EXPECT_EQUAL( takeCommands(), string( "WATCH g:k | GET g:k | UNWATCH" ) );

    _server.pushAll( { "+OK\r\n", "$1\r\na\r\n", "+OK\r\n", "+QUEUED\r\n", "*-1\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeCompareAndErase( "k", bytes( "a" ) ) )._result == EphemeralResult::Conflict );
    SW_EXPECT_EQUAL( takeCommands(), string( "WATCH g:k | GET g:k | MULTI | DEL g:k | EXEC" ) );

    _server.pushAll( { "+OK\r\n", "$-1\r\n", "+OK\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeCompareAndErase( "k", bytes( "a" ) ) )._result == EphemeralResult::Conflict ); // 없는 키
}

// WATCH 사이에는 다른 명령이 끼지 않는다 - 뒤 요청은 GET 답을 본 뒤에 나가고, 답은 맡긴 순서다.
SW_TEST_CASE_F( RESPStoreTest, RequestsWaitBehindACompare )
{
    start();
    const uint64 compareID = _store.submit( EphemeralRequest::makeCompareAndSet( "k", bytes( "a" ), bytes( "b" ), 0 ) );
    const uint64 getID     = _store.submit( EphemeralRequest::makeGet( "later" ) );
    SW_EXPECT_TRUE( poll().empty() );
    SW_EXPECT_EQUAL( takeCommands(), string( "WATCH g:k | GET g:k" ) );

    // 서버가 앞의 두 답을 이제 준다 - 뒤 명령의 답도 맡겨 둔다.
    _server.pushAll( { "+OK\r\n", "+QUEUED\r\n", "*1\r\n+OK\r\n", "$1\r\nv\r\n" } );
    _server.send( _server._lastHandle, "+OK\r\n$1\r\na\r\n" );
    const vector<EphemeralReply> listReply = poll();
    SW_EXPECT_EQUAL( takeCommands(), string( "MULTI | SET g:k b | EXEC | GET g:later" ) );
    SW_REQUIRE_EQUAL( listReply.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( listReply[0]._requestID, compareID );
    SW_EXPECT_EQUAL( listReply[1]._requestID, getID );
    SW_EXPECT_TRUE( listReply[1]._value == bytes( "v" ) );
}

// 더하기: 만료 없으면 INCRBY 하나, 있으면 새 키에만 만료를 거는 트랜잭션이다.
SW_TEST_CASE_F( RESPStoreTest, IncrementWithAndWithoutExpiry )
{
    start();
    _server.pushAll( { ":5\r\n", "-ERR not int\r\n" } );
    EphemeralReply reply = run( EphemeralRequest::makeIncrement( "k", 2, 0 ) );
    SW_EXPECT_EQUAL( reply._integer, int64{ 5 } );
    SW_EXPECT_EQUAL( takeCommands(), string( "INCRBY g:k 2" ) );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeIncrement( "k", 2, 0 ) )._result == EphemeralResult::Invalid );
    (void)takeCommands();

    _server.pushAll( { "+OK\r\n", "+QUEUED\r\n", "+QUEUED\r\n", "*2\r\n$-1\r\n:9\r\n" } );
    reply = run( EphemeralRequest::makeIncrement( "k", 3, 700 ) );
    SW_EXPECT_TRUE( reply._result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( reply._integer, int64{ 9 } );
    SW_EXPECT_EQUAL( takeCommands(), string( "MULTI | SET g:k 0 PX 700 NX | INCRBY g:k 3 | EXEC" ) );

    _server.pushAll( { "+OK\r\n", "+QUEUED\r\n", "+QUEUED\r\n", "*2\r\n+OK\r\n-ERR range\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeIncrement( "k", 3, 700 ) )._result == EphemeralResult::Invalid );
}

// 점수: 설정 · 더하기(실수 글로 온 점수) · 순위 · 범위 · 지우기.
SW_TEST_CASE_F( RESPStoreTest, ScoreOperations )
{
    start();
    _server.pushAll( { ":1\r\n", "-WRONGTYPE\r\n", "$4\r\n12.0\r\n", "$3\r\n2.5\r\n", ":0\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreSet( "z", "m", 10 ) )._result == EphemeralResult::Ok );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreSet( "z", "m", 10 ) )._result == EphemeralResult::Invalid );
    const EphemeralReply added = run( EphemeralRequest::makeScoreAdd( "z", "m", 2 ) );
    SW_EXPECT_EQUAL( added._score, int64{ 12 } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreAdd( "z", "m", 2 ) )._result == EphemeralResult::Invalid ); // 소수 점수
    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreRemove( "z", "m" ) )._result == EphemeralResult::NotFound );
    SW_EXPECT_EQUAL( takeCommands(), string( "ZADD g:z 10 m | ZADD g:z 10 m | ZINCRBY g:z 2 m | ZINCRBY g:z 2 m | ZREM g:z m" ) );

    _server.pushAll( { ":3\r\n", "$2\r\n20\r\n", "$-1\r\n", "$-1\r\n" } );
    const EphemeralReply rank = run( EphemeralRequest::makeScoreRank( "z", "m" ) );
    SW_EXPECT_EQUAL( rank._integer, int64{ 3 } );
    SW_EXPECT_EQUAL( rank._score, int64{ 20 } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreRank( "z", "m" ) )._result == EphemeralResult::NotFound );
    SW_EXPECT_EQUAL( takeCommands(), string( "ZREVRANK g:z m | ZSCORE g:z m | ZREVRANK g:z m | ZSCORE g:z m" ) );

    _server.pushAll( { "*4\r\n$1\r\na\r\n$1\r\n9\r\n$1\r\nb\r\n$1\r\n8\r\n", "*1\r\n$1\r\na\r\n" } );
    const EphemeralReply range = run( EphemeralRequest::makeScoreRange( "z", 2, 2 ) );
    SW_REQUIRE_EQUAL( range._listMember.size(), size_t{ 2 } );
    SW_EXPECT_EQUAL( range._listMember[1]._member, string( "b" ) );
    SW_EXPECT_EQUAL( range._listMember[1]._score, int64{ 8 } );
    SW_EXPECT_EQUAL( takeCommands(), string( "ZREVRANGE g:z 2 3 WITHSCORES" ) );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreRange( "z", 0, 1 ) )._result == EphemeralResult::Invalid );
    (void)takeCommands();

    SW_EXPECT_TRUE( run( EphemeralRequest::makeScoreRange( "z", 0, 0 ) )._result == EphemeralResult::Ok ); // 0 개는 묻지 않는다
    SW_EXPECT_EQUAL( takeCommands(), string() );
}

// 결함: 범위 답의 점수 하나가 깨지면 오류인데 앞 원소들을 담은 채 돌려줬다.
SW_TEST_CASE_F( RESPStoreTest, InvalidRangeReturnsNoMembers )
{
    start();
    _server.pushAll( { "*4\r\n$1\r\na\r\n$1\r\n9\r\n$1\r\nb\r\n$3\r\n2.5\r\n" } );
    const EphemeralReply range = run( EphemeralRequest::makeScoreRange( "z", 0, 2 ) );
    SW_EXPECT_TRUE( range._result == EphemeralResult::Invalid );
    SW_EXPECT_TRUE( range._listMember.empty() );
}

SW_TEST_CASE_F( RESPStoreTest, MalformedRequestIsNotSent )
{
    start();
    SW_EXPECT_TRUE( run( EphemeralRequest::makeGet( "Bad Key" ) )._result == EphemeralResult::Invalid );
    SW_EXPECT_EQUAL( takeCommands(), string() );
}

// 답 시한을 넘기면 기다리던 요청이 실패하고, 다음 요청은 새 연결로 간다.
SW_TEST_CASE_F( RESPStoreTest, TimeoutFailsWaitingRequestsAndReconnects )
{
    start();
    SW_EXPECT_LOG( "RespConnection.RESPConnectionAborted" );
    (void)_store.submit( EphemeralRequest::makeGet( "a" ) );
    (void)_store.submit( EphemeralRequest::makeGet( "b" ) );
    SW_EXPECT_TRUE( poll().empty() );
    _clock.advanceMilliseconds( 2001 );
    const vector<EphemeralReply> listReply = poll();
    SW_REQUIRE_EQUAL( listReply.size(), size_t{ 2 } );
    SW_EXPECT_TRUE( listReply[0]._result == EphemeralResult::Unavailable );
    SW_EXPECT_TRUE( listReply[1]._result == EphemeralResult::Unavailable );
    SW_EXPECT_EQUAL( _server._openedCount, 1 );

    _clock.advanceMilliseconds( 6000 );
    _server.pushAll( { "$1\r\nv\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeGet( "c" ) )._result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( _server._openedCount, 2 );
}

// 서버가 연결을 닫으면 기다리던 요청이 실패한다.
SW_TEST_CASE_F( RESPStoreTest, ServerCloseFailsWaitingRequests )
{
    start();
    SW_EXPECT_LOG( "RespConnection.RESPConnectionClosed" );
    (void)_store.submit( EphemeralRequest::makeGet( "a" ) );
    SW_EXPECT_TRUE( poll().empty() );
    _server._transport->close( _server._lastHandle, StreamCloseMode::Abort );
    const vector<EphemeralReply> listReply = poll();
    SW_REQUIRE_EQUAL( listReply.size(), size_t{ 1 } );
    SW_EXPECT_TRUE( listReply[0]._result == EphemeralResult::Unavailable );
}

// 비밀번호가 있으면 첫 명령이 AUTH 다(사용자 이름은 있을 때만).
SW_TEST_CASE_F( RESPStoreTest, AuthGoesFirst )
{
    RESPStoreConfig config;
    config._userName = "user";
    config._password = "pw";
    start( config );
    _server.pushAll( { "+OK\r\n", "$-1\r\n" } );
    SW_EXPECT_TRUE( run( EphemeralRequest::makeGet( "k" ) )._result == EphemeralResult::NotFound );
    SW_EXPECT_EQUAL( takeCommands(), string( "AUTH user pw | GET g:k" ) );
}

// 결함: AUTH 거절 · 깨진 답으로 끊은 연결은 물러섬이 없어, 구독 연결이 폴링마다 다시 접속해 서버를 두드렸다.
SW_TEST_CASE_F( RESPStoreTest, RejectedAuthBacksOffBeforeReconnecting )
{
    RESPStoreConfig config;
    config._password  = "wrong";
    config._timeoutMs = 1;
    start( config );
    SW_EXPECT_LOG( "RespConnection.RESPAUTHRejected" );
    SW_EXPECT_LOG( "RespConnection.RESPConnectionAborted" );
    SW_EXPECT_LOG( "RespEphemeralStore.RESPSubscriptionNotConfirmed" );
    for ( int32 index = 0; index < 64; ++index )
    {
        _server._listReply.emplace_back( "-WRONGPASS\r\n" );
    }
    poll(); // 명령 연결도 시작 때 열려 있다 - 거절된다
    const int32 openedAfterCommand = _server._openedCount;

    _store.subscribe( "chan" );
    for ( int32 round = 0; round < 8; ++round )
    {
        (void)collectMessages();
    }
    SW_EXPECT_TRUE( _server._openedCount - openedAfterCommand <= 2 ); // 시계가 그대로면 첫 실패 뒤 한 번 말고는 다시 접속하지 않는다

    const int32 opened = _server._openedCount;
    _clock.advanceMilliseconds( 6000 );
    (void)collectMessages();
    SW_EXPECT_TRUE( _server._openedCount > opened ); // 물러섬이 지나면 다시 해 본다
}

// 구독: 접두어를 붙여 구독하고, 받은 메시지는 접두어를 떼어 준다. 다른 접두어의 메시지는 버린다.
SW_TEST_CASE_F( RESPStoreTest, SubscribeDeliversMessagesWithoutPrefix )
{
    RESPStoreConfig config;
    config._timeoutMs = 1; // 같은 스레드라 구독 확인을 기다릴 수 없다 - 바로 넘어가게 한다
    start( config );
    SW_EXPECT_LOG( "RespEphemeralStore.RESPSubscriptionNotConfirmed" );
    _store.subscribe( "chan" );
    (void)collectMessages();
    SW_EXPECT_EQUAL( takeCommands(), string( "SUBSCRIBE g:chan" ) );
    const StreamConnectionHandle subscribed = _server._lastHandle;

    _server.send( subscribed, "*3\r\n$9\r\nsubscribe\r\n$6\r\ng:chan\r\n:1\r\n" );
    _server.send( subscribed, "*3\r\n$7\r\nmessage\r\n$6\r\ng:chan\r\n$2\r\nhi\r\n" );
    _server.send( subscribed, "*3\r\n$7\r\nmessage\r\n$6\r\nx:chan\r\n$2\r\nno\r\n" );
    const vector<EphemeralMessage> listMessage = collectMessages();
    SW_REQUIRE_EQUAL( listMessage.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( listMessage[0]._channel, string( "chan" ) );
    SW_EXPECT_TRUE( listMessage[0]._bytes == bytes( "hi" ) );

    _store.subscribe( "chan" ); // 이미 구독한 채널은 다시 보내지 않는다
    (void)collectMessages();
    SW_EXPECT_EQUAL( takeCommands(), string() );

    _store.unsubscribe( "chan" );
    (void)collectMessages();
    SW_EXPECT_EQUAL( takeCommands(), string( "UNSUBSCRIBE g:chan" ) );
    _store.subscribe( "Bad Channel" );
    (void)collectMessages();
    SW_EXPECT_EQUAL( takeCommands(), string() );
}

// 구독 연결이 끊기면 물러섬 뒤 채널을 모두 다시 구독한다.
SW_TEST_CASE_F( RESPStoreTest, ReconnectResubscribes )
{
    RESPStoreConfig config;
    config._timeoutMs = 1;
    start( config );
    SW_EXPECT_LOG( "RespEphemeralStore.RESPSubscriptionNotConfirmed" );
    SW_EXPECT_LOG( "RespConnection.RESPConnectionClosed" );
    _store.subscribe( "b" );
    _store.subscribe( "a" );
    (void)collectMessages();
    (void)takeCommands();

    _server._transport->close( _server._lastHandle, StreamCloseMode::Abort );
    (void)collectMessages();
    _clock.advanceMilliseconds( 6000 );
    (void)collectMessages();
    SW_EXPECT_EQUAL( takeCommands(), string( "SUBSCRIBE g:a | SUBSCRIBE g:b" ) );
}

// 끝내면 기다리던 요청은 실패로 끝나고, 뒤 요청은 바로 실패다. 두 번 끝내도 된다.
SW_TEST_CASE_F( RESPStoreTest, ShutdownFailsEverything )
{
    start();
    (void)_store.submit( EphemeralRequest::makeGet( "a" ) );
    SW_EXPECT_TRUE( poll().empty() );
    _store.shutdown();
    _store.shutdown();

    (void)_store.submit( EphemeralRequest::makeGet( "b" ) );
    vector<EphemeralReply> listReply;
    SW_EXPECT_EQUAL( _store.pollReplies( listReply ), 2 );
    SW_EXPECT_TRUE( listReply[0]._result == EphemeralResult::Unavailable );
    SW_EXPECT_TRUE( listReply[1]._result == EphemeralResult::Unavailable );
    _store.subscribe( "chan" );
    _store.unsubscribe( "chan" );
}

// 시작 조건: 전송과 주소가 있어야 한다. 시작 전 요청은 실패로 돌아온다.
SW_TEST_CASE_F( RESPStoreTest, InitializeNeedsTransportAndAddress )
{
    (void)_store.submit( EphemeralRequest::makeGet( "a" ) );
    vector<EphemeralReply> listReply;
    SW_EXPECT_EQUAL( _store.pollReplies( listReply ), 1 );
    SW_EXPECT_TRUE( listReply[0]._result == EphemeralResult::Unavailable );

    string          error;
    RESPStoreConfig config;
    config._address = NetAddress::makeLoopback( FakeRESPServer::kPort );
    StreamTransportConfig settings;
    settings._ioThreadCount = 0;
    error                   = describeError( _store.initialize( nullptr, settings, config, nullptr ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_FALSE( error.empty() );
    error.clear();
    error = describeError( _store.initialize( _network.createTransport(), settings, RESPStoreConfig{}, nullptr ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_FALSE( error.empty() );
}
