#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

#include "Core/Network/Transport/PlatformSocketUtil.h"

namespace sw
{
    SW_LOG_CALLER( "HTTPClient" );
} // namespace sw

namespace sw
{
    HTTPClient::HTTPClient( const IClock& clock )
        : _clock{ clock }
        , _mutex{}
        , _mapCall{}
        , _mapHostToTLSContext{}
        , _listResolvingCall{}
        , _listDone{}
        , _transport{}
        , _settings{}
        , _resolver{}
        , _requestIDAllocator{}
        , _ioThreadCount{ 0 }
        , _bInitialized{ false }
    {
    }

    HTTPClient::~HTTPClient()
    {
        shutdown();
    }

    bool HTTPClient::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportConfig& transportSettings, const HTTPClientConfig& settings )
    {
        if ( transport == nullptr || _bInitialized.load( std::memory_order_acquire ) == true )
            return false;
        _transport.set( std::move( transport ) );
        _settings.set( settings );
        _ioThreadCount = transportSettings._ioThreadCount;
        if ( _transport->initialize( this, transportSettings ) == false )
        {
            SW_LOG_ERROR( "HTTP client transport did not start" );
            _transport.reset();
            _settings.reset();
            return false;
        }
        _bInitialized.store( true, std::memory_order_release );
        return true;
    }

    void HTTPClient::shutdown()
    {
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;
        _resolver.stop();       // 풀던 이름 하나는 해석기 시한까지 기다린다. 남은 이름은 풀지 않고 아래에서 끝낸다
        _transport->shutdown(); // 열린 연결은 Shutdown 으로 닫히며 onStreamClosed 가 요청을 끝낸다
        _transport.reset();
        _settings.reset(); // initialize 가 한 번 정한다 — 다시 시작할 수 있게 비운다
        ScopedLock<mutex> lock{ _mutex };
        for ( auto& [packed, call] : _mapCall )
        {
            (void)packed;
            finishLocked( *call, "client shut down" );
        }
        for ( unique_ptr<Call>& call : _listResolvingCall )
        {
            finishLocked( *call, "client shut down" );
        }
        _mapCall.clear();
        _listResolvingCall.clear();
        _bInitialized.store( false, std::memory_order_release );
    }

    void HTTPClient::registerTLSContext( string_view host, ITLSContext* pTLSContext )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapHostToTLSContext[string( host )] = pTLSContext;
    }

    uint64 HTTPClient::submitRequest( const HTTPClientRequest& request )
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
        if ( static_cast<int32>( _mapCall.size() ) >= _settings->_maxConcurrentRequests )
        {
            failImmediately( requestID, "too many requests in flight" );
            return requestID;
        }
        ITLSContext* pTLSContext = nullptr;
        if ( url._bSecure == SW_TRUE )
        {
            const auto contextIt = _mapHostToTLSContext.find( url._host );
            if ( contextIt == _mapHostToTLSContext.end() || contextIt->second == nullptr )
            {
                failImmediately( requestID, "no TLS context for this host" );
                return requestID;
            }
            pTLSContext = contextIt->second;
        }
        unique_ptr<Call> call = make_unique<Call>();
        call->_requestID      = requestID;
        call->_deadline       = _clock.nowMilliseconds() + ( request._timeoutMs > 0 ? request._timeoutMs : HTTPConstant::kDefaultTimeoutMs );
        call->_parser.reset( HTTPMessageKind::Response, _settings->_maxResponseBodyBytes );
        HTTPWriteUtil::writeRequest( request, url, call->_requestBytes );
        if ( call->_link.initialize( pTLSContext ) == false )
        {
            failImmediately( requestID, "could not create a TLS session" );
            return requestID;
        }
        if ( NetAddress::parse( url._host, url._port, call->_address ) )
        {
            connectLocked( std::move( call ) );
            return requestID;
        }
        // 이름이다 — 해석 스레드에 맡기고 `update` 가 연결을 건다. 여기서 풀면 DNS 왕복만큼 주인 스레드와 I/O 콜백이 잠금에 막힌다.
        call->_resolveHost  = url._host;
        call->_address      = NetAddress::makeAnyInterface( url._port );
        call->_resolveState = ResolveState::Pending;
        _listResolvingCall.push_back( std::move( call ) );
        if ( _resolver.isRunning() == false )
            _resolver.start( "HTTPResolve", DedicatedWorker<uint64>::ProcessDelegate::create<&HTTPClient::resolveRequest>( this ) );
        uint64 resolveJob = requestID;
        (void)_resolver.submit( std::move( resolveJob ) ); // 바로 위에서 띄웠으니 받는다 — 못 받아도 요청은 Pending 으로 남아 시한이 끝낸다
        return requestID;
    }

    void HTTPClient::connectLocked( unique_ptr<Call> call )
    {
        const StreamConnectionHandle handle = _transport->connect( call->_address );
        if ( handle.isValid() == false )
        {
            finishLocked( *call, "could not connect" );
            return;
        }
        call->_link.setHandle( handle );
        _mapCall[handle.packed()] = std::move( call );
    }

    void HTTPClient::advanceResolvingLocked( MonotonicMilliseconds now )
    {
        for ( size_t index = 0; index < _listResolvingCall.size(); )
        {
            unique_ptr<Call>& call     = _listResolvingCall[index];
            const bool        bExpired = now >= call->_deadline;
            if ( call->_resolveState == ResolveState::Pending && bExpired == false )
            {
                ++index;
                continue;
            }
            unique_ptr<Call> finished = std::move( call );
            _listResolvingCall.erase( _listResolvingCall.begin() + static_cast<ptrdiff_t>( index ) );
            if ( finished->_resolveState == ResolveState::Resolved && bExpired == false )
                connectLocked( std::move( finished ) );
            else
                finishLocked( *finished, bExpired ? "timed out" : "could not resolve host" );
        }
    }

    void HTTPClient::resolveRequest( uint64& requestID, bool bStopping )
    {
        if ( bStopping )
            return; // 남은 요청은 shutdown 이 끝낸다
        string     host;
        NetAddress address;
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( const unique_ptr<Call>& call : _listResolvingCall )
            {
                if ( call->_requestID == requestID )
                {
                    host    = call->_resolveHost;
                    address = call->_address;
                }
            }
        }
        if ( host.empty() )
            return; // 그새 시한으로 끝났다
        const bool        bResolved = PlatformSocketUtil::resolveAddress( host, address._port, address );
        ScopedLock<mutex> lock{ _mutex };
        for ( unique_ptr<Call>& call : _listResolvingCall )
        {
            if ( call->_requestID != requestID )
                continue;
            call->_address      = address;
            call->_resolveState = bResolved ? ResolveState::Resolved : ResolveState::Failed;
        }
    }

    void HTTPClient::update()
    {
        const MonotonicMilliseconds now = _clock.nowMilliseconds();
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;
        if ( _ioThreadCount == 0 )
            (void)_transport->pollIO( 0 );
        vector<StreamConnectionHandle> listExpired;
        {
            ScopedLock<mutex> lock{ _mutex };
            advanceResolvingLocked( now );
            for ( auto& [packed, call] : _mapCall )
            {
                if ( now >= call->_deadline && call->_requestID != 0 )
                {
                    finishLocked( *call, "timed out" );
                    listExpired.push_back( StreamConnectionHandle::fromPacked( packed ) );
                }
            }
        }
        for ( const StreamConnectionHandle handle : listExpired )
        {
            _transport->close( handle, StreamCloseMode::Abort );
        }
    }

    int32 HTTPClient::pollResponses( vector<HTTPClientResponse>& outListResponse )
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

    int32 HTTPClient::getPendingCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        int32             count = static_cast<int32>( _listResolvingCall.size() );
        for ( const auto& [packed, call] : _mapCall )
        {
            (void)packed;
            count += call->_requestID != 0 ? 1 : 0;
        }
        return count;
    }

    void HTTPClient::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        (void)bAccepted;
        ScopedLock<mutex> lock{ _mutex };
        const auto        callIt = _mapCall.find( handle.packed() );
        if ( callIt == _mapCall.end() || callIt->second->_requestID == 0 )
            return;
        Call& call    = *callIt->second;
        call._bOpened = SW_TRUE;
        if ( call._link.writePlain( *_transport.get(), call._requestBytes.data(), call._requestBytes.size() ) == false )
        {
            finishLocked( call, "could not send the request" );
            _transport->close( handle, StreamCloseMode::Abort );
        }
        call._requestBytes.clear();
    }

    void HTTPClient::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        callIt = _mapCall.find( handle.packed() );
        if ( callIt == _mapCall.end() || callIt->second->_requestID == 0 )
            return;
        Call& call = *callIt->second;
        call._plainBytes.clear();
        if ( call._link.readReceived( *_transport.get(), pData, size, call._plainBytes ) == false )
        {
            finishLocked( call, call._link.getFailureText() );
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        const HTTPParseState state = call._parser.append( call._plainBytes.data(), call._plainBytes.size() );
        if ( state == HTTPParseState::NeedMore )
            return;
        finishLocked( call, state == HTTPParseState::Failed ? call._parser.getFailureText().c_str() : nullptr );
        _transport->close( handle, StreamCloseMode::Graceful );
    }

    void HTTPClient::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        callIt = _mapCall.find( handle.packed() );
        if ( callIt == _mapCall.end() )
            return;
        Call& call = *callIt->second;
        if ( call._requestID != 0 )
        {
            const HTTPParseState state = call._parser.finishOnClose();
            if ( state == HTTPParseState::Complete )
                finishLocked( call, nullptr );
            else
                finishLocked( call, call._bOpened == SW_TRUE ? toString( reason ) : "could not connect" );
        }
        _mapCall.erase( callIt );
    }

    void HTTPClient::finishLocked( Call& call, const utf8* pFailure )
    {
        if ( call._requestID == 0 )
            return;
        HTTPClientResponse& response = _listDone.emplace_back();
        response._requestID          = call._requestID;
        call._requestID              = 0; // 한 번만 — 닫힘 콜백이 다시 끝내지 않게
        if ( pFailure != nullptr )
        {
            response._bTransportFailed = SW_TRUE;
            response._failureText      = pFailure;
            return;
        }
        response._statusCode = call._parser.getStatusCode();
        response._listHeader = call._parser.getHeaders();
        response._bodyBytes  = std::move( call._parser.getBody() );
    }

    void HTTPClient::failImmediately( uint64 requestID, const utf8* pFailure )
    {
        HTTPClientResponse& response = _listDone.emplace_back();
        response._requestID          = requestID;
        response._bTransportFailed   = SW_TRUE;
        response._failureText        = pFailure;
    }
} // namespace sw
