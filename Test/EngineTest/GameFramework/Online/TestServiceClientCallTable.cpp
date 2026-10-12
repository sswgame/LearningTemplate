// 클라이언트 호출 표 — 응답이 사용자 델리게이트를 한 번 찾는다. sendRequest 가 그 자리에서 실패를 알려도(초기화 전 · 끊김) 델리게이트를 잃지 않는다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    using DoneDelegate = Delegate<void( uint16 )>;

    /** @brief 키트 클라이언트 모양 — 응답 공통 처리기가 표에서 사용자 델리게이트를 꺼낸다. */
    struct CallTableClient
    {
        ServiceClientCallTable<DoneDelegate> _callTable;
        OnlineServiceClient*                 _pClient{ nullptr };

        uint64 send( uint16 method, const BitWriter& body, const DoneDelegate& onDone )
        {
            return _callTable.send( *_pClient, method, body, NetRequestOptions{}, OnlineResponseDelegate::create<&CallTableClient::onResponse>( this ), onDone );
        }

        void onResponse( const OnlineResponse& response )
        {
            DoneDelegate onDone;
            if ( _callTable.take( response._requestID, onDone ) && onDone.isBound() )
                onDone( response._errorCode );
        }
    };

    struct DoneRecorder
    {
        vector<uint16> _listErrorCode{};

        void onDone( uint16 errorCode ) { _listErrorCode.push_back( errorCode ); }
    };

    /** @brief 보내는 중에 먼저 다른 id 의 응답을, 이어서 자기 응답을 바로 알리는 가짜 클라이언트입니다. */
    struct ForeignFirstClient
    {
        ServiceClientCallTable<DoneDelegate>* _pTable{ nullptr };
        uint64                                _nextRequestID{ 7 };
        vector<uint64>                        _listUnmatchedID{};

        uint64 peekNextRequestID() const { return _nextRequestID; }

        uint64 sendRequest( uint16, int32, int32, const DoneDelegate& )
        {
            const uint64 requestID = _nextRequestID++;
            for ( const uint64 respondID : { requestID + 100, requestID } )
            {
                DoneDelegate onDone;
                if ( _pTable->take( respondID, onDone ) && onDone.isBound() )
                    onDone( static_cast<uint16>( respondID ) );
                else
                    _listUnmatchedID.push_back( respondID );
            }
            return requestID;
        }
    };

    /** @brief 수명 순서만 보는 빈 키트(테스트 로그인과 같은 영역). */
    class LifetimeClientService final : public IOnlineClientService
    {
    public:
        uint16 getMethodRange() const override { return OnlineMethodRange::kGame; }
        uint32 getProtocolVersion() const override { return 1; }
        void   onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }
    };

    /** @brief 첫 응답에서 자기 주인(@p _pOwner)이 자신을 파괴하게 하는 키트입니다. 응답 수는 키트 밖 @p _pResponseCount 에 센다. */
    class SelfDestroyingClientService final : public IOnlineClientService
    {
    public:
        unique_ptr<SelfDestroyingClientService>* _pOwner{ nullptr };
        int32*                                   _pResponseCount{ nullptr };

        uint16 getMethodRange() const override { return OnlineMethodRange::kGame; }
        uint32 getProtocolVersion() const override { return 1; }
        void   onServicePush( uint16 kind, BitReader& body ) override
        {
            (void)kind;
            (void)body;
        }

        void onResponse( const OnlineResponse& response )
        {
            (void)response;
            ++*_pResponseCount;
            _pOwner->reset(); // 이 뒤로 this 를 건드리지 않는다
        }
    };

} // namespace

/**
 * @brief [ServiceClientCallTableTest] 모은 요청을 실패로 알리는 중 응답 콜백이 키트를 파괴하면 남은 그 키트의 요청은 부르지 않는다.
 * @details 예전에는 지역 목록으로 꺼낸 뒤라 cancelRequestsOf 가 빈 멤버 목록만 훑어, 파괴된 키트의 델리게이트를 불렀다.
 */
