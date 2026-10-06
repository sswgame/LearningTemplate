/**
 * @file StreamMessageEndpoint.h
 * @brief 스트림 전송 위의 프레임 끝점 — I/O 스레드에서 프레임을 잘라 연결마다의 받은 줄에 쌓고, 게임(서비스) 스레드의 `pump` 가 한 잠금에 꺼내 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/NetCompression.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    class ITlsContext;
} // namespace sw

namespace sw
{
    /** @brief 스트림 끝점의 TLS 설정 — 컨텍스트(인증서 · 신뢰)는 `INetSecurityProvider::createTlsContext` 로 만들어 끝점보다 오래 둔다. 없으면 평문. */
    struct StreamSecuritySettings
    {
        ITlsContext* _pTlsContext{ nullptr };
    };
} // namespace sw

namespace sw
{
    struct StreamEndpointSettings
    {
        int32                  _maxFrameBodySize{ StreamFrameConstant::kDefaultMaxFrameSize };
        int32                  _maxPendingReceiveBytes{ 4 * 1024 * 1024 }; ///< 연결 하나가 `pump` 를 기다리는 바이트 — 넘으면 읽기를 멈춘다
        float64                _pingIntervalSeconds{ 15.0 };               ///< 0 = 끈다. 전송의 유휴 시한보다 짧게
        NetCompressionSettings _compression{};                             ///< 보내는 프레임 몸의 압축(기본 꺼짐). 받는 쪽은 설정과 상관없이 등록부에 있는 코덱이면 푼다
        StreamSecuritySettings _security{};                                ///< TLS 컨텍스트가 있으면 모든 연결이 TLS — 열림은 핸드셰이크 뒤
    };
} // namespace sw

namespace sw
{
    /**
     * @class IStreamEndpointListener
     * @brief `pump` 가 부르는 쪽 — 모두 `pump` 를 부른 스레드에서, 연결마다 열림 → 프레임 → 닫힘 순서입니다.
     * @details 연결 실패처럼 **열린 적 없는 연결도 닫힘은 한 번 온다**(`connect` 한 쪽이 결과를 알도록).
     */
    class SW_API IStreamEndpointListener
    {
    public:
        IStreamEndpointListener()          = default;
        virtual ~IStreamEndpointListener() = default;

        IStreamEndpointListener( const IStreamEndpointListener& )            = default;
        IStreamEndpointListener& operator=( const IStreamEndpointListener& ) = default;

