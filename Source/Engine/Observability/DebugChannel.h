/**
 * @file DebugChannel.h
 * @brief 디버그 채널 — 실행 중인 서버(또는 클라이언트)에 붙어 개발 명령을 원격으로 부르는 읽기 중심의 TCP 채널입니다(서버 디버깅 S3). **Shipping 에는 없습니다.**
 * @details 개발 빌드 전용 · 루프백에만 묶음 · 토큰 필수 · 임의 프로퍼티 쓰기는 열지 않고 개발 명령으로만 게임이 정한 경로를 거칩니다.
 *          **프로토콜** 요청은 한 줄 `<토큰> <개발 명령 한 줄>\n`, 응답은 `OK <바이트 수>\n<본문>` 또는 `ERR <바이트 수>\n<본문>` 입니다.
 *          토큰이 틀리면 `ERR` 하나를 돌려주고 연결을 닫습니다(그 연결의 줄은 실행하지 않는다). 한 연결로 요청을 여러 번 보낼 수 있습니다.
 *          **스레드** I/O 스레드는 줄을 큐에 쌓기만 하고, `pump()` 가 게임 스레드에서 명령을 실행해 답합니다 — 씬을 읽는 명령이 게임 스레드에서 돌아야 하고,
 *          서버가 `pause` 로 멈춰 있어도 루프는 `pump()` 를 계속 부르므로 멈춘 상태의 값을 읽을 수 있습니다(중단점에서 값을 보는 쓰임).
 *          실행기는 델리게이트로 받아 이 파일은 콘솔을 모릅니다(서버는 `DevConsole::execute`).
 *          `DebugChannelClient` 는 같은 프로토콜의 동기 클라이언트입니다(에디터 · 테스트 · 도구).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/OnceInit.h"
#include "Core/Console/DevCommandsEnabled.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Concurrency/mutex.h"
    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"
    #include "Core/Delegate/Delegate.h"
    #include "Core/Memory/Memory.h"
    #include "Core/Network/NetTypes.h"
    #include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    /** @brief 명령 한 줄을 실행하고 답을 @p outReply 에 씁니다. 실패면 false(답은 오류 글). 게임 스레드에서만 부릅니다. */
    using DebugChannelExecuteDelegate = Delegate<bool( string_view line, string& outReply )>;

    /** @brief 디버그 채널 설정입니다. */
    struct DebugChannelConfig
    {
        NetAddress _bindAddress{ NetAddress::makeLoopback( 0 ) }; ///< 루프백만 받는다(다른 주소면 시작을 거절). 포트 0 = 아무 포트
        string     _token;                                        ///< 비면 시작을 거절한다
        int32      _maxLineBytes{ 4096 };                         ///< 요청 한 줄의 상한(넘으면 끊는다)
        int32      _maxQueuedLines{ 64 };                         ///< 게임 스레드가 아직 못 돌린 줄의 상한(넘는 줄은 `ERR busy`)
    };
} // namespace sw

namespace sw
{
    /** @class DebugChannel @brief 서버 쪽 채널입니다. 전송은 빌려 쓴다(부르는 쪽이 만들고, 이것을 내린 뒤 지운다). */
    class SW_API DebugChannel final : public IStreamHandler
    {
    public:
        DebugChannel();
        ~DebugChannel() override;

        DebugChannel( const DebugChannel& )            = delete;
        DebugChannel& operator=( const DebugChannel& ) = delete;

        /** @brief 루프백 주소와 토큰이 올바르면 받기를 시작합니다. 아니면 false(이유는 로그). */
        [[nodiscard]] bool initialize( IStreamTransport* pTransport, const StreamTransportConfig& transportSettings, const DebugChannelConfig& settings,
                                       DebugChannelExecuteDelegate execute );
        /** @brief 전송을 내립니다. 두 번 불러도 됩니다. */
        void   shutdown();
        uint16 getListenPort() const;

        /** @brief **게임 스레드**에서 쌓인 요청을 실행해 답합니다. 실행한 줄 수입니다. */
        uint32 pump();

        // IStreamHandler — I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        struct Connection
        {
            string _buffer{};
            uint64 _packedHandle{ 0 };
        };
        struct Request
        {
            uint64 _packedHandle{ 0 };
            string _command{};
        };

        void sendReply( uint64 packedHandle, bool bOk, string_view body ) const;

        vector<Connection> _listConnection    SW_GUARDED_BY( _mutex );
        vector<Request> _listRequest          SW_GUARDED_BY( _mutex );
        OnceInit<DebugChannelConfig>          _settings;
        OnceInit<DebugChannelExecuteDelegate> _execute;
        mutex                                 _mutex;
        OnceInit<IStreamTransport*>           _pTransport;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DebugChannelClient
     * @brief 같은 프로토콜의 동기 클라이언트입니다 — 연결하고, 요청 하나를 보내고 답이 올 때까지 기다립니다. 스레드 하나에서 씁니다.
     */
    class SW_API DebugChannelClient final : public IStreamHandler
    {
    public:
        DebugChannelClient();
        ~DebugChannelClient() override;

        DebugChannelClient( const DebugChannelClient& )            = delete;
        DebugChannelClient& operator=( const DebugChannelClient& ) = delete;

        /** @brief 서버에 붙습니다(@p timeoutMilli 안에 열려야 true). */
        [[nodiscard]] bool connect( const NetAddress& remote, string_view token, int32 timeoutMilli );
        void               disconnect();
        bool               isConnected() const;
        /**
         * @brief 명령 한 줄을 보내고 답을 기다립니다. 서버가 `ERR` 로 답하면 false 이고 @p outBody 는 오류 글입니다. 시한이 지나거나 끊기면 false 이고 @p outBody 가 비어 있습니다.
         */
        [[nodiscard]] bool request( string_view commandLine, int32 timeoutMilli, string& outBody );

        // IStreamHandler — I/O 스레드
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

    private:
        unique_ptr<IStreamTransport> _transport;
        string                       _token;
        string _received             SW_GUARDED_BY( _mutex );
        mutable mutex                _mutex;
        StreamConnectionHandle       _handle;
        uint8                        _bOpen    : 1 SW_GUARDED_BY( _mutex );
        uint8                        _bClosed  : 1 SW_GUARDED_BY( _mutex );
        [[maybe_unused]] uint8       _reserved : 6;
    };
} // namespace sw

#endif
