#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/RESP/RESPCodec.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineEndpointTestUtil.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineRuntimeTestUtil.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

// RESPEndToEndTest — 서버 설정이 고른 `resp` 캐시: 조립(`OnlineServerRuntime`)이 드라이버 키트에서 저장소를 만들고, 실제 TCP 로 로컬 가짜 RESP 서버에 붙어 AUTH(환경 변수 비밀) · 쓰기 · 읽기 · 발행 · 구독을 끝까지 돈다.
// 가짜 서버는 Valkey 의 명령 부분집합을 값으로 흉내 낸다 — 답을 대본으로 주는 `TestRespEphemeralStore.cpp` 와 달리 무엇이 키로 닿았는지 본다. 드라이버 표에 없는 이름 · 없는 환경 변수는 메모리로 물러나지 않고 기동이 멈춘다(nogpu).

using namespace sw;

namespace
{
    constexpr int64       kNowMs     = 1700000000000ll;
    constexpr const utf8* kSecretEnv = "SW_TEST_RESP_END_TO_END_SECRET";
    constexpr const utf8* kSecret    = "cache-secret";

    /** @brief 값을 기억하는 작은 RESP 서버입니다. 전용 I/O 스레드에서 답합니다 - 저장소가 구독 확인을 막고 기다린다. 테스트 스레드는 잠금을 지나 값을 봅니다. */
    class RESPEndpointServer final : public IStreamHandler
    {
    public:
        ~RESPEndpointServer() override { stop(); }

        void start( string_view password )
        {
            _password  = string( password );
            _transport = StreamTransportFactory::createPlatformTransport();
            SW_REQUIRE_TRUE( _transport != nullptr );
            SW_REQUIRE_TRUE( _transport->initialize( this, StreamTransportConfig{} ) );
            SW_REQUIRE_TRUE( _transport->listen( NetAddress::makeLoopback( 0 ) ) );
        }

        void stop()
        {
            if ( _transport != nullptr )
                _transport->shutdown();
        }

        uint16 getPort() const { return _transport->getListenPort(); }

        int32 getAuthCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _authCount;
        }

        string findValue( const string& key ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        valueIt = _mapValue.find( key );
            return valueIt != _mapValue.end() ? valueIt->second : string{};
        }

        vector<string> takeUnknownCommands() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _listUnknown;
        }

        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            ScopedLock<mutex> lock{ _mutex };
            _mapPeer[handle.packed()] = make_unique<Peer>();
        }

        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        peerIt = _mapPeer.find( handle.packed() );
            if ( peerIt == _mapPeer.end() )
                return;
            Peer& peer = *peerIt->second;
            peer._parser.append( pData, static_cast<size_t>( size ) );
            RESPValue value;
            while ( peer._parser.next( value ) == RESPParseResult::Complete )
            {
                handleCommand( handle, peer, value );
            }
        }

        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)reason;
            ScopedLock<mutex> lock{ _mutex };
            _mapPeer.erase( handle.packed() );
        }

    private:
        struct Peer
        {
            RESPParser     _parser{};
            vector<string> _listChannel{};
        };

        void reply( StreamConnectionHandle handle, string_view text ) { (void)_transport->send( handle, reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) ); }

        static string makeBulk( string_view text )
        {
            StringBuilder<constant::buffer::kMax256> bulk;
            bulk.appendFormat( "$%#\r\n%#\r\n", text.size(), text );
            return string( bulk.view() );
        }

        void handleCommand( StreamConnectionHandle handle, Peer& peer, const RESPValue& command )
        {
            vector<string> listArgument;
            for ( const RESPValue& element : command._listElement )
            {
                listArgument.emplace_back( element.getText() );
            }
            if ( listArgument.empty() )
                return;
            const string& name = listArgument[0];
            if ( name == "AUTH" )
            {
                ++_authCount;
                reply( handle, listArgument.back() == _password ? string_view( "+OK\r\n" ) : string_view( "-WRONGPASS\r\n" ) );
            }
            else if ( name == "SET" && listArgument.size() >= 3 )
            {
                _mapValue[listArgument[1]] = listArgument[2];
                reply( handle, "+OK\r\n" );
            }
            else if ( name == "GET" && listArgument.size() == 2 )
            {
                const auto valueIt = _mapValue.find( listArgument[1] );
                reply( handle, valueIt != _mapValue.end() ? makeBulk( valueIt->second ) : string( "$-1\r\n" ) );
            }
            else if ( name == "SUBSCRIBE" && listArgument.size() == 2 )
            {
                peer._listChannel.push_back( listArgument[1] );
                reply( handle, "*3\r\n$9\r\nsubscribe\r\n" + makeBulk( listArgument[1] ) + ":1\r\n" );
            }
            else if ( name == "UNSUBSCRIBE" && listArgument.size() == 2 )
            {
                reply( handle, "*3\r\n$11\r\nunsubscribe\r\n" + makeBulk( listArgument[1] ) + ":0\r\n" );
            }
            else if ( name == "PUBLISH" && listArgument.size() == 3 )
            {
                reply( handle, publish( listArgument[1], listArgument[2] ) );
            }
            else
            {
                _listUnknown.push_back( name );
                reply( handle, "-ERR unknown command\r\n" );
            }
        }

        /** @brief 구독한 연결마다 메시지를 보내고 발행 답(받은 수)을 돌려줍니다. */
        string publish( const string& channel, const string& payload )
        {
            int32 receiverCount = 0;
            for ( const auto& [packed, pPeer] : _mapPeer )
            {
                for ( const string& subscribed : pPeer->_listChannel )
                {
                    if ( subscribed != channel )
                        continue;
                    reply( StreamConnectionHandle::fromPacked( packed ), "*3\r\n$7\r\nmessage\r\n" + makeBulk( channel ) + makeBulk( payload ) );
                    ++receiverCount;
                }
            }
            StringBuilder<constant::buffer::kMax256> resultBuilder;
            resultBuilder.appendFormat( ":%#\r\n", receiverCount );
            return string( resultBuilder.view() );
        }

    private:
        mutable mutex                           _mutex;
        unordered_map<uint64, unique_ptr<Peer>> _mapPeer{};
        unordered_map<string, string>           _mapValue{};
        vector<string>                          _listUnknown{};
        string                                  _password{};
        unique_ptr<IStreamTransport>            _transport{};
        int32                                   _authCount{ 0 };
    };

    struct RESPEndToEndTestUtil
    {
        static string makeServerJSON( uint16 port )
        {
            StringBuilder<constant::buffer::kMax256> resultBuilder;
            resultBuilder.appendFormat( R"({ "_listStore": [ { "_name": "local", "_driver": "memory" } ], "_listCache": [ { "_name": "valkey", "_driver": "resp", "_endpoint": "127.0.0.1:%#?prefix=g:", "_secretEnvironment": "%#" } ] })", port, kSecretEnv );
            return string( resultBuilder.view() );
        }

        static constexpr const utf8* kOnlineJSON = R"({ "_storeName": "local", "_cacheName": "valkey", "_listKit": [ { "_name": "testLogin" } ] })";
    };
} // namespace

