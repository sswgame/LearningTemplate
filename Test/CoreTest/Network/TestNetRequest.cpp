#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogContext.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestStreamEndpointPair.h"

#include <thread>

// 서비스 요청-응답 — 응답 · 모르는 메서드, 시한(늦은 응답은 무시), 취소, 재접속 뒤 같은 멱등 키는 한 번만 처리, 처리 중인 같은 키는 첫 응답을 같이,
// 연결별 처리 중 상한(Overloaded) · 연결 끊김(ConnectionLost), 요청 머리의 추적 id 와 처리기의 로그 문맥. 루프백 위의 두 끝점을 한 스레드로 돈다.

using namespace sw;

namespace
{
    struct ResponseLog
    {
        vector<NetRequestStatus> _listStatus{};
        vector<vector<uint8>>    _listBody{};

        void onResponse( const NetResponse& response )
        {
            _listStatus.push_back( response._status );
            _listBody.emplace_back( response._pBody, response._pBody + response._bodySize );
        }

        Delegate<void( const NetResponse& )> makeCallback() { return Delegate<void( const NetResponse& )>::create<&ResponseLog::onResponse>( this ); }
    };

    /** @brief 메서드 1 = 바로 메아리, 2 = 토큰을 들고 나중에(시험이 respond). 처리 횟수를 센다. */
    struct EchoService final : public INetRequestHandler
    {
        vector<NetRequestToken> _listDeferred{};
        vector<LogTraceID>      _listTraceID{};        ///< 요청 머리의 추적 id(받은 순)
        vector<LogContext>      _listHandlerContext{}; ///< 처리기 안의 로그 문맥(받은 순)
        int32                   _callCount{ 0 };

        void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) override
        {
            ++_callCount;
            _listTraceID.push_back( context._traceID );
            _listHandlerContext.push_back( LogContext::getCurrent() );
            if ( context._token._method == 1 )
                (void)server.respond( context._token, NetRequestStatus::Ok, context._pBody, context._bodySize );
            else
                _listDeferred.push_back( context._token );
        }
    };

    /** @brief 서버 쪽 리스너 — 프레임은 `NetRequestServer`, 닫힘은 `onConnectionClosed`. 열린 연결에 주체를 붙일 수 있다. */
    struct ServerSide final : public IStreamEndpointListener
    {
        NetRequestServer               _server{};
        vector<StreamConnectionHandle> _listOpened{};
        uint64                         _principalID{ 0 };

        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            _listOpened.push_back( handle );
            if ( _principalID != 0 )
                _server.setPrincipal( handle, _principalID );
        }
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            (void)_server.handleFrame( handle, kind, pBody, bodySize );
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)reason;
            _server.onConnectionClosed( handle );
        }
    };

    /** @brief 클라이언트 쪽 리스너 — 프레임은 `NetRequestClient`, 닫힘은 `onConnectionClosed`. */
    struct ClientSide final : public IStreamEndpointListener
    {
        NetRequestClient _client{};
        int32            _openedCount{ 0 };

        void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)handle;
            (void)remote;
            (void)bAccepted;
            ++_openedCount;
        }
        void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
        {
            (void)_client.handleFrame( handle, kind, pBody, bodySize );
        }
        void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)reason;
            _client.onConnectionClosed( handle );
        }
    };

    /** @brief 요청 서버 · 클라이언트를 끝점 한 쌍에 묶습니다. 리스너가 끝점 쌍보다 먼저 선언되어 오래 산다. */
    struct RequestRig
    {
        EchoService              _echo{};
        ServerSide               _serverSide{};
        ClientSide               _clientSide{};
        test::StreamEndpointPair _pair;

        explicit RequestRig( const NetRequestServerSettings& serverSettings = NetRequestServerSettings{}, uint64 principalID = 0 )
            : _pair{ _serverSide, _clientSide, StreamEndpointSettings{}, LoopbackStreamConditions{} }
        {
            _serverSide._principalID = principalID;
            _serverSide._server.initialize( &_pair._server, serverSettings );
            SW_EXPECT_TRUE( _serverSide._server.registerMethod( 1, &_echo ) );
            SW_EXPECT_TRUE( _serverSide._server.registerMethod( 2, &_echo ) );
            _clientSide._client.initialize( &_pair._client );
            _pair.step( 2 );
        }

        uint64 send( StreamConnectionHandle handle, uint16 method, const vector<uint8>& bodyBytes, const NetRequestOptions& options, ResponseLog& log )
        {
            return _clientSide._client.sendRequest( handle, method, bodyBytes.data(), static_cast<int32>( bodyBytes.size() ), options, log.makeCallback() );
        }
    };
} // namespace

