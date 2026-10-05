#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Network/Transport/LoopbackStreamTransport.h"
#include "Core/Network/Transport/StreamSendQueue.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/TestFramework.h"

#include <mutex>
#include <thread>

// 스트림 전송의 계약 — 열기 · 순서대로 받기 · 우아한 종료(양쪽 닫힘 까닭) · 끊기(Reset) · 연결 실패 · 낡은 핸들 · 보낼 줄 물금 · 상한 · 읽기 멈춤 뒤 다시 받기.
// 같은 본문을 루프백(결정적)과 플랫폼 전송(IOCP · epoll, T2 · T3)이 지난다.

using namespace sw;

namespace
{
    /** @brief 받은 사건을 모으는 처리기 — I/O 스레드가 부르므로 잠금. */
    class StreamRecorder final : public IStreamHandler
    {
    public:
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            (void)remote;
            _listOpened.push_back( handle );
            _bLastAccepted = bAccepted ? SW_TRUE : SW_FALSE;
        }
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            (void)handle;
            _receivedBytes.insert( _receivedBytes.end(), pData, pData + size );
        }
        void onStreamWritable( StreamConnectionHandle handle ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            (void)handle;
            ++_writableCount;
        }
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            std::scoped_lock<mutex> lock{ _mutex };
            (void)handle;
            _listClosedReason.push_back( reason );
        }

        int32 getOpenedCount() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return static_cast<int32>( _listOpened.size() );
        }
        StreamConnectionHandle getOpened( int32 index ) const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _listOpened[static_cast<size_t>( index )];
        }
        int32 getReceivedCount() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return static_cast<int32>( _receivedBytes.size() );
        }
        vector<uint8> copyReceived() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _receivedBytes;
        }
        int32 getClosedCount() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return static_cast<int32>( _listClosedReason.size() );
        }
        StreamCloseReason getClosedReason( int32 index ) const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _listClosedReason[static_cast<size_t>( index )];
        }
        int32 getWritableCount() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            return _writableCount;
        }

    private:
        mutable mutex                  _mutex;
        vector<StreamConnectionHandle> _listOpened;
        vector<StreamCloseReason>      _listClosedReason;
        vector<uint8>                  _receivedBytes;
        int32                          _writableCount{ 0 };
        uint8                          _bLastAccepted{ SW_FALSE };
    };

    /** @brief 서버 · 클라이언트 전송 한 쌍과 "조건이 설 때까지 돌리기" 입니다. */
    struct StreamRig
    {
        unique_ptr<LoopbackStreamNetwork> _network{};
        unique_ptr<IStreamTransport>      _server{};
        unique_ptr<IStreamTransport>      _client{};
        StreamRecorder                    _serverRecorder{};
        StreamRecorder                    _clientRecorder{};
        uint8                             _bManualPoll{ SW_TRUE };

        static bool makeLoopback( StreamRig& outRig, const StreamTransportSettings& baseSettings, const LoopbackStreamConditions& conditions )
        {
            outRig._network = make_unique<LoopbackStreamNetwork>( 7u );
            outRig._network->setConditions( conditions );
            outRig._server                   = outRig._network->createTransport();
            outRig._client                   = outRig._network->createTransport();
            StreamTransportSettings settings = baseSettings;
            settings._ioThreadCount          = 0;
            outRig._bManualPoll              = SW_TRUE;
            return outRig._server->initialize( &outRig._serverRecorder, settings ) && outRig._client->initialize( &outRig._clientRecorder, settings );
        }

        /** @brief @p predicate 가 설 때까지(최대 @p timeoutMilli) 두 전송을 돕니다. */
        template <typename TPredicate>
        bool runUntil( TPredicate&& predicate, int64 timeoutMilli = 3000 )
        {
            const Deadline deadline = Deadline::afterMilliseconds( timeoutMilli );
            while ( predicate() == false )
            {
                if ( deadline.isExpired() )
                    return false;
                if ( _bManualPoll == SW_TRUE )
                {
                    (void)_server->pollIo( 0 );
                    (void)_client->pollIo( 0 );
                }
                else
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
            }
            return true;
        }

        void shutdown()
        {
            _client->shutdown();
            _server->shutdown();
        }
    };

    vector<uint8> makePattern( int32 size, uint32 seed )
    {
        vector<uint8> bytes( static_cast<size_t>( size ) );
        for ( int32 index = 0; index < size; ++index )
        {
            seed                                = seed * 1664525u + 1013904223u;
            bytes[static_cast<size_t>( index )] = static_cast<uint8>( seed >> 24 );
        }
        return bytes;
    }

    // ---- 계약 본문 — 장치만 바꿔 루프백 · 플랫폼이 같이 지난다 ----

    void runOpenSendGracefulClose( StreamRig& rig )
    {
        SW_ASSERT_TRUE( rig._server->listen( NetAddress::makeLoopback( 0 ) ) );
        const StreamConnectionHandle clientHandle = rig._client->connect( NetAddress::makeLoopback( rig._server->getListenPort() ) );
        SW_ASSERT_TRUE( clientHandle.isValid() );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getOpenedCount() == 1 && rig._clientRecorder.getOpenedCount() == 1; } ) );

        // 세 덩어리(작은 것 · 64 KB 경계를 넘는 것 · 중간)가 순서 그대로 한 줄로 온다.
        const vector<uint8> payload = makePattern( 150000, 3u );
        SW_EXPECT_TRUE( rig._client->send( clientHandle, payload.data(), 10 ) != StreamSendResult::Closed );
        SW_EXPECT_TRUE( rig._client->send( clientHandle, payload.data() + 10, 100000 ) != StreamSendResult::Closed );
        SW_EXPECT_TRUE( rig._client->send( clientHandle, payload.data() + 100010, 49990 ) != StreamSendResult::Closed );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getReceivedCount() == 150000; } ) );
        SW_EXPECT_TRUE( rig._serverRecorder.copyReceived() == payload );

        // 서버의 답과 클라이언트의 우아한 종료 — 답은 닫힘 전에 다 닿는다.
        const StreamConnectionHandle serverHandle = rig._serverRecorder.getOpened( 0 );
        SW_EXPECT_TRUE( rig._server->send( serverHandle, payload.data(), 5000 ) != StreamSendResult::Closed );
        rig._client->close( clientHandle, StreamCloseMode::Graceful );
        SW_EXPECT_TRUE( rig._client->send( clientHandle, payload.data(), 1 ) == StreamSendResult::Closed ); // 닫는 중에는 더 쌓지 않는다
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getClosedCount() == 1 && rig._clientRecorder.getClosedCount() == 1; } ) );
        SW_EXPECT_EQUAL( 5000, rig._clientRecorder.getReceivedCount() );
        SW_EXPECT_TRUE( rig._clientRecorder.getClosedReason( 0 ) == StreamCloseReason::LocalClose );
        SW_EXPECT_TRUE( rig._serverRecorder.getClosedReason( 0 ) == StreamCloseReason::RemoteClose );

        // 낡은 핸들은 아무것도 가리키지 않는다.
        SW_EXPECT_TRUE( rig._client->send( clientHandle, payload.data(), 1 ) == StreamSendResult::Closed );
        rig.shutdown();
    }

    void runAbortResetsPeer( StreamRig& rig )
    {
        SW_ASSERT_TRUE( rig._server->listen( NetAddress::makeLoopback( 0 ) ) );
        const StreamConnectionHandle clientHandle = rig._client->connect( NetAddress::makeLoopback( rig._server->getListenPort() ) );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getOpenedCount() == 1 && rig._clientRecorder.getOpenedCount() == 1; } ) );
        rig._client->close( clientHandle, StreamCloseMode::Abort );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getClosedCount() == 1 && rig._clientRecorder.getClosedCount() == 1; } ) );
        SW_EXPECT_TRUE( rig._clientRecorder.getClosedReason( 0 ) == StreamCloseReason::LocalClose );
        SW_EXPECT_TRUE( rig._serverRecorder.getClosedReason( 0 ) == StreamCloseReason::Reset );
        rig.shutdown();
    }

    void runConnectToClosedPortFails( StreamRig& rig )
    {
        // 리슨을 열지 않은 서버의 포트 — 루프백은 등록부에 없고, 플랫폼은 RST(ECONNREFUSED · WSAECONNREFUSED).
        SW_ASSERT_TRUE( rig._server->listen( NetAddress::makeLoopback( 0 ) ) );
        const uint16 port = rig._server->getListenPort();
        rig._server->shutdown();
        const StreamConnectionHandle handle = rig._client->connect( NetAddress::makeLoopback( port ) );
        SW_ASSERT_TRUE( handle.isValid() );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._clientRecorder.getClosedCount() == 1; }, 12000 ) );
        SW_EXPECT_TRUE( rig._clientRecorder.getClosedReason( 0 ) == StreamCloseReason::ConnectFailed );
        SW_EXPECT_EQUAL( 0, rig._clientRecorder.getOpenedCount() );
        rig._client->shutdown();
    }

    /** @brief 받는 쪽이 읽기를 멈추면 보낸 쪽 줄이 높은 물금 → 상한(QueueFull)까지 차고, 다시 읽으면 낮은 물금 아래에서 `onStreamWritable` 이 온다. */
    void runBackpressure( StreamRig& rig )
    {
        SW_ASSERT_TRUE( rig._server->listen( NetAddress::makeLoopback( 0 ) ) );
        const StreamConnectionHandle clientHandle = rig._client->connect( NetAddress::makeLoopback( rig._server->getListenPort() ) );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getOpenedCount() == 1 && rig._clientRecorder.getOpenedCount() == 1; } ) );
        const StreamConnectionHandle serverHandle = rig._serverRecorder.getOpened( 0 );
        rig._server->setReceivePaused( serverHandle, true );

        // 플랫폼 전송은 커널 버퍼(수 MB)가 먼저 찬다 — 상한까지 쌓을 때까지 보내며 돈다.
        const vector<uint8> chunk     = makePattern( 32 * 1024, 9u );
        int32               sentBytes = 0;
        bool                bSawHigh  = false;
        bool                bSawFull  = false;
        const Deadline      deadline  = Deadline::afterMilliseconds( 10000 );
        while ( bSawFull == false && deadline.isExpired() == false )
        {
            const StreamSendResult result = rig._client->send( clientHandle, chunk.data(), static_cast<int32>( chunk.size() ) );
            if ( result == StreamSendResult::QueueFull )
            {
                bSawFull = true;
                break;
            }
            sentBytes += static_cast<int32>( chunk.size() );
            bSawHigh = bSawHigh || result == StreamSendResult::QueuedAboveHighWatermark;
            (void)rig.runUntil( []()
            { return false; }, 1 ); // 한 바퀴
        }
        SW_EXPECT_TRUE( bSawHigh );
        SW_ASSERT_TRUE( bSawFull );

        rig._server->setReceivePaused( serverHandle, false );
        SW_ASSERT_TRUE( rig.runUntil( [&]()
        { return rig._serverRecorder.getReceivedCount() == sentBytes; }, 20000 ) );
        SW_EXPECT_TRUE( rig._clientRecorder.getWritableCount() >= 1 );
        rig.shutdown();
    }
} // namespace

