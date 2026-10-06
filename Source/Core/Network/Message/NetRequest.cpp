#include "pch.h"

#include "Core/Network/Message/NetRequest.h"

#include "Core/Common/HashUtil.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Time/MonotonicClock.h"

#include <mutex>
#include <random>

SW_LOG_CALLER( "NetRequest" );

namespace sw
{
    namespace
    {
        struct NetRequestInternal
        {
            static uint64 mixHash( uint64 hash, uint64 value )
            {
                hash = HashUtil::combine( hash, value );
                return hash;
            }

            /** @brief 바이트 정렬한 뒤의 몸 — 읽은 비트를 바이트로 올림한 자리부터 끝까지입니다. */
            static void findPayload( const BitReader& reader, const uint8* pBody, int32 bodySize, const uint8*& pOutPayload, int32& outPayloadSize )
            {
                const int32 offset = ( reader.getBitPosition() + 7 ) / 8;
                pOutPayload        = pBody + offset;
                outPayloadSize     = bodySize - offset;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( NetRequestStatus status )
    {
        switch ( status )
        {
            case NetRequestStatus::Ok:
                return "Ok";
            case NetRequestStatus::ApplicationError:
                return "ApplicationError";
            case NetRequestStatus::DeadlineExceeded:
                return "DeadlineExceeded";
            case NetRequestStatus::Cancelled:
                return "Cancelled";
            case NetRequestStatus::ConnectionLost:
                return "ConnectionLost";
            case NetRequestStatus::UnknownMethod:
                return "UnknownMethod";
            case NetRequestStatus::Malformed:
                return "Malformed";
            case NetRequestStatus::Overloaded:
                return "Overloaded";
            case NetRequestStatus::Count:
                break;
        }
        return "Unknown";
    }

    NetIdempotencyKey NetIdempotencyKey::makeRandom()
    {
        std::random_device randomDevice; // 운영체제 난수(Windows RtlGenRandom · 리눅스 getrandom)
        NetIdempotencyKey  key;
        key._high = ( static_cast<uint64>( randomDevice() ) << 32 ) | randomDevice();
        key._low  = ( static_cast<uint64>( randomDevice() ) << 32 ) | randomDevice();
        if ( key.isValid() == false )
            key._low = 1;
        return key;
    }
} // namespace sw

namespace sw
{
    NetRequestClient::NetRequestClient()
        : _mutex{}
        , _mapPending{}
        , _writer{}
        , _pEndpoint{ nullptr }
        , _nextRequestId{ 1 }
        , _maxPendingRequests{ 1024 }
    {
    }

    void NetRequestClient::initialize( StreamMessageEndpoint* pEndpoint, int32 maxPendingRequests )
    {
        _pEndpoint          = pEndpoint;
        _maxPendingRequests = maxPendingRequests;
    }

    void NetRequestClient::shutdown()
    {
        unordered_map<uint64, Pending> mapPending;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            mapPending = std::move( _mapPending );
            _mapPending.clear();
        }
        for ( auto& [requestId, pending] : mapPending )
            finish( requestId, std::move( pending ), NetRequestStatus::Cancelled, nullptr, 0 );
    }

    void NetRequestClient::finish( uint64 requestId, Pending&& pending, NetRequestStatus status, const uint8* pBody, int32 bodySize )
    {
        if ( pending._onResponse.isBound() == false )
            return;
        NetResponse response;
        response._pBody     = pBody;
        response._bodySize  = bodySize;
        response._requestId = requestId;
        response._method    = pending._method;
        response._status    = status;
        pending._onResponse( response );
    }