SW_TEST_CASE( NetRequestTest, ResponseMatchesRequestAndUnknownMethodIsRefused )
{
    RequestRig rig;
    SW_ASSERT_EQUAL( 1, rig._clientSide._openedCount );
    ResponseLog echoLog;
    ResponseLog unknownLog;
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 1, vector<uint8>{ 1, 2, 3 }, NetRequestOptions{}, echoLog ) != 0 );
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 9, vector<uint8>{ 4 }, NetRequestOptions{}, unknownLog ) != 0 );
    rig._pair.step( 4 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( echoLog._listStatus.size() ) );
    SW_EXPECT_TRUE( echoLog._listStatus[0] == NetRequestStatus::Ok );
    SW_EXPECT_TRUE( echoLog._listBody[0] == ( vector<uint8>{ 1, 2, 3 } ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( unknownLog._listStatus.size() ) );
    SW_EXPECT_TRUE( unknownLog._listStatus[0] == NetRequestStatus::UnknownMethod );
    SW_EXPECT_EQUAL( 0, rig._clientSide._client.getPendingCount() );
}

SW_TEST_CASE( NetRequestTest, DeadlineEndsRequestAndLateResponseIsIgnored )
{
    RequestRig        rig;
    ResponseLog       log;
    NetRequestOptions options;
    options._timeoutSeconds = 0.02;
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 2, vector<uint8>{ 5 }, options, log ) != 0 );
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( rig._echo._listDeferred.size() ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    rig._clientSide._client.update();
    SW_ASSERT_EQUAL( 1, static_cast<int32>( log._listStatus.size() ) );
    SW_EXPECT_TRUE( log._listStatus[0] == NetRequestStatus::DeadlineExceeded );

    SW_EXPECT_TRUE( rig._serverSide._server.respond( rig._echo._listDeferred[0], NetRequestStatus::Ok, nullptr, 0 ) );
    rig._pair.step( 4 );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( log._listStatus.size() ) ); // 늦은 응답은 콜백을 다시 부르지 않는다
}

SW_TEST_CASE( NetRequestTest, CancelCompletesLocallyAndMarksServerRequest )
{
    RequestRig   rig;
    ResponseLog  log;
    const uint64 requestID = rig.send( rig._pair._clientHandle, 2, vector<uint8>{}, NetRequestOptions{}, log );
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( rig._echo._listDeferred.size() ) );
    SW_EXPECT_TRUE( rig._clientSide._client.cancel( requestID ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( log._listStatus.size() ) );
    SW_EXPECT_TRUE( log._listStatus[0] == NetRequestStatus::Cancelled );
    SW_EXPECT_FALSE( rig._clientSide._client.cancel( requestID ) ); // 이미 끝났다
    rig._pair.step( 2 );
    SW_EXPECT_TRUE( rig._serverSide._server.isCancelled( rig._echo._listDeferred[0] ) );
}

/**
 * @brief [NetRequestTest] 주체가 같으면 끊고 다시 붙은 연결의 같은 멱등 키는 처리하지 않고 기억한 응답을 받는다 — 키가 없으면 다시 처리한다
 */
SW_TEST_CASE( NetRequestTest, IdempotentRetryAfterReconnectIsProcessedOnce )
{
    RequestRig                   rig( NetRequestServerSettings{}, 42 );
    const StreamConnectionHandle first = rig._pair._clientHandle;
    NetRequestOptions            options;
    options._idempotencyKey = NetIdempotencyKey{ 0x1234u, 0x5678u };

    ResponseLog lostLog;
    SW_EXPECT_TRUE( rig.send( first, 2, vector<uint8>{ 9 }, options, lostLog ) != 0 );
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 1, rig._echo._callCount );
    rig._pair._client.close( first, StreamCloseMode::Abort ); // 응답 전에 끊긴다
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( lostLog._listStatus.size() ) );
    SW_EXPECT_TRUE( lostLog._listStatus[0] == NetRequestStatus::ConnectionLost );

    const uint8 arrLateBody[1] = { 7 };
    SW_EXPECT_TRUE( rig._serverSide._server.respond( rig._echo._listDeferred[0], NetRequestStatus::Ok, arrLateBody, 1 ) ); // 늦은 응답 — 기억된다

    const StreamConnectionHandle second = rig._pair._client.connect( NetAddress::makeLoopback( rig._pair._serverTransport->getListenPort() ) );
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 2, static_cast<int32>( rig._serverSide._listOpened.size() ) );
    ResponseLog retryLog;
    SW_EXPECT_TRUE( rig.send( second, 2, vector<uint8>{ 9 }, options, retryLog ) != 0 );
    rig._pair.step( 4 );
    SW_EXPECT_EQUAL( 1, rig._echo._callCount );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( retryLog._listStatus.size() ) );
    SW_EXPECT_TRUE( retryLog._listStatus[0] == NetRequestStatus::Ok );
    SW_EXPECT_TRUE( retryLog._listBody[0] == ( vector<uint8>{ 7 } ) );

    ResponseLog freshLog;
    SW_EXPECT_TRUE( rig.send( second, 2, vector<uint8>{ 9 }, NetRequestOptions{}, freshLog ) != 0 );
    rig._pair.step( 2 );
    SW_EXPECT_EQUAL( 2, rig._echo._callCount ); // 키가 없으면 다른 요청이다
}

