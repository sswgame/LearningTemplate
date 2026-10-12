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
