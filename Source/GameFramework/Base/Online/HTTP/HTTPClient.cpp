#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

#include "Core/Network/NetTypes.h"

namespace sw
{
    SW_LOG_CALLER( "HTTPClient" );
} // namespace sw

namespace sw
{
    HTTPClient::HTTPClient()
        : _mutex{}
        , _mapCall{}
        , _mapHostToTLSContext{}
        , _listDone{}
        , _transport{}
        , _settings{}
        , _nextRequestID{ 1 }
        , _ioThreadCount{ 0 }
        , _bInitialized{ SW_FALSE }
    {
    }

    HTTPClient::~HTTPClient() { shutdown(); }

    bool HTTPClient::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const HTTPClientSettings& settings )
    {
        if ( transport == nullptr || _bInitialized == SW_TRUE )
            return false;
        _transport     = std::move( transport );
        _settings      = settings;
        _ioThreadCount = transportSettings._ioThreadCount;
        if ( _transport->initialize( this, transportSettings ) == false )
        {
            SW_LOG_ERROR( "HTTP client transport did not start" );
            _transport.reset();
            return false;
        }
        _bInitialized = SW_TRUE;
        return true;
    }

    void HTTPClient::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _transport->shutdown(); // 열린 연결은 Shutdown 으로 닫히며 onStreamClosed 가 요청을 끝낸다
        _transport.reset();
        std::scoped_lock<mutex> lock{ _mutex };
        for ( auto& [packed, call] : _mapCall )
        {
            (void)packed;
            finishLocked( *call, "client shut down" );
        }
        _mapCall.clear();
        _bInitialized = SW_FALSE;
    }

    void HTTPClient::registerTLSContext( string_view host, ITLSContext* pTLSContext )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapHostToTLSContext[string( host )] = pTLSContext;
    }

    uint64 HTTPClient::submitRequest( const HTTPClientRequest& request, int64 nowMs )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const uint64            requestID = _nextRequestID++;
        HTTPAddress             url;
        if ( _bInitialized == SW_FALSE )
        {
            failImmediately( requestID, "client is not running" );
            return requestID;
        }
        if ( HTTPAddress::parse( request._url, url ) == false )
        {
            failImmediately( requestID, "malformed URL" );
            return requestID;
        }
        if ( static_cast<int32>( _mapCall.size() ) >= _settings._maxConcurrentRequests )
        {
            failImmediately( requestID, "too many requests in flight" );
            return requestID;
        }
        NetAddress address;
        if ( NetAddress::parse( url._host == "localhost" ? string_view( "127.0.0.1" ) : string_view( url._host ), url._port, address ) == false )
        {
            failImmediately( requestID, "host name resolution is not supported (IPv4 or localhost only)" );
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
        call->_deadlineMs     = nowMs + ( request._timeoutMs > 0 ? request._timeoutMs : HTTPConstant::kDefaultTimeoutMs );
        call->_parser.reset( HTTPMessageKind::Response, _settings._maxResponseBodyBytes );
        HTTPWriteUtil::writeRequest( request, url, call->_requestBytes );
        if ( call->_link.initialize( pTLSContext ) == false )
        {
            failImmediately( requestID, "could not create a TLS session" );
            return requestID;
        }
        const StreamConnectionHandle handle = _transport->connect( address );
        if ( handle.isValid() == false )
        {
            failImmediately( requestID, "could not connect" );
            return requestID;
        }
        call->_link.setHandle( handle );
        _mapCall[handle.packed()] = std::move( call );
        return requestID;
    }

    void HTTPClient::tick( int64 nowMs )
    {
        if ( _bInitialized == SW_FALSE )
            return;
        if ( _ioThreadCount == 0 )
            (void)_transport->pollIO( 0 );
        vector<StreamConnectionHandle> listExpired;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto& [packed, call] : _mapCall )
            {
                if ( nowMs >= call->_deadlineMs && call->_requestID != 0 )
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
        std::scoped_lock<mutex> lock{ _mutex };
        const int32             count = static_cast<int32>( _listDone.size() );
        for ( HTTPClientResponse& response : _listDone )
        {
            outListResponse.push_back( std::move( response ) );
        }
        _listDone.clear();
        return count;
    }

    int32 HTTPClient::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        int32                   count = 0;
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
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              callIt = _mapCall.find( handle.packed() );
        if ( callIt == _mapCall.end() || callIt->second->_requestID == 0 )
            return;
        Call& call    = *callIt->second;
        call._bOpened = SW_TRUE;
        if ( call._link.writePlain( *_transport, call._requestBytes.data(), call._requestBytes.size() ) == false )
        {
            finishLocked( call, "could not send the request" );
            _transport->close( handle, StreamCloseMode::Abort );
        }
        call._requestBytes.clear();
    }

    void HTTPClient::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              callIt = _mapCall.find( handle.packed() );
        if ( callIt == _mapCall.end() || callIt->second->_requestID == 0 )
            return;
        Call& call = *callIt->second;
        call._plainBytes.clear();
        if ( call._link.readReceived( *_transport, pData, size, call._plainBytes ) == false )
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
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              callIt = _mapCall.find( handle.packed() );
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