/** @brief [RESPEndToEndTest] 설정이 고른 `resp` 로 조립이 서고, 환경 변수의 비밀로 AUTH 한 뒤 접두어 붙은 키로 쓰고 읽으며 발행한 메시지를 구독으로 받는다 */
SW_TEST_CASE( RESPEndToEndTest, ConfigSelectsRESPAndReachesTheEndpoint )
{
    RESPEndpointServer server;
    server.start( kSecret );
    const test::ScopedEnvironmentVariable secret{ kSecretEnv, kSecret };

    test::OnlineRuntimeFixture rig;
    string                     error;
    {
        const auto result = rig.fromConfig( RESPEndToEndTestUtil::makeServerJSON( server.getPort() ), RESPEndToEndTestUtil::kOnlineJSON, kNowMs );
        SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
    }
    const auto pump = [&rig]()
    {
        rig.updateAll( kNowMs, 1 );
    };

    EphemeralStoreRouter& router = *rig._runtime.getContext()._pRouter;
    string                readBack;
    string                message;
    int32                 repliedCount = 0;
    const Subscription    owner        = router.openReplyOwner();
    const Subscription    subscription = router.subscribe( "e2e.topic", EphemeralStoreRouter::MessageDelegate::create( [&message]( const EphemeralMessage& received )
    { message.assign( received._bytes.begin(), received._bytes.end() ); } ) );
    const auto            onReply      = [&repliedCount, &readBack]( const EphemeralReply& reply )
    {
        ++repliedCount;
        SW_EXPECT_TRUE( reply._result == EphemeralResult::Ok );
        if ( reply._operation == EphemeralOperation::Get )
            readBack.assign( reply._value.begin(), reply._value.end() );
    };

    (void)router.submit( EphemeralRequest::makeSet( "e2e:key", StringUtil::toBytes( "value" ), 0 ), EphemeralStoreRouter::ReplyDelegate::create( onReply ), owner );
    (void)router.submit( EphemeralRequest::makeGet( "e2e:key" ), EphemeralStoreRouter::ReplyDelegate::create( onReply ), owner );
    SW_REQUIRE_TRUE( test::pollUntil( pump, [&repliedCount]()
    { return repliedCount == 2; } ) );
    SW_EXPECT_EQUAL( string( "value" ), readBack );
    SW_EXPECT_EQUAL( string( "value" ), server.findValue( "g:e2e:key" ) );
    SW_EXPECT_TRUE( server.getAuthCount() >= 2 ); // 명령 연결과 구독 연결이 각각 AUTH 한다

    router.submit( EphemeralRequest::makePublish( "e2e.topic", StringUtil::toBytes( "hello" ) ) );
    SW_REQUIRE_TRUE( test::pollUntil( pump, [&message]()
    { return message.empty() == false; } ) );
    SW_EXPECT_EQUAL( string( "hello" ), message );
    SW_EXPECT_TRUE( server.takeUnknownCommands().empty() );
}

/** @brief [RESPEndToEndTest] 비밀 환경 변수가 없으면 비밀 없이 붙지 않고 기동이 멈춘다 */
SW_TEST_CASE( RESPEndToEndTest, MissingSecretStopsStartup )
{
    SW_EXPECT_LOG( "Server.SecretNotSet" );
    test::OnlineRuntimeFixture rig;
    string                     error;
    error = describeError( rig.fromConfig( RESPEndToEndTestUtil::makeServerJSON( 6379 ), RESPEndToEndTestUtil::kOnlineJSON, kNowMs ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "valkey" ) != string::npos );
}

/** @brief [RESPEndToEndTest] 이 빌드에 없는 드라이버 이름은 메모리로 물러나지 않고 기동이 멈춘다 */
SW_TEST_CASE( RESPEndToEndTest, UnknownDriverStopsStartup )
{
    constexpr const utf8*      kServerJSON = R"({ "_listStore": [ { "_name": "local", "_driver": "memory" } ], "_listCache": [ { "_name": "valkey", "_driver": "memcached", "_endpoint": "127.0.0.1:1" } ] })";
    test::OnlineRuntimeFixture rig;
    string                     error;
    error = describeError( rig.fromConfig( kServerJSON, RESPEndToEndTestUtil::kOnlineJSON, kNowMs ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "memcached" ) != string::npos );
}
