/**
 * @file StreamTypes.h
 * @brief 스트림(TCP) 전송의 공통 타입 — 연결 핸들 · 닫힘 까닭 · 보내기 결과 · 설정 · 통계입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /**
     * @struct StreamConnectionHandle
     * @brief 스트림 연결 하나 — 자리 번호 + 세대입니다. 세대 0 은 무효. 닫힌 자리를 다시 쓰면 세대가 올라 옛 핸들은 아무것도 가리키지 않습니다.
     */
    struct StreamConnectionHandle
    {
        uint32 _index{ 0 };
        uint32 _generation{ 0 };

        static constexpr StreamConnectionHandle make( uint32 index, uint32 generation ) { return StreamConnectionHandle{ index, generation }; }
        static constexpr StreamConnectionHandle fromPacked( uint64 packed ) { return make( static_cast<uint32>( packed ), static_cast<uint32>( packed >> 32 ) ); }

        constexpr bool   isValid() const { return _generation != 0; }
        constexpr uint64 packed() const { return ( static_cast<uint64>( _generation ) << 32 ) | _index; }

        constexpr bool operator==( const StreamConnectionHandle& other ) const { return _index == other._index && _generation == other._generation; }
        constexpr bool operator!=( const StreamConnectionHandle& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 스트림 연결이 닫힌 까닭입니다. */
    enum class StreamCloseReason : uint8
    {
        None = 0,
        LocalClose,        ///< 이쪽이 닫았다(우아한 종료가 끝났거나 끊었다)
        RemoteClose,       ///< 저쪽이 닫았다(FIN)
        Reset,             ///< 저쪽이 끊었다(RST) · 소켓 오류
        IdleTimeout,       ///< `_idleTimeoutSeconds` 동안 받은 바이트가 없다
        ConnectFailed,     ///< 연결하지 못했다(거절 · 시한 · 주소)
        ProtocolError,     ///< 프레임이 상한을 넘거나 깨졌다(위 층이 닫았다)
        SendQueueOverflow, ///< 보낼 줄이 `_maxQueuedSendBytes` 를 넘었다(느린 상대)
        SecurityFailure,   ///< TLS 핸드셰이크 · 레코드 검증 실패
        Shutdown           ///< 전송을 내렸다
    };

    SW_API const utf8* toString( StreamCloseReason reason );

    /** @brief 닫는 방식입니다. */
    enum class StreamCloseMode : uint8
    {
        Graceful = 0, ///< 쌓인 것을 다 보내고 FIN, 저쪽 FIN(또는 `_closeLingerSeconds`)을 기다린다
        Abort         ///< 바로 RST — 쌓인 것은 버린다
    };

    /** @brief `send` 의 결과입니다. */
    enum class StreamSendResult : uint8
    {
        Queued = 0,               ///< 쌓았다
        QueuedAboveHighWatermark, ///< 쌓았지만 줄이 높은 물금을 넘었다 — `onStreamWritable` 까지 큰 것은 미룬다
        QueueFull,                ///< 쌓지 않았다(`_maxQueuedSendBytes`) — 위 층은 보통 닫는다(SendQueueOverflow)
        Closed                    ///< 없는 · 닫히는 연결
    };

    /** @brief 스트림 전송 설정입니다. 시간은 초, 크기는 바이트입니다. */
    struct StreamTransportSettings
    {
        int32   _ioThreadCount{ 1 };                    ///< 0 = 전용 스레드 없음 — 부르는 쪽이 `pollIo` 를 돈다
        int32   _maxConnections{ 1024 };                ///< 넘는 수락은 바로 닫는다
        int32   _receiveChunkBytes{ 16 * 1024 };        ///< 읽기 한 번의 버퍼(연결마다 하나)
        int32   _sendHighWatermarkBytes{ 256 * 1024 };  ///< 줄이 이것을 넘으면 `QueuedAboveHighWatermark`
        int32   _sendLowWatermarkBytes{ 64 * 1024 };    ///< 높은 물금을 넘었던 줄이 이 아래로 내려가면 `onStreamWritable`
        int32   _maxQueuedSendBytes{ 4 * 1024 * 1024 }; ///< 넘으면 `QueueFull`
        int32   _listenBacklog{ 512 };
        float64 _idleTimeoutSeconds{ 60.0 }; ///< 0 = 끈다. 위 층의 핑 간격보다 길게
        float64 _connectTimeoutSeconds{ 10.0 };
        float64 _closeLingerSeconds{ 5.0 }; ///< 우아한 종료가 저쪽 FIN 을 기다리는 상한
        uint8   _bNoDelay{ SW_TRUE };       ///< TCP_NODELAY — 요청-응답은 지연 확인과 Nagle 이 겹치면 40 ms 씩 늦는다
    };
} // namespace sw

namespace sw
{
    /** @brief 전송 통계(누계)입니다. */
    struct StreamTransportStats
    {
        uint64 _acceptedCount{ 0 };
        uint64 _connectedCount{ 0 };
        uint64 _closedCount{ 0 };
        uint64 _receivedBytes{ 0 };
        uint64 _sentBytes{ 0 };
        uint64 _receiveCallCount{ 0 }; ///< 완료된 읽기 수(IOCP 완료 · epoll 읽기)
        uint64 _sendCallCount{ 0 };    ///< 완료된 쓰기 수
        int32  _openCount{ 0 };        ///< 지금 열린 연결
    };
} // namespace sw