    uint64 NetRequestClient::sendRequest( StreamConnectionHandle handle, uint16 method, const uint8* pBody, int32 bodySize, const NetRequestOptions& options,
                                          Delegate<void( const NetResponse& )> onResponse )
    {
        Pending pending;
        pending._onResponse          = std::move( onResponse );
        pending._handle              = handle;
        pending._method              = method;
        pending._deadlineNanoseconds = MonotonicClock::nowNanoseconds() + static_cast<int64>( options._timeoutSeconds * 1.0e9 );
        NetRequestStatus failure     = NetRequestStatus::Ok;
        uint64           requestId   = 0;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _pEndpoint == nullptr || static_cast<int32>( _mapPending.size() ) >= _maxPendingRequests )
            {
                failure = NetRequestStatus::Overloaded;
            }
            else
            {
                requestId = _nextRequestId++;
                _writer.clear();
                _writer.writeVarUint( requestId );
                _writer.writeBits( method, 16 );
                _writer.writeVarUint( static_cast<uint64>( options._timeoutSeconds * 1000.0 ) );
                _writer.writeBool( options._idempotencyKey.isValid() );
                if ( options._idempotencyKey.isValid() )
                {
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._high >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._high ), 32 );
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._low >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._low ), 32 );
                }
                const LogTraceId traceId = options._traceId.isValid() ? options._traceId : LogContext::getCurrent()._traceId;
                _writer.writeBool( traceId.isValid() );
                if ( traceId.isValid() )
                {
                    _writer.writeBits( static_cast<uint32>( traceId._high >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( traceId._high ), 32 );
                    _writer.writeBits( static_cast<uint32>( traceId._low >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( traceId._low ), 32 );
                }
                _writer.alignToByte();
                if ( bodySize > 0 )
                    _writer.writeBytes( pBody, bodySize );
                // 줄에 먼저 넣는다 — 같은 연결의 응답이 다른 스레드의 pump 로 보내기보다 먼저 올 수 있다.
                _mapPending.emplace( requestId, std::move( pending ) );
                const StreamSendResult result = _pEndpoint->sendFrame( handle, StreamFrameKind::Request, _writer.getBytes().data(), _writer.getByteCount() );
                if ( result == StreamSendResult::Closed || result == StreamSendResult::QueueFull )
                {
                    pending = std::move( _mapPending[requestId] );
                    _mapPending.erase( requestId );
                    failure = NetRequestStatus::ConnectionLost;
                }
            }
        }
        if ( failure != NetRequestStatus::Ok )
        {
            finish( requestId, std::move( pending ), failure, nullptr, 0 );
            return 0;
        }
        return requestId;
    }

    bool NetRequestClient::cancel( uint64 requestId )
    {
        Pending pending;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              found = _mapPending.find( requestId );
            if ( found == _mapPending.end() )
                return false;
            pending = std::move( found->second );
            _mapPending.erase( found );
            _writer.clear();
            _writer.writeVarUint( requestId );
            (void)_pEndpoint->sendFrame( pending._handle, StreamFrameKind::Cancel, _writer.getBytes().data(), _writer.getByteCount() );
        }
        finish( requestId, std::move( pending ), NetRequestStatus::Cancelled, nullptr, 0 );
        return true;
    }

    bool NetRequestClient::handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( kind != StreamFrameKind::Response )
            return false;
        BitReader              reader( pBody, bodySize );
        const uint64           requestId = reader.readVarUint();
        const NetRequestStatus status    = static_cast<NetRequestStatus>( reader.readBits( 8 ) );
        if ( reader.hasOverflowed() || status >= NetRequestStatus::Count )
            return true; // 깨진 응답 — 그 요청은 시한으로 끝난다
        Pending pending;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              found = _mapPending.find( requestId );
            if ( found == _mapPending.end() || found->second._handle != handle )
                return true; // 이미 끝났다(시한 · 취소) — 늦은 응답
            pending = std::move( found->second );
            _mapPending.erase( found );
        }
        const uint8* pPayload    = nullptr;
        int32        payloadSize = 0;
        NetRequestInternal::findPayload( reader, pBody, bodySize, pPayload, payloadSize );
        finish( requestId, std::move( pending ), status, pPayload, payloadSize );
        return true;
    }

    void NetRequestClient::onConnectionClosed( StreamConnectionHandle handle )
    {
        vector<pair<uint64, Pending>> listLost;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto iterator = _mapPending.begin(); iterator != _mapPending.end(); )
            {
                if ( iterator->second._handle == handle )
                {
                    listLost.emplace_back( iterator->first, std::move( iterator->second ) );
                    iterator = _mapPending.erase( iterator );
                }
                else
                {
                    ++iterator;
                }
            }
        }
        for ( auto& [requestId, pending] : listLost )
            finish( requestId, std::move( pending ), NetRequestStatus::ConnectionLost, nullptr, 0 );
    }

    void NetRequestClient::update()
    {
        const int64                   now = MonotonicClock::nowNanoseconds();
        vector<pair<uint64, Pending>> listExpired;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto iterator = _mapPending.begin(); iterator != _mapPending.end(); )
            {
                if ( iterator->second._deadlineNanoseconds <= now )
                {
                    listExpired.emplace_back( iterator->first, std::move( iterator->second ) );
                    iterator = _mapPending.erase( iterator );
                }
                else
                {
                    ++iterator;
                }
            }
        }
        for ( auto& [requestId, pending] : listExpired )
            finish( requestId, std::move( pending ), NetRequestStatus::DeadlineExceeded, nullptr, 0 );
    }

    int32 NetRequestClient::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return static_cast<int32>( _mapPending.size() );
    }
} // namespace sw

