#include "pch.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "EngineTest/GameFramework/Kits/Storage/FakeRespServer.h"
#include "EngineTest/GameFramework/Online/EphemeralStoreContract.h"

#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Kits/Storage/Server/CacheStore/CacheStoreFactory.h"
#include "GameFramework/Kits/Storage/Server/CacheStore/Driver/Resp/RespEphemeralStore.h"

// RESP 휘발성 저장 — 시험 안 가짜 RESP 서버(루프백 스트림)에 계약 여덟을 돌린다(답이 1..3 바이트 조각으로 온다). 그리고 한 바이트 조각 ·
// 끊김(기다리던 요청은 정확히 한 번 Unavailable) · 다시 연결 · 시한 · AUTH · TLS · 구독 연결이 끊긴 뒤 다시 구독 · 공장의 끝점 읽기. 결정적이고 CI 에서 돈다.

using namespace sw;

namespace
{
    struct RespStoreHarness
    {
        static StreamTransportSettings makeTransportSettings()
        {
            StreamTransportSettings settings;
            settings._ioThreadCount      = 0; // 루프백 — 앞이 직접 돈다
            settings._idleTimeoutSeconds = 0.0;
            return settings;
        }

        static RespStoreSettings makeSettings( const NetAddress& address )
        {
            RespStoreSettings settings;
            settings._address   = address;
            settings._keyPrefix = "t1:";
            settings._timeoutMs = 3000;
            return settings;
        }

        static bool initializeStore( RespEphemeralStore& store, test::FakeRespServer& server, const RespStoreSettings& settings,
                                     unique_ptr<ITlsContext> tlsContext = nullptr )
        {
            string     error;
            const bool bInitialized = store.initialize( server.createClientTransport(), makeTransportSettings(), settings, std::move( tlsContext ), error );
            SW_EXPECT_TRUE_MSG( bInitialized, error.c_str() );
            return bInitialized;
        }

        /** @brief 요청을 맡기고 답이 올 때까지 거둡니다(최대 @p waitMs). 받은 답 수입니다. */
        static int32 collectReplies( IEphemeralStore& store, size_t expectedCount, int64 waitMs, vector<EphemeralReply>& outListReply )
        {
            const Deadline deadline = Deadline::afterMilliseconds( waitMs );
            while ( outListReply.size() < expectedCount && deadline.isExpired() == false )
            {
                if ( store.pollReplies( outListReply ) == 0 )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            return static_cast<int32>( outListReply.size() );
        }
    };

    /** @brief 계약 픽스처 — 가짜 서버 하나 · RESP 앞 둘(발행/구독), 받는 바이트는 1..3 바이트 조각. */
    struct FakeRespFixture
    {
        LoopbackStreamNetwork _network;
        test::FakeRespServer  _server;
        RespEphemeralStore    _store;
        RespEphemeralStore    _secondStore;

        FakeRespFixture()
            : _network{ 5u }
            , _server{ _network }
            , _store{}
            , _secondStore{}
        {
            LoopbackStreamConditions conditions;
            conditions._maxChunkBytes = 3;
            _network.setConditions( conditions );
            (void)RespStoreHarness::initializeStore( _store, _server, RespStoreHarness::makeSettings( _server.getAddress() ) );
            (void)RespStoreHarness::initializeStore( _secondStore, _server, RespStoreHarness::makeSettings( _server.getAddress() ) );
        }

        IEphemeralStore& getStore() { return _store; }
        IEphemeralStore& getSecondStore() { return _secondStore; }
        void             advanceTimeMs( int64 deltaMs ) { _server.advanceTimeMs( deltaMs ); }

