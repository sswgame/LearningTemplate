/**
 * @file StreamMessageEndpoint.h
 * @brief 스트림 전송 위에서 프레임을 주고받는 엔드포인트를 정의합니다. I/O 스레드에서 프레임을 잘라 연결별 수신 큐에 쌓고, 게임(서비스) 스레드의 `pump` 가 한 번의 잠금으로 꺼내 줍니다.
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
    class ITLSContext;
} // namespace sw

namespace sw
{
    /** @brief 스트림 엔드포인트의 TLS 설정입니다. 컨텍스트(인증서와 신뢰 목록)는 `INetSecurityProvider::createTLSContext` 로 만들어 엔드포인트보다 오래 두며, 없으면 평문입니다. */
    struct StreamSecuritySettings
    {
        ITLSContext* _pTLSContext{ nullptr };
    };
} // namespace sw

namespace sw
{
    struct StreamEndpointSettings
    {
        int32                  _maxFrameBodySize{ StreamFrameConstant::kDefaultMaxFrameSize };
        int32                  _maxPendingReceiveBytes{ 4 * 1024 * 1024 }; ///< 연결 하나가 `pump` 를 기다리며 쌓아 둘 수 있는 바이트. 넘으면 읽기를 멈춘다.
        float64                _pingIntervalSeconds{ 15.0 };               ///< 0이면 끈다. 전송의 유휴 시한보다 짧게 잡는다.
        NetCompressionSettings _compression{};                             ///< 보내는 프레임 본문의 압축(기본값은 꺼짐). 받는 쪽은 설정과 상관없이 레지스트리에 있는 코덱이면 푼다.
        StreamSecuritySettings _security{};                                ///< TLS 컨텍스트가 있으면 모든 연결이 TLS 를 쓴다. 열림은 핸드셰이크가 끝난 뒤에 알린다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class IStreamEndpointListener
     * @brief `pump` 가 호출하는 리스너입니다. 모두 `pump` 를 부른 스레드에서 호출되며, 연결마다 열림, 프레임, 닫힘 순서입니다.
     * @details 연결 실패처럼 **열린 적 없는 연결도 닫힘은 한 번 온다**(`connect` 한 쪽이 결과를 알 수 있게).
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
        /** @brief 프레임 하나(Message, Request, Response, Cancel)가 도착했습니다. @p pBody 는 콜백이 도는 동안만 유효합니다. */
        virtual void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) = 0;
        virtual void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason )                                = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class StreamMessageEndpoint
     * @brief 스트림 전송 위의 프레임 엔드포인트입니다. 전송의 처리기가 되어 I/O 스레드에서 프레임을 잘라 연결별 수신 큐에 쌓고, `pump` 가 한 번의 잠금으로 꺼내 줍니다.
     * @details 수신 큐가 `_maxPendingReceiveBytes` 를 넘으면 읽기를 멈춘다(TCP 윈도우가 닫혀 상대가 멈춘다). 핑과 퐁은 엔드포인트가 I/O 스레드에서 스스로 응답한다.
     *          송신 큐가 넘치면 메시지를 버리지 않고 연결을 끊는다(SendQueueOverflow). 버리면 그 위 계층의 순서가 깨지기 때문이다. 보내기는 어느 스레드에서나 가능하다.
     *          `_security._pTLSContext` 가 있으면 연결마다 TLS 세션을 두고 모든 바이트가 그 세션을 지난다.
     *          열림은 핸드셰이크가 끝난 뒤에 알리고, 핸드셰이크나 레코드 검증에 실패하면 SecurityFailure 로 끊어 열림 없이 닫힘만 알린다.
     *          핸드셰이크 중에 보낸 프레임은 세션이 모았다가 끝나면 보낸다. 위 계층은 TLS 를 모른다.
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
        /** @brief 소유자가 전송을 먼저 종료한 뒤 부릅니다. 연결 테이블을 비웁니다. */
        void shutdown();

        /** @brief 쌓인 사건과 프레임을 @p listener 에 넘깁니다. 핑도 여기서 처리합니다. 반환값은 넘긴 프레임 수입니다. */
        int32 pump( IStreamEndpointListener& listener );

        /** @brief Message 프레임을 보냅니다(어느 스레드에서나 가능). */
        StreamSendResult sendMessage( StreamConnectionHandle handle, const uint8* pBody, int32 bodySize );
        /** @brief 종류를 지정해 보냅니다. `NetRequestClient` 와 `NetRequestServer` 가 씁니다. */
        StreamSendResult sendFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        /** @brief 클라이언트로서 연결합니다. 결과는 `pump` 의 열림과 닫힘으로 알립니다. */
        StreamConnectionHandle connect( const NetAddress& remote );
        /** @brief 연결을 닫습니다. 우아하게 닫으면 쌓인 것을 다 보낸 뒤 닫습니다. 리스너가 받는 이유는 LocalClose 입니다. */
        void close( StreamConnectionHandle handle, StreamCloseMode mode );

        IStreamTransport* getTransport() const;
        /** @brief 연결의 RTT(핑으로 측정한 마지막 값, 초)를 반환합니다. 모르면 -1 입니다. */
        float64 getRoundTripSeconds( StreamConnectionHandle handle ) const;

        // IStreamHandler 구현. I/O 스레드에서 호출된다.
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamWritable( StreamConnectionHandle handle ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct Connection;
        struct Event;

        shared_ptr<Connection> findConnection( StreamConnectionHandle handle ) const;
        /** @brief 받은 바이트를 (TLS 면 복호해서) 디코더에 넣고 프레임을 큐에 쌓습니다(I/O 스레드, 연결 잠금 안). 깨졌으면 false 를 반환합니다. */
        bool acceptIncomingLocked( Connection& connection, const uint8* pData, int32 size );
        /** @brief 평문 바이트를 프레임으로 자릅니다(연결 잠금 안). 깨졌으면 false 를 반환합니다. */
        bool decodeFramesLocked( Connection& connection, const uint8* pData, int32 size );
        /** @brief 프레임 바이트를 (TLS 면 세션을 거쳐) 전송에 넘깁니다(연결 잠금 안). */
        StreamSendResult writeOutgoingLocked( Connection& connection, const vector<uint8>& frameBytes );
        /** @brief TLS 세션이 내놓은 암호문(핸드셰이크, 레코드, 경고)을 전송에 넘깁니다(연결 잠금 안). */
        StreamSendResult flushCiphertextLocked( Connection& connection );
        void             announceOpenLocked( Connection& connection );
        void             pushEvent( Event&& event, const uint8* pBody, int32 bodySize );
        void             closeForError( Connection& connection, StreamCloseReason reason );

        mutable mutex                                 _tableMutex;
        unordered_map<uint64, shared_ptr<Connection>> _mapConnection; ///< 핸들(packed)에서 연결로 가는 맵. I/O 스레드가 열고 닫는다.
        mutable mutex                                 _inboxMutex;
        vector<Event>                                 _listInbox;   ///< I/O 스레드가 쌓는다.
        vector<uint8>                                 _inboxBytes;  ///< 프레임 본문을 담는 아레나.
        vector<Event>                                 _listPumping; ///< pump 스레드 전용. 맞바꿔서 꺼낸다.
        vector<uint8>                                 _pumpingBytes;
        unordered_map<uint64, int64>                  _mapLastPingTime; ///< pump 스레드 전용. 열린 연결의 마지막 핑 시각(나노초).
        StreamEndpointSettings                        _settings;
        IStreamTransport*                             _pTransport;
    };
} // namespace sw
