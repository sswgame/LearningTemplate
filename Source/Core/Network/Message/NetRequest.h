/**
 * @file NetRequest.h
 * @brief 서비스 요청-응답(복제용 RPC 아님) — 요청 id · 시한 · 취소 · 멱등 키. Request · Response · Cancel 프레임 위에 섭니다.
 * @details 와이어(몸은 `BitWriter`): Request = `varuint 요청 id · 16 비트 메서드 · varuint 시한(ms) · 1 비트 키 있음 · [32 × 4 비트 키] ·
 *          1 비트 추적 id 있음 · [32 × 4 비트 추적 id] · 바이트 정렬 · 몸`,
 *          Response = `varuint 요청 id · 8 비트 상태 · 바이트 정렬 · 몸`, Cancel = `varuint 요청 id`. 요청 id 는 클라이언트마다 1 부터 오른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogContext.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/Transport/StreamTypes.h"

namespace sw
{
    class NetRequestServer;
    class StreamMessageEndpoint;
} // namespace sw

namespace sw
{
    /** @brief 응답 상태입니다. 값은 와이어 형식입니다. */
    enum class NetRequestStatus : uint8
    {
        Ok = 0,
        ApplicationError, ///< 처리기가 거절했다 — 몸에 키트의 오류 코드
        DeadlineExceeded, ///< 시한 안에 응답이 없다(클라이언트가 스스로 끝낸다)
        Cancelled,        ///< 이쪽이 취소했다
        ConnectionLost,   ///< 응답 전에 연결이 닫혔다
        UnknownMethod,    ///< 서버에 그 메서드가 없다
        Malformed,        ///< 요청 머리가 깨졌다
        Overloaded,       ///< 서버의 진행 중 요청이 상한을 넘었다
        Count
    };

    SW_API const utf8* toString( NetRequestStatus status );

    /** @brief 멱등 키 128 비트 — 같은 키로 다시 보낸 요청은 서버가 처리하지 않고 처음 응답을 돌려줍니다(재접속 뒤에도, 주체가 같으면). 0 = 없음. */
    struct NetIdempotencyKey
    {
        uint64 _high{ 0 };
        uint64 _low{ 0 };

        constexpr bool isValid() const { return ( _high | _low ) != 0; }
        constexpr bool operator==( const NetIdempotencyKey& other ) const { return _high == other._high && _low == other._low; }
        /** @brief 운영체제 난수로 만듭니다(재시도마다 **같은** 키를 다시 써야 한다 — 새로 만들면 다른 요청이다). */
        SW_API static NetIdempotencyKey makeRandom();
    };
} // namespace sw

