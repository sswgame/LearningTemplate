/**
 * @file NetRequest.h
 * @brief 서비스 요청-응답(복제용 RPC 아님)을 제공합니다. 요청 ID, 시한, 취소, 멱등 키를 다루며 Request, Response, Cancel 프레임 위에서 동작합니다.
 * @details 와이어 형식(본문은 `BitWriter`)은 다음과 같다.
 *          Request 는 `varuint 요청 ID`, 16비트 메서드, `varuint 시한(ms)`, 1비트 키 있음, [32 x 4비트 키], 1비트 추적 ID 있음, [32 x 4비트 추적 ID], 바이트 정렬, 본문 순서다.
 *          Response 는 `varuint 요청 ID`, 8비트 상태, 바이트 정렬, 본문 순서다. Cancel 은 `varuint 요청 ID` 하나다.
 *          요청 ID는 클라이언트마다 1부터 증가한다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/deque.h"
#include "Core/Container/pair.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/LogContext.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/Transport/StreamTypes.h"
#include "Core/Serialization/BitStream.h"

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
        ApplicationError, ///< 처리기가 거절했다. 본문에 키트의 오류 코드가 있다.
        DeadlineExceeded, ///< 시한 안에 응답이 없다(클라이언트가 스스로 끝낸다).
        Cancelled,        ///< 이쪽이 취소했다.
        ConnectionLost,   ///< 응답 전에 연결이 닫혔다.
        UnknownMethod,    ///< 서버에 그 메서드가 없다.
        Malformed,        ///< 요청 헤더가 깨졌다.
        Overloaded,       ///< 서버의 진행 중 요청이 상한을 넘었다.
        Count
    };

    SW_API const utf8* toString( NetRequestStatus status );

    /** @brief 멱등 키 128비트입니다. 같은 키로 다시 보낸 요청은 서버가 처리하지 않고 처음 응답을 돌려줍니다(재접속 뒤에도 주체가 같으면 같습니다). 0은 키 없음입니다. */
    struct NetIdempotencyKey
    {
        uint64 _high{ 0 };
        uint64 _low{ 0 };

        constexpr bool isValid() const { return ( _high | _low ) != 0; }
        constexpr bool operator==( const NetIdempotencyKey& other ) const { return _high == other._high && _low == other._low; }
        /** @brief 운영체제 난수로 만듭니다. 재시도마다 **같은** 키를 다시 써야 하며, 새로 만들면 다른 요청이 됩니다. */
        SW_API static NetIdempotencyKey makeRandom();
    };
} // namespace sw

namespace sw
{
    struct NetRequestOptions
    {
        float64           _timeoutSeconds{ 10.0 }; ///< 클라이언트 시한. 서버에도 남은 시간을 실어 보낸다.
        NetIdempotencyKey _idempotencyKey{};       ///< 상태를 바꾸는 요청(거래, 구매)은 반드시 채운다.
        LogTraceID        _traceID{};              ///< 요청 추적 ID. 비어 있으면 보내는 스레드의 로그 컨텍스트(`LogContext::getCurrent`) 값을 싣는다.
    };
} // namespace sw

namespace sw
{
    /** @brief 받은 응답입니다. 본문은 콜백이 도는 동안만 유효합니다. */
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
    /** @brief 서버가 처리 중인 요청 하나를 가리키는 토큰입니다. 나중에(비동기 처리나 DB 호출 뒤) `respond` 할 때 씁니다. */
    struct NetRequestToken
    {
        StreamConnectionHandle _handle{};
        uint64                 _requestID{ 0 };
        uint32                 _serial{ 0 }; ///< 서버 안의 일련번호. 같은 요청 ID를 다시 쓴 연결과 섞이지 않게 한다.
        uint16                 _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 처리기에 넘기는 요청입니다. 본문은 콜백이 도는 동안만 유효합니다. */
    struct NetRequestContext
    {
        NetRequestToken   _token{};
        NetIdempotencyKey _idempotencyKey{};
        LogTraceID        _traceID{};              ///< 요청 헤더의 추적 ID(없으면 서버가 만든다). 처리기는 {추적 ID, 주체} 로그 컨텍스트 안에서 불린다.
        uint64            _principalID{ 0 };       ///< `NetRequestServer::setPrincipal` 로 붙인 주체(로그인한 계정). 없으면 0.
        float64           _deadlineSeconds{ 0.0 }; ///< 서버 단조 시계(초)로 이 시각까지 답하지 않으면 클라이언트는 이미 포기한 것이다.
        const uint8*      _pBody{ nullptr };
        int32             _bodySize{ 0 };
    };
} // namespace sw

namespace sw
{
    struct NetRequestServerConfig
    {
        int32   _maxInFlightPerConnection{ 64 }; ///< 진행 중인 요청이 이 수를 넘으면 Overloaded 로 응답한다.
        int32   _maxIdempotencyEntries{ 100000 };
        float64 _idempotencyTTLSeconds{ 600.0 }; ///< 응답을 기억하는 시간. 재접속과 재시도 윈도우보다 길게 잡는다.
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서버의 메서드 처리 함수입니다(`NetRequestServer::registerMethod`). 요청 하나를 처리한다 — 바로 `respond` 하거나, 토큰을 보관했다가 나중에
     *        한 번만 `respond` 한다. 어떤 잠금도 잡지 않은 채 불린다. 주인은 메서드를 등록한 동안 살아 있어야 한다.
     */
    using NetRequestFunction = Delegate<void( NetRequestServer& server, const NetRequestContext& context )>;
} // namespace sw

namespace sw
{
    /**
     * @class NetRequestClient
     * @brief 요청을 보내고, 응답, 시한, 취소, 연결 끊김, 과부하를 콜백 하나로 정확히 한 번 알립니다.
     * @details 콜백은 `handleFrame`, `update`, `onConnectionClosed` 를 부른 스레드(`pump` 스레드)에서 실행됩니다. 보내지 못한 경우만 `sendRequest` 를 부른 스레드에서 실행됩니다.
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

        /** @brief 요청을 보내고 요청 ID를 반환합니다. 0이면 보내지 못한 것이며, 이때 콜백은 이미 ConnectionLost 나 Overloaded 로 불렸습니다. */
        uint64 sendRequest( StreamConnectionHandle handle, uint16 method, const uint8* pBody, int32 bodySize, const NetRequestOptions& options,
                            Delegate<void( const NetResponse& )> onResponse );
        /** @brief 요청을 취소합니다. 서버에 Cancel 을 보내고 콜백을 Cancelled 로 바로 부릅니다. 이미 끝났으면 false 를 반환합니다. */
        bool cancel( uint64 requestID );

