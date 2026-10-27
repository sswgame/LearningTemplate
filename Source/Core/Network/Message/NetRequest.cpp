#include "pch.h"

#include "Core/Network/Message/NetRequest.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Network/Message/StreamMessageEndpoint.h"
#include "Core/Time/MonotonicClock.h"

#include <mutex>

SW_LOG_CALLER( "NetRequest" );

namespace sw
{
    namespace
    {
        struct NetRequestInternal
        {
            /** @brief 바이트 정렬한 뒤의 몸 — 읽은 비트를 바이트로 올림한 위치부터 끝까지입니다. */
            static void findPayload( const BitReader& reader, const uint8* pBody, int32 bodySize, const uint8*& pOutPayload, int32& outPayloadSize )
            {
                const int32 offset = ( reader.getBitPosition() + 7 ) / 8;
                pOutPayload        = pBody + offset;
                outPayloadSize     = bodySize - offset;
            }

            /** @brief 클라이언트가 쓴 순서(위 32비트 먼저)로 64비트 값을 읽습니다. 두 번의 읽기를 한 식에 두면 순서가 정해지지 않아 문장으로 나눈다. */
            static uint64 readHighFirstUint64( BitReader& reader )
            {
                const uint64 high = reader.readBits( 32 );
                const uint64 low  = reader.readBits( 32 );
                return ( high << 32 ) | low;
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
        NetIdempotencyKey key;
        key._high = SecureRandom::nextUint64();
        key._low  = SecureRandom::nextUint64();
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
        , _requestIDAllocator{}
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
            ScopedLock<mutex> lock{ _mutex };
            mapPending = std::move( _mapPending );
            _mapPending.clear();
        }
        for ( auto& [requestID, pending] : mapPending )
        {
            finish( requestID, std::move( pending ), NetRequestStatus::Cancelled, nullptr, 0 );
        }
    }

    void NetRequestClient::finish( uint64 requestID, Pending&& pending, NetRequestStatus status, const uint8* pBody, int32 bodySize )
    {
        if ( pending._onResponse.isBound() == false )
            return;
        NetResponse response;
        response._pBody     = pBody;
        response._bodySize  = bodySize;
        response._requestID = requestID;
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
        pending._deadlineNanoseconds = MonotonicClock::nowNanoseconds() + static_cast<int64>( options._timeoutSeconds * constant::kNanosecondsPerSecond );
        NetRequestStatus failure     = NetRequestStatus::Ok;
        uint64           requestID   = 0;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _pEndpoint == nullptr || static_cast<int32>( _mapPending.size() ) >= _maxPendingRequests )
            {
                failure = NetRequestStatus::Overloaded;
            }
            else
            {
                requestID = _requestIDAllocator.allocate();
                _writer.clear();
                _writer.writeVarUint( requestID );
                _writer.writeBits( method, 16 );
                _writer.writeVarUint( static_cast<uint64>( options._timeoutSeconds * constant::kMillisecondsPerSecond ) );
                _writer.writeBool( options._idempotencyKey.isValid() );
                if ( options._idempotencyKey.isValid() )
                {
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._high >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._high ), 32 );
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._low >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( options._idempotencyKey._low ), 32 );
                }
                const LogTraceID traceID = options._traceID.isValid() ? options._traceID : LogContext::getCurrent()._traceID;
                _writer.writeBool( traceID.isValid() );
                if ( traceID.isValid() )
                {
                    _writer.writeBits( static_cast<uint32>( traceID._high >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( traceID._high ), 32 );
                    _writer.writeBits( static_cast<uint32>( traceID._low >> 32 ), 32 );
                    _writer.writeBits( static_cast<uint32>( traceID._low ), 32 );
                }
                _writer.alignToByte();
                if ( bodySize > 0 )
                    _writer.writeBytes( pBody, bodySize );
                // 줄에 먼저 넣는다 — 같은 연결의 응답이 다른 스레드의 pump 로 보내기보다 먼저 올 수 있다.
                _mapPending.emplace( requestID, std::move( pending ) );
                const StreamSendResult result = _pEndpoint->sendFrame( handle, StreamFrameKind::Request, _writer.getBytes().data(), _writer.getByteCount() );
                if ( result == StreamSendResult::Closed || result == StreamSendResult::QueueFull )
                {
                    pending = std::move( _mapPending[requestID] );
                    _mapPending.erase( requestID );
                    failure = NetRequestStatus::ConnectionLost;
                }
            }
        }
        if ( failure != NetRequestStatus::Ok )
        {
            finish( requestID, std::move( pending ), failure, nullptr, 0 );
            return 0;
        }
        return requestID;
    }

    bool NetRequestClient::cancel( uint64 requestID )
    {
        Pending pending;
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        found = _mapPending.find( requestID );
            if ( found == _mapPending.end() )
                return false;
            pending = std::move( found->second );
            _mapPending.erase( found );
            _writer.clear();
            _writer.writeVarUint( requestID );
            (void)_pEndpoint->sendFrame( pending._handle, StreamFrameKind::Cancel, _writer.getBytes().data(), _writer.getByteCount() );
        }
        finish( requestID, std::move( pending ), NetRequestStatus::Cancelled, nullptr, 0 );
        return true;
    }

    bool NetRequestClient::handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( kind != StreamFrameKind::Response )
            return false;
        BitReader              reader( pBody, bodySize );
        const uint64           requestID = reader.readVarUint();
        const NetRequestStatus status    = static_cast<NetRequestStatus>( reader.readBits( 8 ) );
        if ( reader.hasOverflowed() || status >= NetRequestStatus::Count )
            return true; // 깨진 응답 — 그 요청은 시한으로 끝난다
        Pending pending;
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        found = _mapPending.find( requestID );
            if ( found == _mapPending.end() || found->second._handle != handle )
                return true; // 이미 끝났다(시한 · 취소) — 늦은 응답
            pending = std::move( found->second );
            _mapPending.erase( found );
        }
        const uint8* pPayload    = nullptr;
        int32        payloadSize = 0;
        NetRequestInternal::findPayload( reader, pBody, bodySize, pPayload, payloadSize );
        finish( requestID, std::move( pending ), status, pPayload, payloadSize );
        return true;
    }

    void NetRequestClient::onConnectionClosed( StreamConnectionHandle handle )
    {
        vector<pair<uint64, Pending>> listLost;
        {
            ScopedLock<mutex> lock{ _mutex };
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
        for ( auto& [requestID, pending] : listLost )
        {
            finish( requestID, std::move( pending ), NetRequestStatus::ConnectionLost, nullptr, 0 );
        }
    }

    void NetRequestClient::update()
    {
        const int64                   now = MonotonicClock::nowNanoseconds();
        vector<pair<uint64, Pending>> listExpired;
        {
            ScopedLock<mutex> lock{ _mutex };
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
        for ( auto& [requestID, pending] : listExpired )
        {
            finish( requestID, std::move( pending ), NetRequestStatus::DeadlineExceeded, nullptr, 0 );
        }
    }

    int32 NetRequestClient::getPendingCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<int32>( _mapPending.size() );
    }
} // namespace sw

