/**
 * @file RespConnection.h
 * @brief RESP 연결 하나 — 스트림 핸들 · 증분 파서 · 보낸 명령의 답 대기 줄(FIFO) · 선택 TLS 세션 · 처음 보낼 AUTH.
 * @details - 스레드 안전하지 않다 — 주인(`RespEphemeralStore`)이 잠금을 쥐고 부른다(전송 콜백은 I/O 스레드, 보내기 · 거두기는 서비스 스레드).
 *          - 명령 연결은 답을 보낸 순서대로 꼬리표에 짝짓는다(RESP 는 답 순서가 명령 순서다). 구독 연결(`_bPushMode`)은 꼬리표 없이 받은 값을 모두 넘긴다.
 *          - 열리기 전에 보낸 명령은 모았다가 열리면 AUTH 다음에 보낸다. 닫히면 기다리던 꼬리표마다 실패 답을 정확히 한 번 낸다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/StreamTypes.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Storage/Server/CacheStore/Driver/Resp/RespCodec.h"

namespace sw
{
    class IStreamTransport;
    class ITlsContext;
    class ITlsSession;

    /** @brief 답이 누구 것인지 — 맡은 요청 id 와 그 요청의 몇 번째 명령인지입니다. */
    struct RespCommandTag
    {
        uint64 _requestId{ 0 };
        uint32 _step{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 짝지은 답 하나입니다. `_bFailed` 면 답을 받지 못했다(끊김 · 시한 · AUTH 실패). */
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
     * @brief RESP 연결 하나입니다(명령 · 구독 연결이 같은 타입).
     */
    class SW_GF_API RespConnection
    {
    public:
        /** @brief AUTH 답에 다는 걸음 번호입니다(요청의 걸음과 겹치지 않는다). */
        static constexpr uint32 kAuthStep = 0xFFFFFFFFu;

        explicit RespConnection( bool bPushMode );
        ~RespConnection();

        RespConnection( const RespConnection& )            = delete;
        RespConnection& operator=( const RespConnection& ) = delete;

        /**
         * @brief 연결을 겁니다. @p pAuthCommand 가 있으면 열리자마자 가장 먼저 보낸다. 시작도 못 하면 false(바로 닫힘 상태).
         * @param pTlsContext 있으면 세션을 만들어 모든 바이트가 그것을 지난다(빌려 쓴다)
         */
        [[nodiscard]] bool beginConnect( IStreamTransport& transport, const NetAddress& address, ITlsContext* pTlsContext, const RespCommand* pAuthCommand );
        /** @brief 명령 하나를 보내고(열리기 전이면 모은다) 답 꼬리표를 줄에 넣습니다. 닫힌 연결이면 false. */
        bool sendCommand( IStreamTransport& transport, const RespCommand& command, const RespCommandTag& tag );
        /** @brief 바로 끊고(RST) 기다리던 꼬리표를 모두 실패로 냅니다. 이 핸들의 늦은 콜백은 버려진다. */
        void abort( IStreamTransport& transport, const utf8* pReason );

        /** @brief 전송 콜백 — 핸들이 이 연결 것이 아니면 false 이고 아무것도 하지 않는다. */
        bool handleOpened( IStreamTransport& transport, StreamConnectionHandle handle );
        bool handleReceived( IStreamTransport& transport, StreamConnectionHandle handle, const uint8* pData, int32 size );
        bool handleClosed( StreamConnectionHandle handle, StreamCloseReason reason );

        /** @brief 짝지은 답(명령 연결) · 받은 값(구독 연결)을 꺼냅니다. */
        void takeReplyRecords( vector<RespReplyRecord>& outListRecord );
        void takePushValues( vector<RespValue>& outListValue );

        RespConnectionState    getState() const { return _state; }
        StreamConnectionHandle getHandle() const { return _handle; }
        int32                  getAwaitingCount() const { return static_cast<int32>( _listAwaitTag.size() ); }
        /** @brief 마지막으로 열렸다가 닫힌 까닭이 AUTH 거절인가입니다(다시 걸어도 소용없다). */
        bool wasAuthRejected() const { return _bAuthRejected == SW_TRUE; }

    private:
        /** @brief 평문을 선에 올립니다(TLS 면 세션을 지나). 실패하면 false. */
        [[nodiscard]] bool writePlainLocked( IStreamTransport& transport, const uint8* pData, size_t size );
        bool               flushCiphertext( IStreamTransport& transport );
        /** @brief 파서에서 값을 꺼내 꼬리표와 짝짓습니다. 깨진 프레임 · 꼬리표 없는 답이면 false. */
        bool drainParsedValues();
        void failAwaitingTags();

        RespParser              _parser;
        deque<RespCommandTag>   _listAwaitTag;
        vector<RespReplyRecord> _listReplyRecord;
        vector<RespValue>       _listPushValue;
        vector<uint8>           _pendingSendBytes; ///< 열리기 전에 모은 평문
        vector<uint8>           _plainBytes;
        vector<uint8>           _cipherBytes;
        unique_ptr<ITlsSession> _tlsSession;
        StreamConnectionHandle  _handle;
        RespConnectionState     _state;
        uint8                   _bPushMode;
        uint8                   _bAuthRejected;
    };
} // namespace sw