        /** @brief Response 프레임이면 처리하고 true 를 반환합니다. */
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

        mutable mutex                              _mutex;
        unordered_map<uint64, Pending> _mapPending SW_GUARDED_BY( _mutex );
        BitWriter _writer                          SW_GUARDED_BY( _mutex ); ///< _mutex 안에서만 쓴다.
        StreamMessageEndpoint* _pEndpoint          SW_GUARDED_BY( _mutex );
        IDAllocator<uint64> _requestIDAllocator    SW_GUARDED_BY( _mutex );
        int32 _maxPendingRequests                  SW_GUARDED_BY( _mutex );
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetRequestServer
     * @brief 메서드마다 처리기 하나를 두고, 요청 헤더를 읽어 멱등 키를 확인한 뒤 처리기를 호출합니다.
     * @details 이미 끝난 키면 기억해 둔 응답을 돌려주고, 진행 중인 키면 그 응답을 함께 받게 한다.
     *          멱등 범위는 (주체, 메서드, 키)다. 주체는 로그인 키트가 `setPrincipal` 로 붙인다. 그래야 끊겼다가 다시 붙은 연결의 재시도가 같은 키로 잡힌다.
     *          주체가 0이면 범위는 연결이다(재접속 뒤에는 잡히지 않으므로 상태를 바꾸는 메서드는 로그인 뒤에만 연다).
     *          잠금 순서는 `_mutex` 를 푼 뒤에 엔드포인트의 연결 잠금을 잡는 것이다. `sendResponse` 는 `_writerMutex`, 엔드포인트 잠금 순서로 잡는다.
     */
    class SW_API NetRequestServer
    {
    public:
        NetRequestServer();

        void initialize( StreamMessageEndpoint* pEndpoint, const NetRequestServerConfig& settings );
        void shutdown();

        /** @brief 메서드에 처리기를 등록합니다. 이미 등록된 메서드면 오류 로그를 남기고 false 를 반환하며 앞 처리기를 그대로 둡니다(키트 둘이 같은 번호를 쓰는 조립). */
        [[nodiscard]] bool registerMethod( uint16 method, const NetRequestFunction& function );
        void               unregisterMethod( uint16 method );
        void               setPrincipal( StreamConnectionHandle handle, uint64 principalID );

        /** @brief 응답합니다(어느 스레드에서나 가능). 이미 응답했으면 false 를 반환합니다. 연결이 닫혔어도 멱등 키가 있으면 응답을 기억하므로 재시도가 받습니다. */
        bool respond( const NetRequestToken& token, NetRequestStatus status, const uint8* pBody, int32 bodySize );
        /** @brief 클라이언트가 취소했는지 반환합니다. 오래 걸리는 처리기가 중간에 확인합니다. */
        bool isCancelled( const NetRequestToken& token ) const;

        /** @brief Request 와 Cancel 프레임이면 처리하고 true 를 반환합니다. */
        [[nodiscard]] bool handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize );
        void               onConnectionClosed( StreamConnectionHandle handle );
        /** @brief 멱등 기억의 수명을 정리합니다. */
        void update();

    private:
        /** @brief 멱등 범위입니다. (주체, 메서드, 키)이며, 주체가 0이면 연결(packed)이 대신하고 `_bConnectionScope` 를 켭니다. */
        struct ScopeKey
        {
            uint64 _owner{ 0 };
            uint64 _keyHigh{ 0 };
            uint64 _keyLow{ 0 };
            uint16 _method{ 0 };
            uint8  _bConnectionScope{ SW_FALSE };