namespace sw
{
    struct NetRequestOptions
    {
        float64           _timeoutSeconds{ 10.0 }; ///< 클라이언트 시한 — 서버에도 남은 시간을 실어 보낸다
        NetIdempotencyKey _idempotencyKey{};       ///< 상태를 바꾸는 요청(거래 · 구매)은 꼭 채운다
        LogTraceID        _traceID{};              ///< 요청 추적 id — 비면 보내는 스레드의 로그 문맥(`LogContext::getCurrent`)의 것을 싣는다
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 응답 — 몸은 콜백 동안만 유효합니다. */
    struct NetResponse
    {
        const uint8*     _pBody{ nullptr };
        int32            _bodySize{ 0 };
        uint64           _requestID{ 0 };
        uint16           _method{ 0 };
        NetRequestStatus _status{ NetRequestStatus::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 처리 중인 요청 하나를 가리키는 표 — 나중에(비동기 · DB 뒤) `respond` 할 때 씁니다. */
    struct NetRequestToken
    {
        StreamConnectionHandle _handle{};
        uint64                 _requestID{ 0 };
        uint32                 _serial{ 0 }; ///< 서버 안의 일련번호 — 같은 요청 id 를 다시 쓴 연결과 섞이지 않게
        uint16                 _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 처리기에 넘어가는 요청 — 몸은 콜백 동안만 유효합니다. */
    struct NetRequestContext
    {
        NetRequestToken   _token{};
        NetIdempotencyKey _idempotencyKey{};
        LogTraceID        _traceID{};              ///< 요청 머리의 추적 id(없으면 서버가 만든다) — 처리기는 {추적 id, 주체} 로그 문맥 안에서 불린다
        uint64            _principalID{ 0 };       ///< `NetRequestServer::setPrincipal` 로 붙인 주체(로그인한 계정) — 없으면 0
        float64           _deadlineSeconds{ 0.0 }; ///< 서버 단조 시계(초)로 이때까지 답하지 않으면 클라이언트는 이미 포기했다
        const uint8*      _pBody{ nullptr };
        int32             _bodySize{ 0 };
    };
} // namespace sw

namespace sw
{
    struct NetRequestServerSettings
    {
        int32   _maxInFlightPerConnection{ 64 }; ///< 넘으면 Overloaded
        int32   _maxIdempotencyEntries{ 100000 };
        float64 _idempotencyTtlSeconds{ 600.0 }; ///< 응답을 기억하는 시간(재접속 · 재시도 창보다 길게)
    };
} // namespace sw

namespace sw
{
    /** @brief 서버의 메서드 처리기입니다. */
    class SW_API INetRequestHandler
    {
    public:
        INetRequestHandler()          = default;
        virtual ~INetRequestHandler() = default;

        INetRequestHandler( const INetRequestHandler& )            = default;
        INetRequestHandler& operator=( const INetRequestHandler& ) = default;

        /** @brief 요청 하나입니다. 바로 `respond` 하거나, 토큰을 들고 나중에 `respond` 합니다(한 번만). 어떤 잠금도 쥐지 않은 채 불립니다. */
        virtual void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetRequestClient
     * @brief 요청을 보내고 응답 · 시한 · 취소 · 연결 끊김 · 과부하를 콜백 하나로 끝냅니다(정확히 한 번). 콜백은 `handleFrame` · `update` ·
     *        `onConnectionClosed` 를 부른 스레드(= `pump` 스레드)이고, 보내지 못한 경우만 `sendRequest` 를 부른 스레드입니다.
     * @code
     *     const uint64 requestID = _requestClient.sendRequest( handle, LoginMethod::kLogin, body.data(), size, options,
     *                                                           Delegate<void( const NetResponse& )>::create<&LoginClient::onLoginResponse>( this ) );
     *     // 리스너에서:
     *     void onEndpointFrame( handle, kind, pBody, size ) { if ( _requestClient.handleFrame( handle, kind, pBody, size ) ) return; ... }
     *     void onEndpointClosed( handle, reason )          { _requestClient.onConnectionClosed( handle ); }
     *     // 틱: _requestClient.update();                    // 시한
     * @endcode
     */
    class SW_API NetRequestClient
    {
    public:
        NetRequestClient();

        void initialize( StreamMessageEndpoint* pEndpoint, int32 maxPendingRequests = 1024 );
        /** @brief 남은 요청을 Cancelled 로 끝냅니다. */
        void shutdown();

        /** @brief 보냅니다. 요청 id 입니다(0 = 보내지 못함 — 콜백은 이미 ConnectionLost · Overloaded 로 불렸다). */
        uint64 sendRequest( StreamConnectionHandle handle, uint16 method, const uint8* pBody, int32 bodySize, const NetRequestOptions& options,
                            Delegate<void( const NetResponse& )> onResponse );
        /** @brief 취소합니다 — 서버에 Cancel 을 보내고 콜백을 Cancelled 로 바로 부릅니다. 이미 끝났으면 false. */
        bool cancel( uint64 requestID );

        /** @brief Response 프레임이면 먹고 true 입니다. */
        [[nodiscard]] bool handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void               onConnectionClosed( StreamConnectionHandle handle );
        /** @brief 시한이 지난 요청을 DeadlineExceeded 로 끝냅니다. */
        void  update();
        int32 getPendingCount() const;

    private:
        struct Pending
        {
            Delegate<void( const NetResponse& )> _onResponse{};
            StreamConnectionHandle               _handle{};
            int64                                _deadlineNanoseconds{ 0 };
            uint16                               _method{ 0 };
        };

        static void finish( uint64 requestID, Pending&& pending, NetRequestStatus status, const uint8* pBody, int32 bodySize );

        mutable mutex                  _mutex;
        unordered_map<uint64, Pending> _mapPending;
        BitWriter                      _writer; ///< _mutex 안에서
        StreamMessageEndpoint*         _pEndpoint;
        uint64                         _nextRequestID;
        int32                          _maxPendingRequests;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetRequestServer
     * @brief 메서드마다 처리기 하나 — 요청 머리를 읽고, 멱등 키를 보고(이미 끝난 키면 기억한 응답, 진행 중이면 그 응답을 같이 받게), 처리기를 부릅니다.
     * @details 멱등 범위는 (주체, 메서드, 키)다. 주체는 로그인 키트가 `setPrincipal` 로 붙인다 — 그래야 끊기고 다시 붙은 연결의 재시도가 같은 키로 잡힌다.
     *          주체가 0 이면 범위는 연결이다(재접속 뒤에는 잡히지 않는다 — 상태를 바꾸는 메서드는 로그인 뒤에만 열 것).
     *          잠금 순서: `_mutex` 를 푼 뒤에 끝점의 연결 잠금. `sendResponse` 는 `_writerMutex` → 끝점 잠금.
     */
    class SW_API NetRequestServer
    {
    public:
        NetRequestServer();

        void initialize( StreamMessageEndpoint* pEndpoint, const NetRequestServerSettings& settings );
        void shutdown();

        /** @brief 메서드에 처리기를 답니다. 이미 단 메서드면 오류 로그와 함께 false 이고 앞 처리기를 그대로 둔다(키트 둘이 같은 번호를 쓰는 조립). */
        [[nodiscard]] bool registerMethod( uint16 method, INetRequestHandler* pHandler );
        void               unregisterMethod( uint16 method );
        void               setPrincipal( StreamConnectionHandle handle, uint64 principalID );

        /** @brief 답합니다(아무 스레드). 이미 답했으면 false — 연결이 닫혔어도 멱등 키가 있으면 응답은 기억한다(재시도가 받는다). */
        bool respond( const NetRequestToken& token, NetRequestStatus status, const uint8* pBody, int32 bodySize );
        /** @brief 클라이언트가 취소했는지입니다(오래 걸리는 처리기가 중간에 본다). */
        bool isCancelled( const NetRequestToken& token ) const;

        /** @brief Request · Cancel 프레임이면 먹고 true 입니다. */
        [[nodiscard]] bool handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void               onConnectionClosed( StreamConnectionHandle handle );
        /** @brief 멱등 기억의 수명을 정리합니다. */
        void update();

    private:
        /** @brief 멱등 범위 — (주체, 메서드, 키). 주체가 0 이면 연결(packed)이 대신하고 `_bConnectionScope`. */
        struct ScopeKey
        {
            uint64 _owner{ 0 };
            uint64 _keyHigh{ 0 };
            uint64 _keyLow{ 0 };
            uint16 _method{ 0 };
            uint8  _bConnectionScope{ SW_FALSE };

            bool operator==( const ScopeKey& other ) const;
        };
        struct ScopeKeyHash
        {
            size_t operator()( const ScopeKey& key ) const;
        };
        struct RequestKey
        {
            uint64 _handlePacked{ 0 };
            uint64 _requestID{ 0 };

            bool operator==( const RequestKey& other ) const { return _handlePacked == other._handlePacked && _requestID == other._requestID; }
        };
        struct RequestKeyHash
        {
            size_t operator()( const RequestKey& key ) const;
        };
        struct InFlight
        {
            NetRequestToken _token{};
            ScopeKey        _scope{};
            uint8           _bIdempotent{ SW_FALSE };
            uint8           _bCancelled{ SW_FALSE };
        };
        struct IdempotencyEntry
        {
            vector<uint8>           _body{};
            vector<NetRequestToken> _listWaiter{}; ///< 처리 중에 같은 키로 온 요청 — 첫 응답을 같이 받는다
            int64                   _expireNanoseconds{ 0 };
            NetRequestStatus        _status{ NetRequestStatus::Ok };
            uint8                   _bInProgress{ SW_FALSE }; ///< 처리기가 아직 답하지 않았다
            uint8                   _bDone{ SW_FALSE };
        };

        void sendResponse( StreamConnectionHandle handle, uint64 requestID, NetRequestStatus status, const uint8* pBody, int32 bodySize );
        void purgeIdempotencyLocked( int64 now );

        mutable mutex                                           _mutex;
        unordered_map<uint16, INetRequestHandler*>              _mapHandler;
        unordered_map<uint32, InFlight>                         _mapInFlight; ///< 일련번호 → 처리 중
        unordered_map<RequestKey, uint32, RequestKeyHash>       _mapSerialByRequest;
        unordered_map<uint64, int32>                            _mapInFlightCount; ///< 연결 → 처리 중 수
        unordered_map<uint64, uint64>                           _mapPrincipal;     ///< 연결 → 주체
        unordered_map<ScopeKey, IdempotencyEntry, ScopeKeyHash> _mapIdempotency;
        deque<pair<ScopeKey, int64>>                            _listExpiry; ///< 끝난 기억의 만료 순서
        mutex                                                   _writerMutex;
        BitWriter                                               _writer; ///< _writerMutex 안에서
        NetRequestServerSettings                                _settings;
        StreamMessageEndpoint*                                  _pEndpoint;
        uint32                                                  _nextSerial;
    };
} // namespace sw