SW_TEST_CASE( NetRequestTest, InFlightDuplicateKeyWaitsForFirstResponse )
{
    RequestRig        rig;
    NetRequestOptions options;
    options._idempotencyKey = NetIdempotencyKey{ 7u, 8u };
    ResponseLog firstLog;
    ResponseLog secondLog;
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 2, vector<uint8>{}, options, firstLog ) != 0 );
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 2, vector<uint8>{}, options, secondLog ) != 0 );
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 1, rig._echo._callCount );
    const uint8 arrBody[2] = { 3, 4 };
    SW_EXPECT_TRUE( rig._serverSide._server.respond( rig._echo._listDeferred[0], NetRequestStatus::Ok, arrBody, 2 ) );
    rig._pair.step( 4 );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( firstLog._listStatus.size() ) );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( secondLog._listStatus.size() ) );
    SW_EXPECT_TRUE( firstLog._listStatus[0] == NetRequestStatus::Ok && secondLog._listStatus[0] == NetRequestStatus::Ok );
    SW_EXPECT_TRUE( secondLog._listBody[0] == ( vector<uint8>{ 3, 4 } ) );
}

SW_TEST_CASE( NetRequestTest, ConnectionLossAndOverloadComplete )
{
    NetRequestServerSettings settings;
    settings._maxInFlightPerConnection = 2;
    RequestRig  rig( settings );
    ResponseLog log;
    for ( int32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 2, vector<uint8>{}, NetRequestOptions{}, log ) != 0 );
    }
    rig._pair.step( 4 );
    SW_EXPECT_EQUAL( 2, rig._echo._callCount );
    SW_ASSERT_EQUAL( 1, static_cast<int32>( log._listStatus.size() ) );
    SW_EXPECT_TRUE( log._listStatus[0] == NetRequestStatus::Overloaded );

    SW_ASSERT_EQUAL( 1, static_cast<int32>( rig._serverSide._listOpened.size() ) );
    rig._pair._server.close( rig._serverSide._listOpened[0], StreamCloseMode::Abort );
    rig._pair.step( 2 );
    SW_ASSERT_EQUAL( 3, static_cast<int32>( log._listStatus.size() ) );
    SW_EXPECT_TRUE( log._listStatus[1] == NetRequestStatus::ConnectionLost && log._listStatus[2] == NetRequestStatus::ConnectionLost );
    SW_EXPECT_EQUAL( 0, rig._clientSide._client.getPendingCount() );
}

SW_TEST_CASE( NetRequestTest, TraceIDTravelsInTheHeadAndWrapsTheHandler )
{
    RequestRig  rig( NetRequestServerSettings{}, 7 );
    ResponseLog log;
    LogContext  caller;
    caller._traceID = LogTraceID{ 0xAB, 0xCD };
    {
        ScopedLogContext scope( caller ); // 보내는 스레드의 문맥 — 옵션이 비면 이것이 실린다
        SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 1, vector<uint8>{ 1 }, NetRequestOptions{}, log ) != 0 );
        NetRequestOptions explicitOptions;
        explicitOptions._traceID = LogTraceID{ 0x1, 0x2 }; // 옵션에 적은 것이 이긴다
        SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 1, vector<uint8>{ 2 }, explicitOptions, log ) != 0 );
    }
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 1, vector<uint8>{ 3 }, NetRequestOptions{}, log ) != 0 ); // 아무 데도 없으면 서버가 만든다
    SW_EXPECT_TRUE( rig.send( rig._pair._clientHandle, 1, vector<uint8>{ 4 }, NetRequestOptions{}, log ) != 0 );
    rig._pair.step( 4 ); // 문맥 밖에서 돈다 — 처리기의 문맥은 서버가 건 것이다
    SW_ASSERT_EQUAL( 4, static_cast<int32>( rig._echo._listTraceID.size() ) );
    SW_EXPECT_TRUE( rig._echo._listTraceID[0] == caller._traceID );
    SW_EXPECT_TRUE( rig._echo._listTraceID[1] == ( LogTraceID{ 0x1, 0x2 } ) );
    SW_EXPECT_TRUE( rig._echo._listTraceID[2].isValid() );
    SW_EXPECT_TRUE( rig._echo._listTraceID[2] != rig._echo._listTraceID[3] );
    for ( int32 index = 0; index < 4; ++index )
    {
        const LogContext& handlerContext = rig._echo._listHandlerContext[static_cast<size_t>( index )];
        SW_EXPECT_TRUE( handlerContext._traceID == rig._echo._listTraceID[static_cast<size_t>( index )] );
        SW_EXPECT_EQUAL( handlerContext._principalID, uint64( 7 ) );
    }
    SW_EXPECT_TRUE( LogContext::getCurrent().isEmpty() );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( log._listStatus.size() ) );
}
