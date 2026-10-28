#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Client.h"

#include "Core/Container/StringUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/PlatformSocketUtil.h"

namespace sw
{
    SW_LOG_CALLER( "HTTP2Client" );
} // namespace sw

namespace sw
{
    HTTP2Client::HTTP2Client( const IClock& clock )
        : _clock{ clock }
        , _mutex{}
        , _mapSession{}
        , _mapKeyToSession{}
        , _mapHandleToSession{}
        , _mapHostToTLSContext{}
        , _listDone{}
        , _transport{}
        , _settings{}
        , _resolver{}
        , _requestIDAllocator{}
        , _sessionIDAllocator{}
        , _ioThreadCount{ 0 }
        , _bInitialized{ false }
    {
    }

    HTTP2Client::~HTTP2Client()
    {
        shutdown();
    }

    bool HTTP2Client::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportConfig& transportSettings, const HTTP2ClientConfig& settings )
    {
        if ( transport == nullptr || _bInitialized.load( std::memory_order_acquire ) == true )
            return false;
        _transport.set( std::move( transport ) );
        _settings.set( settings );
        _ioThreadCount = transportSettings._ioThreadCount;
        if ( _transport->initialize( this, transportSettings ) == false )
        {
            SW_LOG_ERROR( "HTTP/2 client transport did not start" );
            _transport.reset();
            _settings.reset();
            return false;
        }
        _bInitialized.store( true, std::memory_order_release );
        return true;
    }

    void HTTP2Client::shutdown()
    {
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;
        _resolver.stop();       // 풀던 이름 하나는 해석기 시한까지 기다린다. 남은 이름은 풀지 않고 아래에서 끝낸다
        _transport->shutdown(); // 열린 연결은 Shutdown 으로 닫히며 onStreamClosed 가 요청을 끝낸다
        _transport.reset();
        _settings.reset();
        ScopedLock<mutex> lock{ _mutex };
        for ( auto& [sessionID, session] : _mapSession )
        {
            (void)sessionID;
            failSessionLocked( *session, "client shut down" );
        }
        _mapSession.clear();
        _mapKeyToSession.clear();
        _mapHandleToSession.clear();
        _bInitialized.store( false, std::memory_order_release );
    }

    void HTTP2Client::registerTLSContext( string_view host, ITLSContext* pTLSContext )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapHostToTLSContext[string( host )] = pTLSContext;
    }

    uint64 HTTP2Client::submitRequest( const HTTPClientRequest& request )
    {
        ScopedLock<mutex> lock{ _mutex };
        const uint64      requestID = _requestIDAllocator.allocate();
        HTTPAddress       url;
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
        {
            failImmediately( requestID, "client is not running" );
            return requestID;
        }
        if ( HTTPAddress::parse( request._url, url ) == false )
        {
            failImmediately( requestID, "malformed URL" );
            return requestID;
        }
        if ( url._bSecure == SW_FALSE )
        {
            failImmediately( requestID, "HTTP/2 needs an https:// URL" );
            return requestID;
        }
        int32 pendingCount = 0;
        for ( const auto& [sessionID, session] : _mapSession )
        {
            (void)sessionID;
            pendingCount += static_cast<int32>( session->_mapDeadline.size() );
        }
        if ( pendingCount >= _settings->_maxConcurrentRequests )
        {
            failImmediately( requestID, "too many requests in flight" );
            return requestID;
        }
        const auto contextIt = _mapHostToTLSContext.find( url._host );
        if ( contextIt == _mapHostToTLSContext.end() || contextIt->second == nullptr )
        {
            failImmediately( requestID, "no TLS context for this host" );
            return requestID;
        }
        const string key      = url._host + ":" + to_string( static_cast<int32>( url._port ) );
        Session*     pSession = nullptr;
        const auto   keyIt    = _mapKeyToSession.find( key );
        if ( keyIt != _mapKeyToSession.end() )
        {
            Session*   pCandidate = _mapSession[keyIt->second].get();
            const bool bUsable    = pCandidate->_bAccepting == SW_TRUE && ( pCandidate->_connection == nullptr || pCandidate->_connection->isUsable() );
            pSession              = bUsable ? pCandidate : nullptr; // 끊기는 중이거나 GOAWAY 를 받은 연결은 새 요청을 받지 않는다
        }
        if ( pSession == nullptr )
            pSession = createSessionLocked( url, contextIt->second, key );
        if ( pSession == nullptr )
        {
            failImmediately( requestID, "could not connect" );
            return requestID;
        }
        Pending& pending      = pSession->_listWaiting.emplace_back();
        pending._request      = request;
        pending._url          = url;
        pending._requestID    = requestID;
        const int64 timeoutMs = request._timeoutMs > 0 ? request._timeoutMs : HTTPConstant::kDefaultTimeoutMs;
        pSession->_mapDeadline.insert_or_assign( requestID, _clock.nowMilliseconds() + timeoutMs );
        if ( pSession->_state == SessionState::Ready && sendWaitingLocked( *pSession ) == false )
        {
            failSessionLocked( *pSession, "could not send the request" );
            _transport->close( pSession->_handle, StreamCloseMode::Abort );
        }
        return requestID;
    }

    HTTP2Client::Session* HTTP2Client::createSessionLocked( const HTTPAddress& url, ITLSContext* pTLSContext, string_view key )
    {
        unique_ptr<Session> session = make_unique<Session>();
        if ( session->_link.initialize( pTLSContext ) == false )
            return nullptr;
        session->_sessionID              = _sessionIDAllocator.allocate();
        session->_key                    = string( key );
        session->_host                   = url._host;
        Session*     pSession            = session.get();
        const uint64 sessionID           = pSession->_sessionID;
        _mapSession[sessionID]           = std::move( session );
        _mapKeyToSession[pSession->_key] = sessionID;
        if ( NetAddress::parse( url._host, url._port, pSession->_address ) )
        {
            if ( connectSessionLocked( *pSession ) == false )
            {
                eraseSessionLocked( sessionID );
                return nullptr;
            }
            return pSession;
        }
        // 이름이다 — 해석 스레드에 맡기고 `update` 가 연결을 건다(요청 경로에서 DNS 를 막고 기다리지 않는다).
        pSession->_address      = NetAddress::makeAnyInterface( url._port );
        pSession->_state        = SessionState::Resolving;
        pSession->_resolveState = ResolveState::Pending;
        if ( _resolver.isRunning() == false )
            _resolver.start( "HTTP2Resolve", DedicatedWorker<uint64>::ProcessDelegate::create<&HTTP2Client::resolveSession>( this ) );
        uint64 resolveJob = sessionID;
        (void)_resolver.submit( std::move( resolveJob ) ); // 바로 위에서 띄웠으니 받는다 — 못 받아도 요청은 시한이 끝낸다
        return pSession;
    }

    bool HTTP2Client::connectSessionLocked( Session& session )
    {
        const StreamConnectionHandle handle = _transport->connect( session._address );
        if ( handle.isValid() == false )
            return false;
        session._handle                      = handle;
        session._state                       = SessionState::Connecting;
        _mapHandleToSession[handle.packed()] = session._sessionID;
        return true;
    }

    void HTTP2Client::advanceResolvingLocked()
    {
        vector<uint64> listErase;
        for ( auto& [sessionID, session] : _mapSession )
        {
            if ( session->_state != SessionState::Resolving )
                continue;
            if ( session->_resolveState == ResolveState::Resolved )
            {
                if ( connectSessionLocked( *session ) == false )
                {
                    failSessionLocked( *session, "could not connect" );
                    listErase.push_back( sessionID );
                }
            }
            else if ( session->_resolveState == ResolveState::Failed )
            {
                failSessionLocked( *session, "could not resolve host" );
                listErase.push_back( sessionID );
            }
            else if ( session->_mapDeadline.empty() )
            {
                listErase.push_back( sessionID ); // 기다리는 요청이 시한으로 모두 끝났다 — 풀린 주소를 쓸 곳이 없다
            }
        }
        for ( const uint64 sessionID : listErase )
        {
            eraseSessionLocked( sessionID );
        }
    }

    void HTTP2Client::resolveSession( uint64& sessionID, bool bStopping )
    {
        if ( bStopping )
            return; // 남은 연결은 shutdown 이 끝낸다
        string     host;
        NetAddress address;
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        sessionIt = _mapSession.find( sessionID );
            if ( sessionIt == _mapSession.end() )
                return; // 그새 정리됐다
            host    = sessionIt->second->_host;
            address = sessionIt->second->_address;
        }
        const bool        bResolved = PlatformSocketUtil::resolveAddress( host, address._port, address );
        ScopedLock<mutex> lock{ _mutex };
        const auto        sessionIt = _mapSession.find( sessionID );
        if ( sessionIt == _mapSession.end() )
            return;
        sessionIt->second->_address      = address;
        sessionIt->second->_resolveState = bResolved ? ResolveState::Resolved : ResolveState::Failed;
    }

    bool HTTP2Client::establishLocked( Session& session )
    {
        if ( session._link.getNegotiatedProtocol() != kAlpnProtocol )
        {
            failSessionLocked( session, "server did not negotiate h2" );
            return false;
        }
        HTTP2ConnectionConfig config;
        config._maxResponseBodyBytes = _settings->_maxResponseBodyBytes;
        session._connection          = make_unique<HTTP2Connection>( config );
        session._connection->start( session._outBytes );
        session._state = SessionState::Ready;
        if ( sendWaitingLocked( session ) == false )
        {
            failSessionLocked( session, "could not send the request" );
            return false;
        }
        return true;
    }

    bool HTTP2Client::sendWaitingLocked( Session& session )
    {
        for ( Pending& pending : session._listWaiting )
        {
            session._connection->submit( pending._requestID, pending._request, pending._url, session._outBytes );
        }
        session._listWaiting.clear();
        return pumpLocked( session );
    }

    bool HTTP2Client::pumpLocked( Session& session )
    {
        bool bWritten = true;
        if ( session._outBytes.empty() == false )
            bWritten = session._link.writePlain( *_transport.get(), session._outBytes.data(), session._outBytes.size() );
        session._outBytes.clear();
        collectLocked( session );
        return bWritten;
    }

    void HTTP2Client::collectLocked( Session& session )
    {
        if ( session._connection == nullptr )
            return;
        vector<HTTPClientResponse> listResponse;
        (void)session._connection->takeResponses( listResponse );
        for ( HTTPClientResponse& response : listResponse )
        {
            session._mapDeadline.erase( response._requestID );
            _listDone.push_back( std::move( response ) );
        }
    }

    void HTTP2Client::failSessionLocked( Session& session, const utf8* pFailure )
    {
        session._bAccepting = SW_FALSE;
        if ( session._connection != nullptr )
            session._connection->failAll( pFailure );
        for ( const Pending& pending : session._listWaiting )
        {
            failImmediately( pending._requestID, pFailure );
        }
        session._listWaiting.clear();
        collectLocked( session );
        session._mapDeadline.clear();
    }

    void HTTP2Client::eraseSessionLocked( uint64 sessionID )
    {
        const auto sessionIt = _mapSession.find( sessionID );
        if ( sessionIt == _mapSession.end() )
            return;
        const Session& session = *sessionIt->second;
        const auto     keyIt   = _mapKeyToSession.find( session._key );
        if ( keyIt != _mapKeyToSession.end() && keyIt->second == sessionID )
            _mapKeyToSession.erase( keyIt );
        if ( session._handle.isValid() )
            _mapHandleToSession.erase( session._handle.packed() );
        _mapSession.erase( sessionIt );
    }

    void HTTP2Client::failImmediately( uint64 requestID, const utf8* pFailure )
    {
        HTTPClientResponse& response = _listDone.emplace_back();
        response._requestID          = requestID;
        response._bTransportFailed   = SW_TRUE;
        response._failureText        = pFailure;
    }

    HTTP2Client::Session* HTTP2Client::findByHandleLocked( StreamConnectionHandle handle )
    {
        const auto handleIt = _mapHandleToSession.find( handle.packed() );
        if ( handleIt == _mapHandleToSession.end() )
            return nullptr;
        const auto sessionIt = _mapSession.find( handleIt->second );
        return sessionIt != _mapSession.end() ? sessionIt->second.get() : nullptr;
    }

    void HTTP2Client::update()
    {
        const MonotonicMilliseconds now = _clock.nowMilliseconds();
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;
        if ( _ioThreadCount == 0 )
            (void)_transport->pollIO( 0 );
        vector<StreamConnectionHandle> listClose;
        {
            ScopedLock<mutex> lock{ _mutex };
            advanceResolvingLocked();
            for ( auto& [sessionID, session] : _mapSession )
            {
                (void)sessionID;
                vector<uint64> listExpired;
                for ( const auto& [requestID, deadline] : session->_mapDeadline )
                {
                    if ( now >= deadline )
                        listExpired.push_back( requestID );
                }
                if ( listExpired.empty() )
                    continue;
                for ( const uint64 requestID : listExpired )
                {
                    bool bWaiting = false;
                    for ( size_t index = 0; index < session->_listWaiting.size(); ++index )
                    {
                        if ( session->_listWaiting[index]._requestID != requestID )
                            continue;
                        session->_listWaiting.erase( session->_listWaiting.begin() + static_cast<ptrdiff_t>( index ) );
                        failImmediately( requestID, "timed out" );
                        session->_mapDeadline.erase( requestID );
                        bWaiting = true;
                        break;
                    }
                    if ( bWaiting == false && session->_connection != nullptr )
                        session->_connection->cancel( requestID, "timed out", session->_outBytes ); // 응답으로 모이고 아래 pump 가 회수한다(시한도 거기서 지운다)
                    else if ( bWaiting == false )
                        session->_mapDeadline.erase( requestID );
                }
                if ( session->_connection != nullptr && pumpLocked( *session ) == false )
                {
                    failSessionLocked( *session, "could not send the request" );
                    listClose.push_back( session->_handle );
                }
            }
        }
        for ( const StreamConnectionHandle handle : listClose )
        {
            _transport->close( handle, StreamCloseMode::Abort );
        }
    }

    int32 HTTP2Client::pollResponses( vector<HTTPClientResponse>& outListResponse )
    {
        ScopedLock<mutex> lock{ _mutex };
        const int32       count = static_cast<int32>( _listDone.size() );
        for ( HTTPClientResponse& response : _listDone )
        {
            outListResponse.push_back( std::move( response ) );
        }
        _listDone.clear();
        return count;
    }

    int32 HTTP2Client::getPendingCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        int32             count = 0;
        for ( const auto& [sessionID, session] : _mapSession )
        {
            (void)sessionID;
            count += static_cast<int32>( session->_mapDeadline.size() );
        }
        return count;
    }

    int32 HTTP2Client::getSessionCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return static_cast<int32>( _mapSession.size() );
    }

    void HTTP2Client::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        (void)bAccepted;
        ScopedLock<mutex> lock{ _mutex };
        Session*          pSession = findByHandleLocked( handle );
        if ( pSession == nullptr )
            return;
        pSession->_state = SessionState::Handshaking;
        if ( pSession->_link.flushHandshake( *_transport.get() ) == false )
        {
            failSessionLocked( *pSession, "could not start the TLS handshake" );
            _transport->close( handle, StreamCloseMode::Abort );
        }
    }

    void HTTP2Client::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        ScopedLock<mutex> lock{ _mutex };
        Session*          pSession = findByHandleLocked( handle );
        if ( pSession == nullptr )
            return;
        Session& session = *pSession;
        session._plainBytes.clear();
        if ( session._link.readReceived( *_transport.get(), pData, size, session._plainBytes ) == false )
        {
            failSessionLocked( session, session._link.getFailureText() );
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        const bool bHandshakeDone = session._state == SessionState::Handshaking && session._link.isEstablished();
        if ( bHandshakeDone && establishLocked( session ) == false )
        {
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        if ( session._state != SessionState::Ready || session._plainBytes.empty() )
            return;
        string     error;
        const bool bFed     = session._connection->feed( session._plainBytes.data(), session._plainBytes.size(), session._outBytes, error );
        const bool bWritten = pumpLocked( session ); // 연결 오류로 끝난 요청의 실패 응답도 여기서 회수한다
        if ( bFed == false || bWritten == false )
        {
            failSessionLocked( session, bFed ? "could not send the request" : error.c_str() );
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        if ( session._connection->hasGoneAway() )
        {
            session._bAccepting = SW_FALSE; // 새 요청은 새 연결로 간다
            if ( session._connection->getActiveCount() == 0 )
                _transport->close( handle, StreamCloseMode::Graceful );
        }
    }

    void HTTP2Client::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        ScopedLock<mutex> lock{ _mutex };
        Session*          pSession = findByHandleLocked( handle );
        if ( pSession == nullptr )
            return;
        failSessionLocked( *pSession, toString( reason ) );
        eraseSessionLocked( pSession->_sessionID );
    }
} // namespace sw
