/**
 * @file RespConnection.h
 * @brief RESP 연결 하나입니다. 스트림 핸들, 증분 파서, 보낸 명령의 답을 기다리는 FIFO 큐, 선택 사항인 TLS 세션, 처음에 보낼 AUTH 를 가집니다.
 * @details - 스레드 안전하지 않다. 소유자(`RespEphemeralStore`)가 락을 잡고 호출해야 한다. 전송 콜백은 I/O 스레드에서, 송신과 회수는 서비스 스레드에서 실행된다.
 *          - 명령 연결은 답을 보낸 순서대로 태그에 짝짓는다. RESP 는 답의 순서가 곧 명령의 순서이기 때문이다.
 *            구독 연결(`_bPushMode`)은 태그 없이 받은 값을 모두 넘긴다.
 *          - 연결이 열리기 전에 보낸 명령은 모아 두었다가 열리면 AUTH 다음에 보낸다. 연결이 닫히면 기다리던 태그마다 정확히 한 번 실패 답을 낸다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespCodec.h"

namespace sw
{
    class IStreamTransport;
    class ITLSContext;
    class ITLSSession;

    /** @brief 답이 누구의 것인지 나타냅니다. 맡은 요청 id 와 그 요청의 몇 번째 명령인지를 가집니다. */
    struct RespCommandTag
    {
        uint64 _requestID{ 0 };
        uint32 _step{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 짝지은 답 하나입니다. `_bFailed` 가 true 이면 답을 받지 못한 것이며, 연결이 끊겼거나 시한을 넘겼거나 AUTH 가 실패한 경우입니다. */
    struct RespReplyRecord
    {
        RespValue      _value{};
        RespCommandTag _tag{};
        uint8          _bFailed{ SW_FALSE };
    };

    enum class RespConnectionState : uint8
    {
        Closed = 0,
        Connecting,
        Open
    };
} // namespace sw

namespace sw
{
    /**
     * @class RespConnection
     * @brief RESP 연결 하나입니다. 명령 연결과 구독 연결이 같은 타입입니다.
     */
    class SW_KIT_SERVER_API RespConnection
    {
    public:
        /** @brief AUTH 답에 다는 단계 번호입니다. 요청의 단계 번호와 겹치지 않습니다. */
        static constexpr uint32 kAuthStep = 0xFFFFFFFFu;

        explicit RespConnection( bool bPushMode );
        ~RespConnection();

        RespConnection( const RespConnection& )            = delete;
        RespConnection& operator=( const RespConnection& ) = delete;

        /**
         * @brief 연결을 시작합니다. @p pAuthCommand 가 있으면 열리자마자 가장 먼저 보냅니다. 시작하지 못하면 false 를 반환하고 바로 닫힌 상태가 됩니다.
         * @param pTLSContext 있으면 세션을 만들고 모든 바이트가 그 세션을 거칩니다. 빌려 쓰는 포인터입니다.
         */
        [[nodiscard]] bool beginConnect( IStreamTransport& transport, const NetAddress& address, ITLSContext* pTLSContext, const RespCommand* pAuthCommand );
        /** @brief 명령 하나를 보내고 답 태그를 큐에 넣습니다. 열리기 전이면 모아 둡니다. 닫힌 연결이면 false 를 반환합니다. */
        bool sendCommand( IStreamTransport& transport, const RespCommand& command, const RespCommandTag& tag );
        /** @brief 바로 끊고(RST) 기다리던 태그를 모두 실패로 처리합니다. 이 핸들의 늦은 콜백은 버립니다. */
        void abort( IStreamTransport& transport, const utf8* pReason );

        /** @brief 전송 콜백입니다. 핸들이 이 연결의 것이 아니면 false 를 반환하고 아무것도 하지 않습니다. */
        bool handleOpened( IStreamTransport& transport, StreamConnectionHandle handle );
        bool handleReceived( IStreamTransport& transport, StreamConnectionHandle handle, const uint8* pData, int32 size );
        bool handleClosed( StreamConnectionHandle handle, StreamCloseReason reason );

        /** @brief 명령 연결은 짝지은 답을, 구독 연결은 받은 값을 꺼냅니다. */
        void takeReplyRecords( vector<RespReplyRecord>& outListRecord );
        void takePushValues( vector<RespValue>& outListValue );

        RespConnectionState    getState() const { return _state; }
        StreamConnectionHandle getHandle() const { return _handle; }
        int32                  getAwaitingCount() const { return static_cast<int32>( _listAwaitTag.size() ); }
        /** @brief 마지막으로 열렸다가 닫힌 이유가 AUTH 거절이면 true 입니다. 이때는 다시 연결해도 소용이 없습니다. */
        bool wasAuthRejected() const { return _bAuthRejected == SW_TRUE; }

    private:
        /** @brief 평문을 전송합니다. TLS 이면 세션을 거칩니다. 실패하면 false 를 반환합니다. */
        [[nodiscard]] bool writePlainLocked( IStreamTransport& transport, const uint8* pData, size_t size );
        bool               flushCiphertext( IStreamTransport& transport );
        /** @brief 파서에서 값을 꺼내 태그와 짝짓습니다. 프레임이 깨졌거나 태그 없는 답이면 false 를 반환합니다. */
        bool drainParsedValues();
        void failAwaitingTags();

        RespParser              _parser;
        deque<RespCommandTag>   _listAwaitTag;
        vector<RespReplyRecord> _listReplyRecord;
        vector<RespValue>       _listPushValue;
        vector<uint8>           _pendingSendBytes; ///< 연결이 열리기 전에 모은 평문이다
        vector<uint8>           _plainBytes;
        vector<uint8>           _cipherBytes;
        unique_ptr<ITLSSession> _tlsSession;
        StreamConnectionHandle  _handle;
        RespConnectionState     _state;
        uint8                   _bPushMode;
        uint8                   _bAuthRejected;
    };
} // namespace sw