namespace sw
{
    bool NetRequestServer::ScopeKey::operator==( const ScopeKey& other ) const
    {
        return _owner == other._owner && _keyHigh == other._keyHigh && _keyLow == other._keyLow && _method == other._method && _bConnectionScope == other._bConnectionScope;
    }

    uint64 NetRequestServer::ScopeKey::computeHash() const
    {
        return HashUtil::makeHash( _owner, _keyHigh, _keyLow, _method, _bConnectionScope );
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

    void NetRequestServer::initialize( StreamMessageEndpoint* pEndpoint, const NetRequestServerConfig& settings )
    {
        _pEndpoint = pEndpoint;
        _settings  = settings;
    }

    void NetRequestServer::shutdown()
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapInFlight.clear();
        _mapSerialByRequest.clear();
        _mapInFlightCount.clear();
        _mapIdempotency.clear();
        _listExpiry.clear();
    }

    bool NetRequestServer::registerMethod( uint16 method, INetRequestHandler* pHandler )
    {
        ScopedLock<mutex> lock{ _mutex };
        const bool        bInserted = _mapHandler.emplace( method, pHandler ).second;
        if ( bInserted == false )
            SW_LOG_ERROR( "NetRequestServer: method %# is already registered - two handlers claim one method number", static_cast<uint32>( method ) );
        return bInserted;
    }

