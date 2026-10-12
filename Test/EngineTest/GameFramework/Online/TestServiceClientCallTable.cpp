// 클라이언트 호출 표 — 응답이 사용자 델리게이트를 한 번 찾는다. sendRequest 가 그 자리에서 실패를 알려도(초기화 전 · 끊김) 델리게이트를 잃지 않는다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

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
} // namespace

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