SW_TEST_CASE( ServiceClientCallTableTest, KitDestroyedWhileFailingQueuedCallsGetsNoMoreCalls )
{
    LoopbackStreamNetwork                   network( 5u );
    unique_ptr<IStreamTransport>            transport = network.createTransport();
    OnlineServiceClient                     client;
    int32                                   responseCount = 0;
    unique_ptr<SelfDestroyingClientService> kit           = sw::make_unique<SelfDestroyingClientService>();
    kit->_pOwner                                          = &kit;
    kit->_pResponseCount                                  = &responseCount;
    SW_ASSERT_TRUE( client.registerClientService( kit.get() ) );
    string error;
    SW_ASSERT_TRUE_MSG( client.initialize( transport.get(), OnlineServiceClientSettings{}, error ), error.c_str() ); // 서버가 없다 — 요청은 모인다

    const OnlineResponseDelegate onResponse = OnlineResponseDelegate::create<&SelfDestroyingClientService::onResponse>( kit.get() );
    (void)client.sendRequest( OnlineMethodRange::kGame + 1, BitWriter{}, NetRequestOptions{}, onResponse );
    (void)client.sendRequest( OnlineMethodRange::kGame + 2, BitWriter{}, NetRequestOptions{}, onResponse );
    client.shutdown(); // 첫 실패 알림이 키트를 파괴한다

    SW_EXPECT_EQUAL( responseCount, 1 );
    SW_EXPECT_TRUE( kit == nullptr );
}

SW_TEST_CASE( ServiceClientCallTableTest, ImmediateFailureInsideSendStillReachesTheDelegate )
{
    OnlineServiceClient client; // 초기화 전 — sendRequest 가 그 자리에서 kUnavailable 을 알린다
    CallTableClient     tableClient;
    tableClient._pClient = &client;
    DoneRecorder recorder;
    const uint64 requestID = tableClient.send( OnlineMethodRange::kGame + 1, BitWriter{}, DoneDelegate::create<&DoneRecorder::onDone>( &recorder ) );
    SW_EXPECT_TRUE( requestID != 0 );
    SW_ASSERT_EQUAL( recorder._listErrorCode.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listErrorCode[0], OnlineError::kUnavailable );
    SW_EXPECT_EQUAL( tableClient._callTable.getCount(), size_t( 0 ) ); // 남은 것 없음 — 늦게 오는 응답이 다른 델리게이트를 꺼내지 않는다
}