            bool operator==( const ScopeKey& other ) const;
            /** @brief 키의 해시입니다(`unordered_map` 의 기본 해시 `DefaultHash` 가 부릅니다). */
            [[nodiscard]] uint64 computeHash() const;
        };
        struct RequestKey
        {
            uint64 _handlePacked{ 0 };
            uint64 _requestID{ 0 };

            bool operator==( const RequestKey& other ) const { return _handlePacked == other._handlePacked && _requestID == other._requestID; }
            /** @brief 키의 해시입니다(`unordered_map` 의 기본 해시 `DefaultHash` 가 부릅니다). */
            [[nodiscard]] uint64 computeHash() const { return HashUtil::makeHash( _handlePacked, _requestID ); }
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
            vector<NetRequestToken> _listWaiter{}; ///< 처리 중에 같은 키로 온 요청. 첫 응답을 함께 받는다.
            int64                   _expireNanoseconds{ 0 };
            NetRequestStatus        _status{ NetRequestStatus::Ok };
            uint8                   _bInProgress{ SW_FALSE }; ///< 처리기가 아직 응답하지 않았다.
            uint8                   _bDone{ SW_FALSE };
        };

        /** @brief 요청 프레임의 머리(id · 메서드 · 시한 · 멱등 키 · 추적 id)와 몸의 위치입니다. */
        struct RequestHeader
        {
            NetIdempotencyKey _key{};
            LogTraceID        _traceID{};
            uint64            _requestID{ 0 };
            uint64            _timeoutMilli{ 0 };
            const uint8*      _pPayload{ nullptr };
            int32             _payloadSize{ 0 };
            uint16            _method{ 0 };
        };
        /** @brief 요청 하나를 받을지 정한 결과입니다. 잠금 안에서 정하고 잠금 밖에서 따른다. */
        struct RequestAdmission
        {
            enum class Action : uint8
            {
                Dispatch = 0, ///< 처리기를 부른다
                Reply,        ///< 바로 답한다(모르는 메서드 · 과부하 · 기억한 응답)
                Wait          ///< 처리 중인 같은 키 — 첫 응답을 같이 받는다
            };
            NetRequestContext  _context{};
            vector<uint8>      _replyBytes{};
            NetRequestFunction _function{};
            NetRequestStatus   _replyStatus{ NetRequestStatus::Ok };
            Action             _action{ Action::Dispatch };
        };

        /** @brief 취소 프레임: 처리 중인 그 요청에 취소 표시를 남깁니다(처리기가 `isCancelled` 로 본다). */
        void handleCancelFrame( StreamConnectionHandle handle, const uint8* pBody, int32 bodySize );
        /** @brief 요청 프레임의 머리를 읽습니다. 깨졌으면 false 이고, 그때 `_requestID` 는 읽은 만큼(못 읽었으면 0)입니다. */
        [[nodiscard]] static bool readRequestHeader( const uint8* pBody, int32 bodySize, RequestHeader& outHeader );
        /** @brief 메서드 · 처리 중 수 · 멱등 키로 요청을 받을지 정하고, 받으면 처리 중 목록에 올립니다(`_mutex` 를 잡는다). */
        RequestAdmission admitRequest( StreamConnectionHandle handle, const RequestHeader& header );
        void             sendResponse( StreamConnectionHandle handle, uint64 requestID, NetRequestStatus status, const uint8* pBody, int32 bodySize );
        void             purgeIdempotencyLocked( int64 now ) SW_REQUIRES_LOCK( _mutex );

        mutable mutex                                             _mutex;
        unordered_map<uint16, NetRequestFunction> _mapHandler     SW_GUARDED_BY( _mutex );
        unordered_map<uint32, InFlight> _mapInFlight              SW_GUARDED_BY( _mutex ); ///< 일련번호에서 처리 중인 요청으로 가는 맵.
        unordered_map<RequestKey, uint32> _mapSerialByRequest     SW_GUARDED_BY( _mutex );
        unordered_map<uint64, int32> _mapInFlightCount            SW_GUARDED_BY( _mutex ); ///< 연결에서 처리 중인 요청 수로 가는 맵.
        unordered_map<uint64, uint64> _mapPrincipal               SW_GUARDED_BY( _mutex ); ///< 연결에서 주체로 가는 맵.
        unordered_map<ScopeKey, IdempotencyEntry> _mapIdempotency SW_GUARDED_BY( _mutex );
        deque<pair<ScopeKey, int64>> _listExpiry                  SW_GUARDED_BY( _mutex ); ///< 끝난 기억의 만료 순서.
        mutex                                                     _writerMutex;
        BitWriter _writer                                         SW_GUARDED_BY( _writerMutex ); ///< _writerMutex 안에서만 쓴다.
        OnceInit<NetRequestServerConfig>                          _settings;                     ///< initialize 가 한 번 정한다.
        OnceInit<StreamMessageEndpoint*>                          _pEndpoint;                    ///< initialize 가 한 번 정한다.
        uint32 _nextSerial                                        SW_GUARDED_BY( _mutex );
    };
} // namespace sw