SW_TEST_CASE( StreamTransportTest, LoopbackOpensSendsInOrderAndClosesGracefully )
{
    StreamRig rig;
    SW_ASSERT_TRUE( StreamRig::makeLoopback( rig, StreamTransportSettings{}, LoopbackStreamConditions{ 777, 0 } ) ); // 1..777 조각으로 쪼개 받는다
    runOpenSendGracefulClose( rig );
}

SW_TEST_CASE( StreamTransportTest, LoopbackAbortResetsPeer )
{
    StreamRig rig;
    SW_ASSERT_TRUE( StreamRig::makeLoopback( rig, StreamTransportSettings{}, LoopbackStreamConditions{} ) );
    runAbortResetsPeer( rig );
}

SW_TEST_CASE( StreamTransportTest, LoopbackConnectToClosedPortFails )
{
    StreamRig rig;
    SW_ASSERT_TRUE( StreamRig::makeLoopback( rig, StreamTransportSettings{}, LoopbackStreamConditions{} ) );
    runConnectToClosedPortFails( rig );
}

SW_TEST_CASE( StreamTransportTest, LoopbackBackpressureFillsThenDrains )
{
    StreamTransportSettings settings;
    settings._sendHighWatermarkBytes = 64 * 1024;
    settings._sendLowWatermarkBytes  = 16 * 1024;
    settings._maxQueuedSendBytes     = 256 * 1024;
    StreamRig rig;
    SW_ASSERT_TRUE( StreamRig::makeLoopback( rig, settings, LoopbackStreamConditions{ 0, 8 * 1024 } ) );
    runBackpressure( rig );
}