SW_TEST_CASE( ServiceClientCallTableTest, ResponsesFindTheirOwnDelegateOverLoopback )
{
    LoopbackStreamNetwork   network( 5u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    server.start();
    test::OnlineTestClients clients( network );
    const int32             clientIndex = clients.connect( server._port, {} );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_TRUE( clients.getClient( clientIndex ).isReady() );

    CallTableClient tableClient;
    tableClient._pClient = &clients.getClient( clientIndex );
    DoneRecorder goodRecorder;
    DoneRecorder badRecorder;
    BitWriter    goodBody;
    goodBody.writeVarUint( 42 );
    BitWriter badBody;
    badBody.writeVarUint( 0 ); // 계정 0 — 테스트 로그인이 거절한다
    (void)tableClient.send( test::TestLoginService::kLoginMethod, badBody, DoneDelegate::create<&DoneRecorder::onDone>( &badRecorder ) );
    (void)tableClient.send( test::TestLoginService::kLoginMethod, goodBody, DoneDelegate::create<&DoneRecorder::onDone>( &goodRecorder ) );
    SW_EXPECT_EQUAL( tableClient._callTable.getCount(), size_t( 2 ) );
    test::tickAll( { &server }, clients, 0 );

    SW_ASSERT_EQUAL( badRecorder._listErrorCode.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( badRecorder._listErrorCode[0], OnlineError::kInvalidRequest );
    SW_ASSERT_EQUAL( goodRecorder._listErrorCode.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( goodRecorder._listErrorCode[0], OnlineError::kOk );
    SW_EXPECT_EQUAL( tableClient._callTable.getCount(), size_t( 0 ) );
    StreamConnectionHandle connection;
    SW_EXPECT_TRUE( server._host.findConnection( 42, connection ) );
}

/**
 * @brief [ServiceClientCallTableTest] 보내는 중에 온 다른 id 의 응답은 보내는 중인 델리게이트를 가져가지 않는다.
 * @details 예전 표는 id 를 대조하지 않고 보내는 중이면 무엇이든 내줘, 늦게 온 다른 요청의 응답이 이 요청의 콜백을 불렀다.
 */
SW_TEST_CASE( ServiceClientCallTableTest, ForeignResponseDuringSendDoesNotTakeTheSendingDelegate )
{
    ServiceClientCallTable<DoneDelegate> table;
    ForeignFirstClient                   client;
    client._pTable = &table;
    DoneRecorder recorder;
    const uint64 requestID = table.send( client, uint16( 1 ), int32( 0 ), int32( 0 ), DoneDelegate{}, DoneDelegate::create<&DoneRecorder::onDone>( &recorder ) );
    SW_EXPECT_EQUAL( requestID, uint64( 7 ) );
    SW_ASSERT_EQUAL( recorder._listErrorCode.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listErrorCode[0], uint16( 7 ) );
    SW_ASSERT_EQUAL( client._listUnmatchedID.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( client._listUnmatchedID[0], uint64( 107 ) );
    SW_EXPECT_EQUAL( table.getCount(), size_t( 0 ) );
}

SW_TEST_CASE( ServiceClientCallTableTest, CancelledRequestsDropTheirAnswersWithoutCallingTheOwner )
{
    LoopbackStreamNetwork   network( 5u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    server.start();
    test::OnlineTestClients clients( network );
    const int32             clientIndex = clients.connect( server._port, {} );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_TRUE( clients.getClient( clientIndex ).isReady() );

    // 키트 클라이언트가 응답 전에 사라진 모양 — 보낸 요청의 델리게이트 주인을 지운 뒤 응답이 온다.
    unique_ptr<CallTableClient> tableClient = sw::make_unique<CallTableClient>();
    tableClient->_pClient                   = &clients.getClient( clientIndex );
    DoneRecorder recorder;
    BitWriter    body;
    body.writeVarUint( 42 );
    (void)tableClient->send( test::TestLoginService::kLoginMethod, body, DoneDelegate::create<&DoneRecorder::onDone>( &recorder ) );
    clients.getClient( clientIndex ).cancelRequestsOf( OnlineMethodRange::kGame );
    tableClient.reset();
    test::tickAll( { &server }, clients, 0 );

    SW_EXPECT_EQUAL( recorder._listErrorCode.size(), size_t( 0 ) );
    StreamConnectionHandle connection;
    SW_EXPECT_TRUE( server._host.findConnection( 42, connection ) ); // 서버는 처리했고 응답만 버려졌다
}

SW_TEST_CASE( ServiceClientCallTableTest, KitAndServiceClientMayBeDestroyedInEitherOrder )
{
    {
        OnlineServiceClient client;
        {
            LifetimeClientService kit;
            SW_EXPECT_TRUE( client.registerClientService( &kit ) );
        } // 키트가 먼저 — 소멸자가 스스로 내린다
        LifetimeClientService nextKit;
        SW_EXPECT_TRUE( client.registerClientService( &nextKit ) ); // 같은 영역을 다시 올릴 수 있다 — 앞 키트가 목록에서 빠졌다
    }
    {
        LifetimeClientService kit;
        {
            OnlineServiceClient client;
            SW_EXPECT_TRUE( client.registerClientService( &kit ) );
        } // 서비스 클라이언트가 먼저 — 키트 소멸자가 파괴된 객체를 부르지 않는다
    }
}