    void NetRequestServer::unregisterMethod( uint16 method )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapHandler.erase( method );
    }

    void NetRequestServer::setPrincipal( StreamConnectionHandle handle, uint64 principalID )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapPrincipal[handle.packed()] = principalID;
    }

    void NetRequestServer::sendResponse( StreamConnectionHandle handle, uint64 requestID, NetRequestStatus status, const uint8* pBody, int32 bodySize )
    {
        ScopedLock<mutex> lock{ _writerMutex };
        _writer.clear();
        _writer.writeVarUint( requestID );
        _writer.writeBits( static_cast<uint32>( status ), 8 );
        _writer.alignToByte();
        if ( bodySize > 0 )
            _writer.writeBytes( pBody, bodySize );
        (void)_pEndpoint->sendFrame( handle, StreamFrameKind::Response, _writer.getBytes().data(), _writer.getByteCount() );
    }

    void NetRequestServer::handleCancelFrame( StreamConnectionHandle handle, const uint8* pBody, int32 bodySize )
    {
        BitReader    reader( pBody, bodySize );
        const uint64 requestID = reader.readVarUint();
        if ( reader.hasOverflowed() )
            return;
        ScopedLock<mutex> lock{ _mutex };
        const auto        found = _mapSerialByRequest.find( RequestKey{ handle.packed(), requestID } );
        if ( found != _mapSerialByRequest.end() )
            _mapInFlight[found->second]._bCancelled = SW_TRUE;
    }

    bool NetRequestServer::readRequestHeader( const uint8* pBody, int32 bodySize, RequestHeader& outHeader )
    {
        BitReader reader( pBody, bodySize );
        outHeader               = RequestHeader{};
        outHeader._requestID    = reader.readVarUint();
        outHeader._method       = static_cast<uint16>( reader.readBits( 16 ) );
        outHeader._timeoutMilli = reader.readVarUint();
        if ( reader.readBool() )
        {
            outHeader._key._high = NetRequestInternal::readHighFirstUint64( reader );
            outHeader._key._low  = NetRequestInternal::readHighFirstUint64( reader );
        }
        if ( reader.readBool() )
        {
            outHeader._traceID._high = NetRequestInternal::readHighFirstUint64( reader );
            outHeader._traceID._low  = NetRequestInternal::readHighFirstUint64( reader );
        }
        if ( outHeader._traceID.isValid() == false )
            outHeader._traceID = LogTraceID::makeRandom(); // 클라이언트가 싣지 않았으면 서버가 만든다 — 서버 쪽 줄은 늘 요청마다 갈린다
        if ( reader.hasOverflowed() )
            return false;
        NetRequestInternal::findPayload( reader, pBody, bodySize, outHeader._pPayload, outHeader._payloadSize );
        return true;
    }

    NetRequestServer::RequestAdmission NetRequestServer::admitRequest( StreamConnectionHandle handle, const RequestHeader& header )
    {
        RequestAdmission  admission;
        ScopedLock<mutex> lock{ _mutex };
        const auto        handlerFound  = _mapHandler.find( header._method );
        int32&            inFlightCount = _mapInFlightCount[handle.packed()];
        if ( handlerFound == _mapHandler.end() || inFlightCount >= _settings._maxInFlightPerConnection )
        {
            admission._action      = RequestAdmission::Action::Reply;
            admission._replyStatus = handlerFound == _mapHandler.end() ? NetRequestStatus::UnknownMethod : NetRequestStatus::Overloaded;
            return admission;
        }
        admission._pHandler         = handlerFound->second;
        const auto   principalFound = _mapPrincipal.find( handle.packed() );
        const uint64 principalID    = principalFound != _mapPrincipal.end() ? principalFound->second : 0;
        InFlight     inFlight;
        inFlight._token = NetRequestToken{ handle, header._requestID, _nextSerial++, header._method };
        if ( header._key.isValid() )
        {
            const bool bPrincipal   = principalID != 0;
            inFlight._scope         = ScopeKey{ bPrincipal ? principalID : handle.packed(), header._key._high, header._key._low, header._method,
                                        static_cast<uint8>( bPrincipal ? SW_FALSE : SW_TRUE ) };
            IdempotencyEntry& entry = _mapIdempotency[inFlight._scope];
            if ( entry._bDone == SW_TRUE )
            {
                admission._action      = RequestAdmission::Action::Reply; // 이미 끝난 키 — 처리하지 않고 기억한 응답
                admission._replyStatus = entry._status;
                admission._replyBytes  = entry._body;
                return admission;
            }
            if ( entry._bInProgress == SW_TRUE )
            {
                admission._action = RequestAdmission::Action::Wait;
                entry._listWaiter.push_back( inFlight._token );
                return admission;
            }
            entry._bInProgress    = SW_TRUE;
            inFlight._bIdempotent = SW_TRUE;
        }
        ++inFlightCount;
        _mapSerialByRequest[RequestKey{ handle.packed(), header._requestID }] = inFlight._token._serial;
        NetRequestContext& context                                            = admission._context;
        context._token                                                        = inFlight._token;
        context._idempotencyKey                                               = header._key;
        context._traceID                                                      = header._traceID;
        context._principalID                                                  = principalID;
        context._deadlineSeconds                                              = static_cast<float64>( MonotonicClock::nowNanoseconds() ) * 1.0e-9 + static_cast<float64>( header._timeoutMilli ) * 1.0e-3;
        context._pBody                                                        = header._pPayload;
        context._bodySize                                                     = header._payloadSize;
        _mapInFlight.emplace( inFlight._token._serial, inFlight );
        return admission;
    }

    bool NetRequestServer::handleFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        if ( kind == StreamFrameKind::Cancel )
        {
            handleCancelFrame( handle, pBody, bodySize );
            return true;
        }
        if ( kind != StreamFrameKind::Request )
            return false;
        RequestHeader header;
        if ( readRequestHeader( pBody, bodySize, header ) == false )
        {
            sendResponse( handle, header._requestID, NetRequestStatus::Malformed, nullptr, 0 ); // id 를 못 읽었으면 0 — 클라이언트는 시한으로 끝낸다
            return true;
        }
        const RequestAdmission admission = admitRequest( handle, header );
        switch ( admission._action )
        {
            case RequestAdmission::Action::Dispatch:
            {
                ScopedLogContext scope( LogContext{ admission._context._traceID, admission._context._principalID } ); // 처리기 · 그가 맡긴 저장소 작업의 로그 줄에 같은 태그
                admission._pHandler->onNetRequest( *this, admission._context );                                       // 어떤 잠금도 쥐지 않은 채
                break;
            }
            case RequestAdmission::Action::Reply:
            {
                sendResponse( handle, header._requestID, admission._replyStatus, admission._replyBytes.data(), static_cast<int32>( admission._replyBytes.size() ) );
                break;
            }
            case RequestAdmission::Action::Wait:
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
            ScopedLock<mutex> lock{ _mutex };
            const auto        found = _mapInFlight.find( token._serial );
            if ( found == _mapInFlight.end() )
                return false; // 이미 답했다
            const InFlight inFlight = found->second;
            _mapInFlight.erase( found );
            _mapSerialByRequest.erase( RequestKey{ token._handle.packed(), token._requestID } );
            const auto foundCount = _mapInFlightCount.find( token._handle.packed() );
            if ( foundCount != _mapInFlightCount.end() && --foundCount->second <= 0 )
                _mapInFlightCount.erase( foundCount );
            if ( inFlight._bIdempotent == SW_TRUE )
            {
                IdempotencyEntry& entry = _mapIdempotency[inFlight._scope];
                entry._status           = status;
                entry._body.assign( pBody, pBody + bodySize );
                entry._bInProgress       = SW_FALSE;
                entry._bDone             = SW_TRUE;
                entry._expireNanoseconds = MonotonicClock::nowNanoseconds() + static_cast<int64>( _settings._idempotencyTtlSeconds * constant::kNanosecondsPerSecond );
                listWaiter.swap( entry._listWaiter );
                _listExpiry.emplace_back( inFlight._scope, entry._expireNanoseconds );
                purgeIdempotencyLocked( MonotonicClock::nowNanoseconds() );
            }
        }
        sendResponse( token._handle, token._requestID, status, pBody, bodySize ); // 연결이 닫혔으면 엔드포인트가 Closed 로 버린다 — 기억은 이미 했다
        for ( const NetRequestToken& waiter : listWaiter )
        {
            sendResponse( waiter._handle, waiter._requestID, status, pBody, bodySize );
        }
        return true;
    }

    bool NetRequestServer::isCancelled( const NetRequestToken& token ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        found = _mapInFlight.find( token._serial );
        return found != _mapInFlight.end() && found->second._bCancelled == SW_TRUE;
    }

    void NetRequestServer::onConnectionClosed( StreamConnectionHandle handle )
    {
        // 처리 중 기록은 지우지 않는다 — 처리기가 나중에 답하면 멱등 기억이 남아 재접속한 재시도가 받는다. 연결별 수 · 주체만 지운다.
        ScopedLock<mutex> lock{ _mutex };
        _mapInFlightCount.erase( handle.packed() );
        _mapPrincipal.erase( handle.packed() );
    }

    void NetRequestServer::update()
    {
        ScopedLock<mutex> lock{ _mutex };
        purgeIdempotencyLocked( MonotonicClock::nowNanoseconds() );
    }

    void NetRequestServer::purgeIdempotencyLocked( int64 now )
    {
        const bool bOverCapacity = static_cast<int32>( _mapIdempotency.size() ) > _settings._maxIdempotencyEntries;
        while ( _listExpiry.empty() == false && ( _listExpiry.front().second <= now || bOverCapacity ) )
        {
            const auto found                  = _mapIdempotency.find( _listExpiry.front().first );
            const bool bExpiredEntryCompleted = found != _mapIdempotency.end() && found->second._bDone == SW_TRUE && found->second._expireNanoseconds == _listExpiry.front().second;
            if ( bExpiredEntryCompleted )
                _mapIdempotency.erase( found );
            _listExpiry.pop_front();
            if ( bOverCapacity && static_cast<int32>( _mapIdempotency.size() ) <= _settings._maxIdempotencyEntries )
                break;
        }
    }
} // namespace sw
