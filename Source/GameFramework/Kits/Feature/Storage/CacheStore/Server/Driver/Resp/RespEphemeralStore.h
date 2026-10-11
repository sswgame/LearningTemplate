/**
 * @file RespEphemeralStore.h
 * @brief 휘발성 저장 계약(`IEphemeralStore`)의 RESP2 구현입니다. Valkey(리눅스)와 Garnet(윈도우)이 모두 지원하는 명령 부분집합만 씁니다.
 * @details - 연결은 두 개이다. 명령 연결은 파이프라인으로 쓰며 답이 보낸 순서대로 온다. 구독 연결은 SUBSCRIBE 모드로 쓴다.
 *            비교 후 쓰기(WATCH, GET, MULTI/EXEC)는 명령 연결에서 하며, 그 GET 답을 받을 때까지 뒤 요청을 내보내지 않는다.
 *            WATCH 는 연결 상태이므로 그 사이에 다른 명령이 끼면 안 되기 때문이다. 임대는 드물어서 이 제약의 비용이 작다.
 *          - 답은 요청을 맡긴 순서대로 회수한다(계약). 연결이 끊기거나 시한(`_timeoutMs`)을 넘기면 기다리던 요청은 모두 정확히 한 번 `Unavailable` 로 끝난다.
 *            다음 요청이 다시 연결하며, 다시 연결할 때는 백오프를 둔다.
 *          - 키와 채널 앞에 `_keyPrefix` 를 붙인다. DB 번호 대신 접두어로 한 서버를 여러 배포가 나눠 쓰게 하려는 것이다. 받은 메시지의 채널에서는 접두어를 뗀다.
 *          - 쓰지 않는 것은 Lua, `SELECT`, RESP3, Redis 6.2 이상의 옵션(`SET … GET`, `GETEX`, `PEXPIRE NX`)이다. Garnet 과 동작이 갈리기 때문이다.
 *          - 전송의 `_ioThreadCount` 가 0이면(루프백 테스트) `pollReplies`, `pollMessages`, 구독 대기가 전송을 직접 돌린다.
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
    /** @brief RESP 구현의 설정입니다. */
    struct RespStoreSettings
    {
        string     _keyPrefix{}; ///< 모든 키와 채널 앞에 붙이는 접두어이다(예: `game1:`)
        string     _userName{};  ///< 비면 `AUTH <비밀번호>` 형식으로 보낸다
        string     _password{};  ///< 비면 AUTH 를 보내지 않는다
        NetAddress _address{};
        int64      _timeoutMs{ 2000 };    ///< 요청 하나의 시한이다. 넘으면 그 연결을 끊고 다시 연다. 답 순서에 기대므로 하나가 늦으면 뒤의 요청도 모두 늦어진다
        int64      _maxBackoffMs{ 5000 }; ///< 다시 연결할 때 백오프의 상한이다. 100 ms 부터 두 배씩 늘어난다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RespEphemeralStore
     * @brief RESP2 휘발성 저장소입니다. 서비스 스레드 하나가 요청을 맡기고 답을 회수하며, 전송 콜백만 I/O 스레드에서 실행됩니다.
     */
    class SW_GF_API RespEphemeralStore final : public IEphemeralStore, private IStreamHandler
    {
    public:
        RespEphemeralStore();
        ~RespEphemeralStore() override;

        /**
         * @brief 전송을 받아 초기화하고 명령 연결을 시작합니다. 연결이 열리기를 기다리지 않으므로 첫 요청은 모아 두었다가 나갑니다.
         * @param transport 이 저장소가 소유합니다. `_ioThreadCount` 는 설정 그대로 둡니다. 0이면 이 저장소가 직접 돌립니다.
         * @param tlsContext 있으면 연결마다 TLS 세션(클라이언트 역할)을 만듭니다.
         */
        [[nodiscard]] bool initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const RespStoreSettings& settings,
                                       unique_ptr<ITLSContext> tlsContext, string& outError );

        uint64 submit( const EphemeralRequest& request ) override;
        int32  pollReplies( vector<EphemeralReply>& outListReply ) override;

        /** @brief 구독합니다. 서버가 확인할 때까지 최대 `_timeoutMs` 동안 기다리므로, 이 함수가 돌아온 뒤에는 다른 인스턴스의 발행이 이 저장소에 도착합니다. */
        void  subscribe( string_view channel ) override;
        void  unsubscribe( string_view channel ) override;
        int32 pollMessages( vector<EphemeralMessage>& outListMessage ) override;

        int32 getPendingCount() const override { return static_cast<int32>( _listOperation.size() ); }
        void  shutdown() override;

    private:
        /** @brief 맡은 요청 하나입니다. 명령 몇 개의 답이 모여 답 하나가 됩니다. */
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
        Operation* findOperation( uint64 requestID );
        /** @brief 아직 내보내지 않은 요청을 맡긴 순서대로 내보냅니다. 비교 후 쓰기의 GET 답을 기다리는 동안은 멈춥니다. */
        void sendDeferredOperations();
        /** @brief 요청 하나의 명령을 보냅니다. 명령 연결이 없으면 다시 연결하며, 백오프 중이면 Unavailable 로 끝납니다. */
        void sendOperation( Operation& operation );
        void sendCommand( Operation& operation, const RespCommand& command, uint32 step );
        void handleReply( Operation& operation, uint32 step, const RespValue& value );
        void handleCompareReply( Operation& operation, uint32 step, const RespValue& value );
        void finishOperation( Operation& operation, EphemeralResult result );
        void failOperation( Operation& operation );
        /** @brief 명령 연결이 열려 있거나 연결 중이면 true 입니다. 아니면 백오프가 지났을 때 다시 연결합니다. */
        bool ensureCommandConnection();
        bool ensureSubscribeConnection();
        void abortTimedOutRequests();
        void handlePushValue( const RespValue& value );
        /** @brief 구독 연결이 @p channel 확인을 낼 때까지 기다립니다. 전송이 별도 스레드 없이 돌면 직접 돌립니다. */
        void        waitForSubscriptionAck( uint64 targetAckCount );
        string      makeKey( string_view key ) const;
        RespCommand makeAuthCommand() const;

        RespStoreSettings            _settings;
        StreamTransportSettings      _transportSettings;
        mutable mutex                _mutex; ///< 두 연결, 받은 메시지, 확인 수를 보호한다. I/O 스레드와 함께 쓴다
        RespConnection               _commandConnection;
        RespConnection               _subscribeConnection;
        vector<EphemeralMessage>     _listMessage;
        vector<RespReplyRecord>      _listRecordScratch;
        vector<RespValue>            _listPushScratch;
        vector<string>               _listChannel; ///< 구독 중인 채널이다. 접두어가 없는 이름이며 다시 연결하면 다시 구독한다
        deque<Operation>             _listOperation;
        unique_ptr<IStreamTransport> _transport;
        unique_ptr<ITLSContext>      _tlsContext;
        int64                        _nextCommandConnectNanoseconds;
        int64                        _nextSubscribeConnectNanoseconds;
        int64                        _commandBackoffMs;
        int64                        _subscribeBackoffMs;
        uint64                       _nextRequestID;
        uint64                       _subscriptionAckCount; ///< 구독 연결이 받은 subscribe 와 unsubscribe 확인 수
        uint64                       _subscriptionSentCount;
        uint64                       _blockingRequestID; ///< 비교 후 쓰기가 GET 답을 기다리는 요청이다. 0이면 없다
        uint8                        _bInitialized;
        uint8                        _bShutdown;
    };
} // namespace sw