namespace sw
{
    bool NetRequestServer::ScopeKey::operator==( const ScopeKey& other ) const
    {
        return _owner == other._owner && _keyHigh == other._keyHigh && _keyLow == other._keyLow && _method == other._method && _bConnectionScope == other._bConnectionScope;
    }

    size_t NetRequestServer::ScopeKeyHash::operator()( const ScopeKey& key ) const
    {
        uint64 hash = NetRequestInternal::mixHash( key._owner, key._keyHigh );
        hash        = NetRequestInternal::mixHash( hash, key._keyLow );
        return static_cast<size_t>( NetRequestInternal::mixHash( hash, ( static_cast<uint64>( key._method ) << 1 ) | key._bConnectionScope ) );
    }

    size_t NetRequestServer::RequestKeyHash::operator()( const RequestKey& key ) const
    {
        return static_cast<size_t>( NetRequestInternal::mixHash( key._handlePacked, key._requestId ) );
    }

    NetRequestServer::NetRequestServer()
        : _mutex{}
        , _mapHandler{}
        , _mapInFlight{}
        , _mapSerialByRequest{}
        , _mapInFlightCount{}
        , _mapPrincipal{}
        , _mapIdempotency{}
        , _listExpiry{}
        , _writerMutex{}
        , _writer{}
        , _settings{}
        , _pEndpoint{ nullptr }
        , _nextSerial{ 1 }
    {
    }

    void NetRequestServer::initialize( StreamMessageEndpoint* pEndpoint, const NetRequestServerSettings& settings )
    {
        _pEndpoint = pEndpoint;
        _settings  = settings;
    }