        string makeKey( const utf8* pName )
        {
            string key{ "contract:" };
            key += pName;
            return key;
        }
    };
} // namespace

SW_EPHEMERAL_STORE_CONTRACT_SUITE( RespEphemeralStoreFakeServerTest, FakeRespFixture )

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, OneByteChunksGiveTheSameResult )
{
    LoopbackStreamNetwork    network{ 9u };
    LoopbackStreamConditions conditions;
    conditions._maxChunkBytes = 1;
    network.setConditions( conditions );
    test::FakeRespServer server{ network };
    RespEphemeralStore   store;
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( store, server, RespStoreHarness::makeSettings( server.getAddress() ) ) );

    vector<uint8> bytes( 4096 );
    for ( size_t index = 0; index < bytes.size(); ++index )
        bytes[index] = static_cast<uint8>( index * 7u + 13u ); // \r · \n · 0 이 몸 안에 든다
    EphemeralReply reply;
    SW_EXPECT_TRUE( test::EphemeralStoreContract::executeResult( store, EphemeralRequest::makeSet( "big", bytes, 0 ) ) == EphemeralResult::Ok );
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( store, EphemeralRequest::makeGet( "big" ), reply ) );
    SW_EXPECT_TRUE( reply._value == bytes );
    SW_EXPECT_TRUE( test::EphemeralStoreContract::executeResult( store, EphemeralRequest::makeScoreSet( "rank", "x", 7 ) ) == EphemeralResult::Ok );
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( store, EphemeralRequest::makeScoreRange( "rank", 0, 5 ), reply ) );
    SW_ASSERT_EQUAL( size_t( 1 ), reply._listMember.size() );
    SW_EXPECT_TRUE( reply._listMember[0]._member == "x" && reply._listMember[0]._score == 7 );
}

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, DroppedConnectionAnswersEveryPendingRequestOnceThenReconnects )
{
    LoopbackStreamNetwork network{ 3u };
    test::FakeRespServer  server{ network };
    RespEphemeralStore    store;
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( store, server, RespStoreHarness::makeSettings( server.getAddress() ) ) );
    SW_EXPECT_TRUE( test::EphemeralStoreContract::executeResult( store, EphemeralRequest::makeSet( "k", vector<uint8>{ 1 }, 0 ) ) == EphemeralResult::Ok );

    server.setHoldReplies( true );
    for ( int32 index = 0; index < 5; ++index )
        (void)store.submit( EphemeralRequest::makeGet( "k" ) );
    vector<EphemeralReply> listReply;
    SW_EXPECT_EQUAL( 0, RespStoreHarness::collectReplies( store, 5, 50, listReply ) ); // 서버가 붙잡고 있다
    server.dropAllConnections();
    SW_ASSERT_EQUAL( 5, RespStoreHarness::collectReplies( store, 5, 3000, listReply ) );
    for ( const EphemeralReply& reply : listReply )
        SW_EXPECT_TRUE( reply._result == EphemeralResult::Unavailable );
    SW_EXPECT_EQUAL( 5, RespStoreHarness::collectReplies( store, 6, 50, listReply ) ); // 한 번씩만
    SW_EXPECT_EQUAL( 0, store.getPendingCount() );

    server.setHoldReplies( false );
    EphemeralReply reply;
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( store, EphemeralRequest::makeGet( "k" ), reply ) ); // 다음 요청이 다시 연결한다
    SW_EXPECT_TRUE( reply._result == EphemeralResult::Ok );
    SW_EXPECT_TRUE( reply._value == vector<uint8>{ 1 } );
    SW_EXPECT_EQUAL( 2, server.getAcceptedCount() );
}

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, TimedOutRequestIsUnavailableAndTheNextOneReconnects )
{
    LoopbackStreamNetwork network{ 4u };
    test::FakeRespServer  server{ network };
    RespEphemeralStore    store;
    RespStoreSettings     settings = RespStoreHarness::makeSettings( server.getAddress() );
    settings._timeoutMs            = 100;
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( store, server, settings ) );

    server.setHoldReplies( true );
    const Stopwatch stopwatch;
    EphemeralReply  reply;
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( store, EphemeralRequest::makeIncrement( "n", 1, 0 ), reply ) );
    SW_EXPECT_TRUE( reply._result == EphemeralResult::Unavailable );
    SW_EXPECT_TRUE( stopwatch.getElapsedMilliseconds() >= 100 );

    server.setHoldReplies( false );
    const Deadline  deadline = Deadline::afterMilliseconds( 3000 );
    EphemeralResult result   = EphemeralResult::Unavailable;
    while ( result == EphemeralResult::Unavailable && deadline.isExpired() == false ) // 물러남(첫 번은 0) 동안은 바로 Unavailable
        result = test::EphemeralStoreContract::executeResult( store, EphemeralRequest::makeSet( "n2", vector<uint8>{ 2 }, 0 ) );
    SW_EXPECT_TRUE( result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( 2, server.getAcceptedCount() );
}

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, AuthIsSentFirstAndAWrongPasswordFailsRequests )
{
    LoopbackStreamNetwork network{ 6u };
    test::FakeRespServer  server{ network };
    server.setPassword( "s3cret" );

    RespEphemeralStore good;
    RespStoreSettings  goodSettings = RespStoreHarness::makeSettings( server.getAddress() );
    goodSettings._password          = "s3cret";
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( good, server, goodSettings ) );
    SW_EXPECT_TRUE( test::EphemeralStoreContract::executeResult( good, EphemeralRequest::makeSet( "a", vector<uint8>{ 1 }, 0 ) ) == EphemeralResult::Ok );

    RespEphemeralStore bad;
    RespStoreSettings  badSettings = RespStoreHarness::makeSettings( server.getAddress() );
    badSettings._password          = "wrong";
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( bad, server, badSettings ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a rejected AUTH logs an error" );
        SW_EXPECT_TRUE( test::EphemeralStoreContract::executeResult( bad, EphemeralRequest::makeGet( "a" ) ) == EphemeralResult::Unavailable );
    }
}

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, TlsCarriesTheCommands )
{
    INetSecurityProvider& provider = EngineNetSecurity::getProvider();
    string                certificatePem;
    string                privateKeyPem;
    SW_ASSERT_TRUE( provider.createSelfSignedCertificate( "localhost", 30, certificatePem, privateKeyPem ) );
    string             error;
    TlsContextSettings serverSettings;
    serverSettings._role                  = TlsRole::Server;
    serverSettings._certificatePem        = certificatePem;
    serverSettings._privateKeyPem         = privateKeyPem;
    unique_ptr<ITlsContext> serverContext = provider.createTlsContext( serverSettings, error );
    TlsContextSettings      clientSettings;
    clientSettings._role                  = TlsRole::Client;
    clientSettings._trustPem              = certificatePem;
    clientSettings._serverName            = "localhost";
    unique_ptr<ITlsContext> clientContext = provider.createTlsContext( clientSettings, error );
    SW_ASSERT_TRUE( serverContext != nullptr && clientContext != nullptr );

    LoopbackStreamNetwork network{ 8u };
    test::FakeRespServer  server{ network, serverContext.get() };
    RespEphemeralStore    store;
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( store, server, RespStoreHarness::makeSettings( server.getAddress() ), std::move( clientContext ) ) );
    SW_EXPECT_TRUE( test::EphemeralStoreContract::executeResult( store, EphemeralRequest::makeSet( "secure", vector<uint8>{ 9, 8 }, 0 ) ) == EphemeralResult::Ok );
    EphemeralReply reply;
    SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( store, EphemeralRequest::makeGet( "secure" ), reply ) );
    SW_EXPECT_TRUE( reply._value == ( vector<uint8>{ 9, 8 } ) );
}

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, SubscriptionComesBackAfterTheConnectionDrops )
{
    LoopbackStreamNetwork network{ 2u };
    test::FakeRespServer  server{ network };
    RespEphemeralStore    publisher;
    RespEphemeralStore    subscriber;
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( publisher, server, RespStoreHarness::makeSettings( server.getAddress() ) ) );
    SW_ASSERT_TRUE( RespStoreHarness::initializeStore( subscriber, server, RespStoreHarness::makeSettings( server.getAddress() ) ) );
    subscriber.subscribe( "news" );
    SW_EXPECT_EQUAL( 1, server.getSubscriberCount( "t1:news" ) );

    server.dropAllConnections();
    const Deadline           deadline = Deadline::afterMilliseconds( 3000 );
    vector<EphemeralMessage> listMessage;
    while ( server.getSubscriberCount( "t1:news" ) == 0 && deadline.isExpired() == false )
    {
        (void)subscriber.pollMessages( listMessage ); // 끊긴 것을 보고 다시 연결해 다시 구독한다
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    SW_ASSERT_EQUAL( 1, server.getSubscriberCount( "t1:news" ) );

    // 발행하는 앞의 명령 연결도 끊겼다 — 끊김을 보기 전에 맡긴 첫 요청은 Unavailable 이고(계약: 다시 맡긴다) 다음 요청이 다시 연결한다.
    EphemeralReply published;
    published._result = EphemeralResult::Unavailable;
    for ( int32 attempt = 0; attempt < 3 && published._result == EphemeralResult::Unavailable; ++attempt )
    {
        SW_ASSERT_TRUE( test::EphemeralStoreContract::executeRequest( publisher, EphemeralRequest::makePublish( "news", vector<uint8>{ 5 } ), published ) );
    }
    SW_EXPECT_TRUE( published._result == EphemeralResult::Ok );
    SW_EXPECT_EQUAL( int64( 1 ), published._integer );
    SW_EXPECT_EQUAL( 1, test::EphemeralStoreContract::collectMessages( subscriber, 2000, listMessage ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listMessage.size() );
    SW_EXPECT_TRUE( listMessage[0]._channel == "news" ); // 접두를 뗀 이름
}

SW_TEST_CASE( RespEphemeralStoreFakeServerTest, FactoryReadsTheEndpointAndRefusesUnknownDrivers )
{
    CacheEndpoint endpoint;
    string        error;
    SW_ASSERT_TRUE( CacheStoreFactory::parseEndpoint( "127.0.0.1:6380?prefix=game1:&timeoutMs=500&tls=1&ca=certs/ca.pem", endpoint, error ) );
    SW_EXPECT_TRUE( endpoint._address == NetAddress::make( 127, 0, 0, 1, 6380 ) );
    SW_EXPECT_TRUE( endpoint._keyPrefix == "game1:" );
    SW_EXPECT_EQUAL( int64( 500 ), endpoint._timeoutMs );
    SW_EXPECT_TRUE( endpoint._bTls == SW_TRUE );
    SW_EXPECT_TRUE( endpoint._trustFile == "certs/ca.pem" );

    SW_ASSERT_TRUE( CacheStoreFactory::parseEndpoint( "localhost", endpoint, error ) );
    SW_EXPECT_TRUE( endpoint._address == NetAddress::makeLoopback( CacheStoreFactory::kDefaultRespPort ) );
    SW_EXPECT_TRUE( endpoint._host == "localhost" );

    SW_EXPECT_FALSE( CacheStoreFactory::parseEndpoint( "cache.example:6379", endpoint, error ) ); // 이름 해석은 없다
    SW_EXPECT_FALSE( CacheStoreFactory::parseEndpoint( "127.0.0.1:6379?db=2", endpoint, error ) );
    SW_EXPECT_FALSE( CacheStoreFactory::parseEndpoint( "127.0.0.1:6379?tls=yes", endpoint, error ) );
    SW_EXPECT_FALSE( CacheStoreFactory::parseEndpoint( "", endpoint, error ) );

    unique_ptr<IEphemeralStore> memory = CacheStoreFactory::createEphemeralStore( CacheStoreFactory::kMemoryDriverName, "", "", error );
    SW_EXPECT_TRUE( memory != nullptr );
    SW_EXPECT_TRUE( CacheStoreFactory::createEphemeralStore( "redis-cluster", "127.0.0.1:6379", "", error ) == nullptr );
    SW_EXPECT_TRUE( error.find( "redis-cluster" ) != string::npos );
}
