/**
 * @file RESPConnection.h
 * @brief RESP 연결 하나입니다. 스트림 핸들, 증분 파서, 보낸 명령의 답을 기다리는 FIFO 큐, 선택 사항인 TLS 세션, 처음에 보낼 AUTH 를 가집니다.
 * @details - 스레드 안전하지 않다. 소유자(`RESPEphemeralStore`)가 락을 잡고 호출해야 한다. 전송 콜백은 I/O 스레드에서, 송신과 회수는 서비스 스레드에서 실행된다.
 *          - 명령 연결은 답을 보낸 순서대로 태그에 짝짓는다. RESP 는 답의 순서가 곧 명령의 순서이기 때문이다.
 *            구독 연결(`_bPushMode`)도 보낸 명령마다 태그 하나를 두고 확인(subscribe · unsubscribe)이 오면 떼며, `message` 만 넘긴다 — 확인을 기다리는 수가 연결과 함께 사라져 옛 연결의 확인을 새 연결의 것으로 세지 않는다.
 *          - 다시 연결하기 전의 물러섬도 연결이 든다. 어느 길로 닫혀도(저쪽이 닫음 · 시한 · 보내기 실패 · 깨진 답 · 연결 실패) 닫히는 한 곳에서 다음 시각을 정하고,
 *            답 하나를 온전히 받은 연결만 물러섬을 처음으로 돌린다 — TLS 핸드셰이크나 잘린 답의 바이트는 답이 아니다.
 *          - 연결이 열리기 전에 보낸 명령은 모아 두었다가 열리면 AUTH 다음에 보낸다. 연결이 닫히면 기다리던 태그마다 정확히 한 번 실패 답을 낸다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Time/Clock.h"
#include "Core/Time/ExponentialBackoff.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/RESP/RESPCodec.h"

namespace sw
{
    class IStreamTransport;
    class ITLSContext;
    class ITLSSession;

    /** @brief 답이 누구의 것인지 나타냅니다. 맡은 요청 id 와 그 요청의 몇 번째 명령인지를 가집니다. */
    struct RESPCommandTag
    {
        uint64 _requestID{ 0 };
        uint32 _step{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 짝지은 답 하나입니다. `_bFailed` 가 true 이면 답을 받지 못한 것이며, 연결이 끊겼거나 시한을 넘겼거나 AUTH 가 실패한 경우입니다. */
    struct RESPReplyRecord
    {
        RESPValue      _value{};
        RESPCommandTag _tag{};
        uint8          _bFailed{ SW_FALSE };
    };

    enum class RESPConnectionState : uint8
    {
        Closed = 0,
        Connecting,
        Open
    };
} // namespace sw

namespace sw
{
    /**
     * @class RESPConnection
     * @brief RESP 연결 하나입니다. 명령 연결과 구독 연결이 같은 타입입니다.
     */
    class SW_KIT_SERVER_API RESPConnection
    {
    public:
        /** @brief AUTH 답에 다는 단계 번호입니다. 요청의 단계 번호와 겹치지 않습니다. */
        static constexpr uint32 kAuthStep = 0xFFFFFFFFu;

        /** @param clock 물러섬을 재는 시계입니다. 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        RESPConnection( bool bPushMode, const IClock& clock );
        ~RESPConnection();

        RESPConnection( const RESPConnection& )            = delete;
        RESPConnection& operator=( const RESPConnection& ) = delete;

        /**
         * @brief 연결을 시작합니다. @p pAuthCommand 가 있으면 열리자마자 가장 먼저 보냅니다. 시작하지 못하면 false 를 반환하고 바로 닫힌 상태가 됩니다.
         * @param pTLSContext  있으면 세션을 만들고 모든 바이트가 그 세션을 거칩니다. 빌려 쓰는 포인터입니다.
         * @param maxBackoffMs 이 연결이 닫힌 뒤 물러섬의 상한입니다.
         */
        [[nodiscard]] bool beginConnect( IStreamTransport& transport, const NetAddress& address, ITLSContext* pTLSContext, const RESPCommand* pAuthCommand, int64 maxBackoffMs );
        /** @brief 닫혀 있고 물러섬이 지났으면 true 입니다. */
        [[nodiscard]] bool canConnect() const { return _state == RESPConnectionState::Closed && _clock.nowNanoseconds() >= _nextConnectNanoseconds; }
        /** @brief 명령 하나를 보내고 답 태그를 큐에 넣습니다. 열리기 전이면 모아 둡니다. 닫힌 연결이면 false 를 반환합니다. */
        bool sendCommand( IStreamTransport& transport, const RESPCommand& command, const RESPCommandTag& tag );
        /** @brief 바로 끊고(RST) 기다리던 태그를 모두 실패로 처리합니다. 이 핸들의 늦은 콜백은 버립니다. */
        void abort( IStreamTransport& transport, const utf8* pReason );
        /** @brief 저장소를 내릴 때 끊습니다. `abort` 와 같지만 예상된 종료라 경고를 남기지 않습니다. */
        void closeForShutdown( IStreamTransport& transport );

        /** @brief 전송 콜백입니다. 핸들이 이 연결의 것이 아니면 false 를 반환하고 아무것도 하지 않습니다. */
        bool handleOpened( IStreamTransport& transport, StreamConnectionHandle handle );
        bool handleReceived( IStreamTransport& transport, StreamConnectionHandle handle, const uint8* pData, int32 size );
        bool handleClosed( StreamConnectionHandle handle, StreamCloseReason reason );

        /** @brief 명령 연결은 짝지은 답을, 구독 연결은 받은 메시지(`message` 배열)를 꺼냅니다. */
        void takeReplyRecords( vector<RESPReplyRecord>& outListRecord );
        void takePushValues( vector<RESPValue>& outListValue );

        RESPConnectionState    getState() const { return _state; }
        StreamConnectionHandle getHandle() const { return _handle; }
        int32                  getAwaitingCount() const { return static_cast<int32>( _listAwaitTag.size() ); }
        /** @brief 마지막으로 열렸다가 닫힌 이유가 AUTH 거절이면 true 입니다. 이때는 다시 연결해도 소용이 없습니다. */
        bool wasAuthRejected() const { return _bAuthRejected == SW_TRUE; }

    private:
        /** @brief 평문을 전송합니다. TLS 이면 세션을 거칩니다. 실패하면 false 를 반환합니다. */
        [[nodiscard]] bool writePlainLocked( IStreamTransport& transport, const uint8* pData, size_t size );
        bool               flushCiphertext( IStreamTransport& transport );
        /** @brief 파서에서 값을 꺼내 태그와 짝짓습니다. 프레임이 깨졌거나 태그 없는 답이면 false 를 반환합니다. */
        bool pollParsedValues();
        void failAwaitingTags();
        /** @brief 다음 연결까지 물러섭니다. 닫히는 길이 모두 부릅니다. */
        void holdReconnect();

    private:
        const IClock&                _clock;
        RESPParser                   _parser;
        deque<RESPCommandTag>        _listAwaitTag;
        EventBuffer<RESPReplyRecord> _replyRecordBuffer;
        EventBuffer<RESPValue>       _pushValueBuffer;
        vector<uint8>                _pendingSendBytes; ///< 연결이 열리기 전에 모은 평문이다
        vector<uint8>                _plainBytes;
        vector<uint8>                _cipherBytes;
        unique_ptr<ITLSSession>      _tlsSession;
        StreamConnectionHandle       _handle;
        ExponentialBackoff           _backoff;
        int64                        _nextConnectNanoseconds;
        int64                        _maxBackoffMs;
        RESPConnectionState          _state;
        uint8                        _bAnswered; ///< 이 연결에서 답 하나를 온전히 받았다(AUTH 거절은 아니다)
        uint8                        _bPushMode;
        uint8                        _bAuthRejected;
    };
} // namespace sw