    void NetRequestServer::shutdown()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapInFlight.clear();
        _mapSerialByRequest.clear();
        _mapInFlightCount.clear();
        _mapIdempotency.clear();
        _listExpiry.clear();
    }

    bool NetRequestServer::registerMethod( uint16 method, INetRequestHandler* pHandler )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const bool              bInserted = _mapHandler.emplace( method, pHandler ).second;
        if ( bInserted == false )
            SW_LOG_ERROR( "NetRequestServer: method %# is already registered - two handlers claim one method number", static_cast<uint32>( method ) );
        return bInserted;
    }

    void NetRequestServer::unregisterMethod( uint16 method )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapHandler.erase( method );
    }

    void NetRequestServer::setPrincipal( StreamConnectionHandle handle, uint64 principalId )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPrincipal[handle.packed()] = principalId;
    }

    void NetRequestServer::sendResponse( StreamConnectionHandle handle, uint64 requestId, NetRequestStatus status, const uint8* pBody, int32 bodySize )
    {
        std::scoped_lock<mutex> lock{ _writerMutex };
        _writer.clear();
        _writer.writeVarUint( requestId );
        _writer.writeBits( static_cast<uint32>( status ), 8 );
        _writer.alignToByte();
        if ( bodySize > 0 )
            _writer.writeBytes( pBody, bodySize );
        (void)_pEndpoint->sendFrame( handle, StreamFrameKind::Response, _writer.getBytes().data(), _writer.getByteCount() );
    }

    bool NetRequestServer::handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( kind == StreamFrameKind::Cancel )
        {
            BitReader    reader( pBody, bodySize );
            const uint64 requestId = reader.readVarUint();
            if ( reader.hasOverflowed() )
                return true;
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              found = _mapSerialByRequest.find( RequestKey{ handle.packed(), requestId } );
            if ( found != _mapSerialByRequest.end() )
                _mapInFlight[found->second]._bCancelled = SW_TRUE;
            return true;
        }
        if ( kind != StreamFrameKind::Request )
            return false;

        BitReader         reader( pBody, bodySize );
        const uint64      requestId    = reader.readVarUint();
        const uint16      method       = static_cast<uint16>( reader.readBits( 16 ) );
        const uint64      timeoutMilli = reader.readVarUint();
        const bool        bHasKey      = reader.readBool();
        NetIdempotencyKey key;
        if ( bHasKey )
        {
            key._high = ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 ) | reader.readBits( 32 );
            key._low  = ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 ) | reader.readBits( 32 );
        }
        const bool bHasTraceId = reader.readBool();
        LogTraceId traceId;
        if ( bHasTraceId )
        {
            traceId._high = ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 ) | reader.readBits( 32 );
            traceId._low  = ( static_cast<uint64>( reader.readBits( 32 ) ) << 32 ) | reader.readBits( 32 );
        }
        if ( traceId.isValid() == false )
            traceId = LogTraceId::makeRandom(); // 클라이언트가 싣지 않았으면 서버가 만든다 — 서버 쪽 줄은 늘 요청마다 갈린다
        if ( reader.hasOverflowed() )
        {
            sendResponse( handle, requestId, NetRequestStatus::Malformed, nullptr, 0 ); // id 를 못 읽었으면 0 — 클라이언트는 시한으로 끝낸다
            return true;
        }
        const uint8* pPayload    = nullptr;
        int32        payloadSize = 0;
        NetRequestInternal::findPayload( reader, pBody, bodySize, pPayload, payloadSize );

        enum class RequestAction : uint8
        {
            Dispatch = 0, ///< 처리기를 부른다
            Reply,        ///< 바로 답한다(모르는 메서드 · 과부하 · 기억한 응답)
            Wait          ///< 처리 중인 같은 키 — 첫 응답을 같이 받는다
        };
        RequestAction       action      = RequestAction::Dispatch;
        NetRequestStatus    replyStatus = NetRequestStatus::Ok;
        vector<uint8>       replyBytes;
        NetRequestContext   context;
        INetRequestHandler* pHandler = nullptr;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              handlerFound  = _mapHandler.find( method );
            int32&                  inFlightCount = _mapInFlightCount[handle.packed()];
            if ( handlerFound == _mapHandler.end() )
            {
                action      = RequestAction::Reply;
                replyStatus = NetRequestStatus::UnknownMethod;
            }
            else if ( inFlightCount >= _settings._maxInFlightPerConnection )
            {
                action      = RequestAction::Reply;
                replyStatus = NetRequestStatus::Overloaded;
            }
            else
            {
                pHandler                    = handlerFound->second;
                const auto   principalFound = _mapPrincipal.find( handle.packed() );
                const uint64 principalId    = principalFound != _mapPrincipal.end() ? principalFound->second : 0;
                InFlight     inFlight;
                inFlight._token = NetRequestToken{ handle, requestId, _nextSerial++, method };
                if ( key.isValid() )
                {
                    const bool bPrincipal   = principalId != 0;
                    inFlight._scope         = ScopeKey{ bPrincipal ? principalId : handle.packed(), key._high, key._low, method, static_cast<uint8>( bPrincipal ? SW_FALSE : SW_TRUE ) };
                    IdempotencyEntry& entry = _mapIdempotency[inFlight._scope];
                    if ( entry._bDone == SW_TRUE )
                    {
                        action      = RequestAction::Reply; // 이미 끝난 키 — 처리하지 않고 기억한 응답
                        replyStatus = entry._status;
                        replyBytes  = entry._body;
                    }
                    else if ( entry._bInProgress == SW_TRUE )
                    {
                        action = RequestAction::Wait;
                        entry._listWaiter.push_back( inFlight._token );
                    }
                    else
                    {
                        entry._bInProgress    = SW_TRUE;
                        inFlight._bIdempotent = SW_TRUE;
                    }
                }
                if ( action == RequestAction::Dispatch )
                {
                    ++inFlightCount;
                    _mapSerialByRequest[RequestKey{ handle.packed(), requestId }] = inFlight._token._serial;
                    context._token                                                = inFlight._token;
                    context._idempotencyKey                                       = key;
                    context._traceId                                              = traceId;
                    context._principalId                                          = principalId;
                    context._deadlineSeconds                                      = static_cast<float64>( MonotonicClock::nowNanoseconds() ) * 1.0e-9 + static_cast<float64>( timeoutMilli ) * 1.0e-3;
                    context._pBody                                                = pPayload;
                    context._bodySize                                             = payloadSize;
                    _mapInFlight.emplace( inFlight._token._serial, inFlight );
                }
            }
        }
        switch ( action )
        {
            case RequestAction::Dispatch:
            {
                ScopedLogContext scope( LogContext{ context._traceId, context._principalId } ); // 처리기 · 그가 맡긴 저장소 일의 줄에 같은 꼬리표
                pHandler->onNetRequest( *this, context );                                       // 어떤 잠금도 쥐지 않은 채
                break;
            }
            case RequestAction::Reply:
            {
                sendResponse( handle, requestId, replyStatus, replyBytes.data(), static_cast<int32>( replyBytes.size() ) );
                break;
            }
            case RequestAction::Wait:
            {
                break;
            }
        }
        return true;
    }

    bool NetRequestServer::respond( const NetRequestToken& token, NetRequestStatus status, const uint8* pBody, int32 bodySize )
    {
        vector<NetRequestToken> listWaiter;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              found = _mapInFlight.find( token._serial );
            if ( found == _mapInFlight.end() )
                return false; // 이미 답했다
            const InFlight inFlight = found->second;
            _mapInFlight.erase( found );
            _mapSerialByRequest.erase( RequestKey{ token._handle.packed(), token._requestId } );
            const auto countFound = _mapInFlightCount.find( token._handle.packed() );
            if ( countFound != _mapInFlightCount.end() && --countFound->second <= 0 )
                _mapInFlightCount.erase( countFound );
            if ( inFlight._bIdempotent == SW_TRUE )
            {
                IdempotencyEntry& entry = _mapIdempotency[inFlight._scope];
                entry._status           = status;
                entry._body.assign( pBody, pBody + bodySize );
                entry._bInProgress       = SW_FALSE;
                entry._bDone             = SW_TRUE;
                entry._expireNanoseconds = MonotonicClock::nowNanoseconds() + static_cast<int64>( _settings._idempotencyTtlSeconds * 1.0e9 );
                listWaiter.swap( entry._listWaiter );
                _listExpiry.emplace_back( inFlight._scope, entry._expireNanoseconds );
                purgeIdempotencyLocked( MonotonicClock::nowNanoseconds() );
            }
        }
        sendResponse( token._handle, token._requestId, status, pBody, bodySize ); // 연결이 닫혔으면 끝점이 Closed 로 버린다 — 기억은 이미 했다
        for ( const NetRequestToken& waiter : listWaiter )
            sendResponse( waiter._handle, waiter._requestId, status, pBody, bodySize );
        return true;
    }

    bool NetRequestServer::isCancelled( const NetRequestToken& token ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              found = _mapInFlight.find( token._serial );
        return found != _mapInFlight.end() && found->second._bCancelled == SW_TRUE;
    }

    void NetRequestServer::onConnectionClosed( StreamConnectionHandle handle )
    {
        // 처리 중 기록은 지우지 않는다 — 처리기가 나중에 답하면 멱등 기억이 남아 재접속한 재시도가 받는다. 연결별 수 · 주체만 지운다.
        std::scoped_lock<mutex> lock{ _mutex };
        _mapInFlightCount.erase( handle.packed() );
        _mapPrincipal.erase( handle.packed() );
    }

    void NetRequestServer::update()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        purgeIdempotencyLocked( MonotonicClock::nowNanoseconds() );
    }

    void NetRequestServer::purgeIdempotencyLocked( int64 now )
    {
        const bool bOverCapacity = static_cast<int32>( _mapIdempotency.size() ) > _settings._maxIdempotencyEntries;
        while ( _listExpiry.empty() == false && ( _listExpiry.front().second <= now || bOverCapacity ) )
        {
            const auto found = _mapIdempotency.find( _listExpiry.front().first );
            if ( found != _mapIdempotency.end() && found->second._bDone == SW_TRUE && found->second._expireNanoseconds == _listExpiry.front().second )
                _mapIdempotency.erase( found );
            _listExpiry.pop_front();
            if ( bOverCapacity && static_cast<int32>( _mapIdempotency.size() ) <= _settings._maxIdempotencyEntries )
                break;
        }
    }
} // namespace sw
