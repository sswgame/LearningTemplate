/**
 * @file RespEphemeralStore.h
 * @brief 휘발성 저장 계약(`IEphemeralStore`)의 RESP2 구현 — Valkey(리눅스) · Garnet(윈도우)이 모두 지원하는 명령 부분집합만 씁니다.
 * @details - 연결 둘: 명령 연결(파이프라인 — 답은 보낸 순서) · 구독 연결(SUBSCRIBE 모드). 비교 후 쓰기(WATCH → GET → MULTI/EXEC)는 명령 연결에서 하고,
 *            그 GET 답을 받을 때까지 뒤 요청을 내보내지 않는다(WATCH 는 연결 상태라 사이에 다른 명령이 끼면 안 된다 — 임대는 드물다).
 *          - 답은 맡긴 순서대로 거둔다(계약). 끊기거나 시한(`_timeoutMs`)을 넘기면 기다리던 요청은 모두 `Unavailable` 로 정확히 한 번 — 다음 요청이 다시 연결한다(물러남).
 *          - 키 · 채널 앞에 `_keyPrefix` 를 붙인다(한 서버를 배포 여럿이 나눠 쓰기 — DB 번호 대신). 받은 메시지의 채널에서는 뗀다.
 *          - 쓰지 않는 것: Lua · `SELECT` · RESP3 · Redis 6.2+ 옵션(`SET … GET` · `GETEX` · `PEXPIRE NX`) — Garnet 과 갈린다.
 *          - 전송의 `_ioThreadCount` 가 0 이면(루프백 시험) `pollReplies` · `pollMessages` · 구독 기다림이 전송을 직접 돈다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/CacheStore/Server/Driver/Resp/RespConnection.h"

namespace sw
{
    /** @brief RESP 앞의 설정입니다. */
    struct RespStoreSettings
    {
        string     _keyPrefix{}; ///< 모든 키 · 채널 앞(`game1:`)
        string     _userName{};  ///< 비면 `AUTH <비밀번호>`
        string     _password{};  ///< 비면 AUTH 없음
        NetAddress _address{};
        int64      _timeoutMs{ 2000 };    ///< 요청 하나의 시한 — 넘으면 그 연결을 끊고 다시 연다(답 순서에 기대므로 하나가 늦으면 뒤가 모두 늦다)
        int64      _maxBackoffMs{ 5000 }; ///< 다시 연결 물러남의 상한(100 ms 부터 두 배씩)
    };
} // namespace sw

namespace sw
{
    /**
     * @class RespEphemeralStore
     * @brief RESP2 휘발성 저장 앞입니다. 서비스 스레드 하나가 맡기고 거둡니다(전송 콜백만 I/O 스레드).
     */
    class SW_GF_API RespEphemeralStore final : public IEphemeralStore, private IStreamHandler
    {
    public:
        RespEphemeralStore();
        ~RespEphemeralStore() override;

        /**
         * @brief 전송을 받아 띄우고 명령 연결을 겁니다(열림을 기다리지 않는다 — 첫 요청은 모였다가 나간다).
         * @param transport 이 앞이 가진다. `_ioThreadCount` 는 설정 그대로(0 이면 이 앞이 돈다)
         * @param tlsContext 있으면 연결마다 TLS 세션(클라이언트 역할)
         */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const RespStoreSettings& settings,
                                       unique_ptr<ITlsContext> tlsContext, string& outError );

        uint64 submit( const EphemeralRequest& request ) override;
        int32  pollReplies( vector<EphemeralReply>& outListReply ) override;

        /** @brief 구독합니다 — 서버가 확인할 때까지(최대 `_timeoutMs`) 기다린다. 그래서 돌아온 뒤 다른 앞의 발행은 이 앞에 닿는다. */
        void  subscribe( string_view channel ) override;
        void  unsubscribe( string_view channel ) override;
        int32 pollMessages( vector<EphemeralMessage>& outListMessage ) override;

        int32 getPendingCount() const override { return static_cast<int32>( _listOperation.size() ); }
        void  shutdown() override;

    private:
        /** @brief 맡은 요청 하나 — 명령 몇 개의 답이 모여 답 하나가 된다. */
        struct Operation
        {
            EphemeralRequest _request{};
            EphemeralReply   _reply{};
            int64            _sentAtNanoseconds{ 0 };
            int32            _awaitingReplyCount{ 0 };
            uint8            _bSent{ SW_FALSE };
            uint8            _bDone{ SW_FALSE };
        };

        // IStreamHandler (I/O 스레드)
        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override;
        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override;
        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override;

        void       pumpTransport();
        Operation* findOperation( uint64 requestId );
        /** @brief 아직 내보내지 않은 요청을 맡긴 순서대로 내보냅니다(비교 후 쓰기의 GET 답을 기다리는 동안은 멈춘다). */
        void sendDeferredOperations();
        /** @brief 요청 하나의 명령을 보냅니다. 명령 연결이 없으면 다시 겁니다(물러남 동안이면 Unavailable). */
        void sendOperation( Operation& operation );
        void sendCommand( Operation& operation, const RespCommand& command, uint32 step );
        void handleReply( Operation& operation, uint32 step, const RespValue& value );
        void handleCompareReply( Operation& operation, uint32 step, const RespValue& value );
        void finishOperation( Operation& operation, EphemeralResult result );
        void failOperation( Operation& operation );
        /** @brief 명령 연결이 열려 있거나 걸리는 중이면 true — 아니면 물러남이 지났을 때 다시 겁니다. */
        bool ensureCommandConnection();
        bool ensureSubscribeConnection();
        void abortTimedOutRequests();
        void handlePushValue( const RespValue& value );
        /** @brief 구독 연결이 @p channel 확인을 낼 때까지 기다립니다(전송이 스레드 없이 돌면 직접 돈다). */
        void        waitForSubscriptionAck( uint64 targetAckCount );
        string      makeKey( string_view key ) const;
        RespCommand makeAuthCommand() const;

        RespStoreSettings            _settings;
        StreamTransportSettings      _transportSettings;
        mutable mutex                _mutex; ///< 두 연결 · 받은 메시지 · 확인 수 — I/O 스레드와 나눈다
        RespConnection               _commandConnection;
        RespConnection               _subscribeConnection;
        vector<EphemeralMessage>     _listMessage;
        vector<RespReplyRecord>      _listRecordScratch;
        vector<RespValue>            _listPushScratch;
        vector<string>               _listChannel; ///< 구독 중인 채널(접두 없는 이름) — 다시 연결하면 다시 구독한다
        deque<Operation>             _listOperation;
        unique_ptr<IStreamTransport> _transport;
        unique_ptr<ITlsContext>      _tlsContext;
        int64                        _nextCommandConnectNanoseconds;
        int64                        _nextSubscribeConnectNanoseconds;
        int64                        _commandBackoffMs;
        int64                        _subscribeBackoffMs;
        uint64                       _nextRequestId;
        uint64                       _subscriptionAckCount; ///< 구독 연결이 받은 subscribe · unsubscribe 확인 수
        uint64                       _subscriptionSentCount;
        uint64                       _blockingRequestId; ///< 비교 후 쓰기가 GET 답을 기다리는 요청(0 = 없음)
        uint8                        _bInitialized;
        uint8                        _bShutdown;
    };
} // namespace sw