        /** @brief 연결이 열렸습니다(TLS 면 핸드셰이크가 끝난 뒤). */
        virtual void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) = 0;
        /** @brief 프레임 하나(Message · Request · Response · Cancel). @p pBody 는 콜백 동안만 유효합니다. */
        virtual void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) = 0;
        virtual void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason )                                = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class StreamMessageEndpoint
     * @brief 스트림 전송 위의 프레임 끝점 — 전송의 처리기가 되어 I/O 스레드에서 프레임을 잘라 연결마다의 받은 줄에 쌓고, `pump` 가 한 잠금에 꺼내 줍니다.
     * @details 받은 줄이 `_maxPendingReceiveBytes` 를 넘으면 읽기를 멈춘다(TCP 창이 닫혀 상대가 멈춘다). 핑 · 퐁은 끝점이 I/O 스레드에서 스스로 답한다.
     *          보낼 줄이 넘치면 메시지를 버리지 않고 끊는다(SendQueueOverflow) — 버리면 그 위의 순서가 깨진다. 보내기는 아무 스레드에서나.
     *          `_security._pTlsContext` 가 있으면 연결마다 TLS 세션을 두고 모든 바이트가 그것을 지난다 — 열림은 핸드셰이크가 끝난 뒤, 핸드셰이크 · 레코드 검증
     *          실패는 SecurityFailure 로 끊어 열림 없이 닫힘만 간다. 핸드셰이크 중에 보낸 프레임은 세션이 모았다가 끝나면 보낸다. 위 층은 TLS 를 모른다.
     * @code
     *     unique_ptr<IStreamTransport> transport = StreamTransportFactory::createPlatformTransport();
     *     StreamMessageEndpoint endpoint;
     *     (void)endpoint.initialize( transport.get(), endpointSettings );   // 전송의 처리기가 된다
     *     (void)transport->initialize( &endpoint, transportSettings );
     *     (void)transport->listen( NetAddress::makeAnyInterface( 7100 ) );
     *     // 게임 스레드 틱:
     *     endpoint.pump( *this );                                           // onEndpointOpened · onEndpointFrame · onEndpointClosed
     *     (void)endpoint.sendMessage( handle, writer.getBytes().data(), writer.getByteCount() );
     * @endcode
     */
    class SW_API StreamMessageEndpoint final : public IStreamHandler
    {
    public:
        StreamMessageEndpoint();
        ~StreamMessageEndpoint() override;

        StreamMessageEndpoint( const StreamMessageEndpoint& )            = delete;
        StreamMessageEndpoint& operator=( const StreamMessageEndpoint& ) = delete;

        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const StreamEndpointSettings& settings );
        /** @brief 전송을 먼저 내린 뒤(소유자 몫) 부릅니다 — 표를 비웁니다. */
        void shutdown();

        /** @brief 쌓인 사건 · 프레임을 @p listener 에 넘깁니다. 핑도 여기서 돈다. 넘긴 프레임 수입니다. */
        int32 pump( IStreamEndpointListener& listener );

        /** @brief Message 프레임을 보냅니다(아무 스레드). */
        StreamSendResult sendMessage( StreamConnectionHandle handle, const uint8* pBody, int32 bodySize );
        /** @brief 종류를 골라 보냅니다 — `NetRequestClient` · `NetRequestServer` 가 쓴다. */
        StreamSendResult sendFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        /** @brief 연결을 겁니다(클라이언트). 결과는 `pump` 의 열림 · 닫힘으로. */
        StreamConnectionHandle connect( const NetAddress& remote );
        /** @brief 닫습니다 — 우아하면 쌓인 것을 다 보낸 뒤. 리스너가 받는 까닭은 LocalClose. */
        void close( StreamConnectionHandle handle, StreamCloseMode mode );

        IStreamTransport* getTransport() const;
        /** @brief 연결의 RTT(핑으로 잰 마지막 값, 초)입니다. 모르면 −1. */
        float64 getRoundTripSeconds( StreamConnectionHandle handle ) const;

        // IStreamHandler — I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamWritable( StreamConnectionHandle handle ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct Connection;
        struct Event;

        shared_ptr<Connection> findConnection( StreamConnectionHandle handle ) const;
        /** @brief 받은 바이트를 (TLS 면 풀어서) 해독기에 넣고 프레임을 줄에 쌓습니다(I/O 스레드, 연결 잠금 안). 깨졌으면 false. */
        bool acceptIncomingLocked( Connection& connection, const uint8* pData, int32 size );
        /** @brief 평문 바이트를 프레임으로 자릅니다(연결 잠금 안). 깨졌으면 false. */
        bool decodeFramesLocked( Connection& connection, const uint8* pData, int32 size );
        /** @brief 프레임 바이트를 (TLS 면 세션을 지나) 전송에 넘깁니다(연결 잠금 안). */
        StreamSendResult writeOutgoingLocked( Connection& connection, const vector<uint8>& frameBytes );
        /** @brief TLS 세션이 내놓은 암호문(핸드셰이크 · 레코드 · 경고)을 전송에 넘깁니다(연결 잠금 안). */
        StreamSendResult flushCiphertextLocked( Connection& connection );
        void             announceOpenLocked( Connection& connection );
        void             pushEvent( Event&& event, const uint8* pBody, int32 bodySize );
        void             closeForError( Connection& connection, StreamCloseReason reason );

        mutable mutex                                 _tableMutex;
        unordered_map<uint64, shared_ptr<Connection>> _mapConnection; ///< 핸들(packed) → 연결 — I/O 스레드가 열고 닫는다
        mutable mutex                                 _inboxMutex;
        vector<Event>                                 _listInbox;   ///< I/O 스레드가 쌓는다
        vector<uint8>                                 _inboxBytes;  ///< 프레임 몸 아레나
        vector<Event>                                 _listPumping; ///< pump 스레드 전용 — 맞바꿔 꺼낸다
        vector<uint8>                                 _pumpingBytes;
        unordered_map<uint64, int64>                  _mapLastPingTime; ///< pump 스레드 전용 — 열린 연결의 마지막 핑(나노초)
        StreamEndpointSettings                        _settings;
        IStreamTransport*                             _pTransport;
    };
} // namespace sw