/**
 * @brief [StreamTransportTest] 보낼 줄 — 64 KB 덩어리 경계를 넘는 쌓기 · 구간 꺼내기 · 물금 판정(넘을 때 · 내려올 때 한 번) · 상한
 */
SW_TEST_CASE( StreamTransportTest, SendQueueTracksWatermarksAcrossChunks )
{
    StreamSendQueue queue;
    queue.configure( 100000, 20000, 200000 );
    const vector<uint8> bytes = makePattern( 70000, 1u );
    SW_EXPECT_TRUE( queue.append( bytes.data(), 70000 ) == StreamSendResult::Queued );
    SW_EXPECT_TRUE( queue.append( bytes.data(), 70000 ) == StreamSendResult::QueuedAboveHighWatermark );
    SW_EXPECT_TRUE( queue.append( bytes.data(), 70000 ) == StreamSendResult::QueueFull ); // 210 000 > 200 000 — 쌓지 않는다
    SW_EXPECT_EQUAL( 140000, queue.getQueuedBytes() );

    StreamSendSpan arrSpan[4];
    const int32    spanCount = queue.collectSpans( arrSpan, 4, 1 << 30 );
    int32          total     = 0;
    for ( int32 index = 0; index < spanCount; ++index )
        total += arrSpan[index]._size;
    SW_EXPECT_EQUAL( 140000, total );
    SW_EXPECT_TRUE( arrSpan[0]._pData[0] == bytes[0] && arrSpan[1]._pData[0] == bytes[static_cast<size_t>( StreamSendQueue::kChunkBytes )] );

    SW_EXPECT_FALSE( queue.consume( 100000 ) ); // 40 000 — 아직 낮은 물금 위
    SW_EXPECT_TRUE( queue.consume( 25000 ) );   // 15 000 — 내려왔다(한 번)
    SW_EXPECT_FALSE( queue.consume( 15000 ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}
