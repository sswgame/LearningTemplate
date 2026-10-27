#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTPServer.h"

namespace sw
{
    SW_LOG_CALLER( "HTTPServer" );
} // namespace sw

namespace sw
{
    HTTPServer::HTTPServer()
        : _mutex{}
        , _mapPeer{}
        , _listReady{}
        , _transport{}
        , _settings{}
        , _pHandler{}
        , _ioThreadCount{ 0 }
        , _handledCount{ 0 }
        , _bInitialized{ false }
    {
    }

    HTTPServer::~HTTPServer()
    {
        shutdown();
    }

    bool HTTPServer::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportConfig& transportSettings, const HTTPServerConfig& settings,
                                 IHTTPRequestHandler* pHandler )
    {
        const bool bCannotStart = transport == nullptr || pHandler == nullptr || _bInitialized.load( std::memory_order_acquire ) == true;
        if ( bCannotStart )
            return false;
        _transport.set( std::move( transport ) );
        _settings.set( settings );
        _pHandler.set( pHandler );
        _ioThreadCount = transportSettings._ioThreadCount;
        if ( _transport->initialize( this, transportSettings ) == false || _transport->listen( settings._listenAddress ) == false )
        {
            SW_LOG_ERROR( "HTTP server could not listen" );
            _transport->shutdown();
            _transport.reset();
            _settings.reset();
            _pHandler.reset();
            return false;
        }
        _bInitialized.store( true, std::memory_order_release );
        return true;
    }

    void HTTPServer::shutdown()
    {
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;
        _transport->shutdown();
        _transport.reset();
        _settings.reset(); // initialize 가 한 번 정한다 — 다시 시작할 수 있게 비운다
        ScopedLock<mutex> lock{ _mutex };
        _mapPeer.clear();
        _listReady.clear();
        _pHandler.reset();
        _bInitialized.store( false, std::memory_order_release );
    }

    void HTTPServer::update()
    {
        if ( _bInitialized.load( std::memory_order_acquire ) == false )
            return;
        if ( _ioThreadCount == 0 )
            (void)_transport->pollIO( 0 );
        vector<ReadyRequest> listReady;
        {
            ScopedLock<mutex> lock{ _mutex };
            listReady.swap( _listReady );
        }
        for ( ReadyRequest& ready : listReady )
        {
            HTTPServerResponse response;
            if ( ready._bMalformed == SW_TRUE )
                response._statusCode = 400;
            else
                _pHandler->onHTTPRequest( ready._request, response );
            ++_handledCount;
            vector<uint8> bytes;
            HTTPWriteUtil::writeResponse( response, bytes );
            ScopedLock<mutex> lock{ _mutex };
            const auto        peerIt = _mapPeer.find( ready._handle.packed() );
            if ( peerIt == _mapPeer.end() )
                continue; // 그새 끊겼다
            const bool bWritten = peerIt->second->_link.writePlain( *_transport.get(), bytes.data(), bytes.size() );
            _transport->close( ready._handle, bWritten ? StreamCloseMode::Graceful : StreamCloseMode::Abort );
        }
    }

    uint16 HTTPServer::getListenPort() const
    {
        return _transport.get() != nullptr ? _transport->getListenPort() : 0;
    }

    void HTTPServer::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        if ( bAccepted == false )
            return;
        ScopedLock<mutex> lock{ _mutex };
        unique_ptr<Peer>  peer = make_unique<Peer>();
        if ( peer->_link.initialize( _settings->_pTLSContext ) == false )
        {
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        peer->_link.setHandle( handle );
        peer->_parser.reset( HTTPMessageKind::Request, _settings->_maxRequestBodyBytes );
        _mapPeer[handle.packed()] = std::move( peer );
    }

    void HTTPServer::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        peerIt = _mapPeer.find( handle.packed() );
        if ( peerIt == _mapPeer.end() || peerIt->second->_bComplete == SW_TRUE )
            return;
        Peer& peer = *peerIt->second;
        peer._plainBytes.clear();
        if ( peer._link.readReceived( *_transport.get(), pData, size, peer._plainBytes ) == false )
        {
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        const HTTPParseState state = peer._parser.append( peer._plainBytes.data(), peer._plainBytes.size() );
        if ( state == HTTPParseState::NeedMore )
            return;
        peer._bComplete     = SW_TRUE;
        ReadyRequest& ready = _listReady.emplace_back();
        ready._handle       = handle;
        if ( state == HTTPParseState::Failed )
        {
            ready._bMalformed = SW_TRUE;
            return;
        }
        const string& target       = peer._parser.getTarget();
        const size_t  question     = target.find( '?' );
        ready._request._method     = peer._parser.getMethod();
        ready._request._path       = target.substr( 0, question );
        ready._request._listHeader = peer._parser.getHeaders();
        ready._request._bodyBytes  = std::move( peer._parser.getBody() );
        const bool bQueryOk        = question == string::npos || HTTPUtil::decodeForm( string_view( target ).substr( question + 1 ), ready._request._listQuery );
        ready._bMalformed          = bQueryOk ? SW_FALSE : SW_TRUE;
    }

    void HTTPServer::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        (void)reason;
        ScopedLock<mutex> lock{ _mutex };
        _mapPeer.erase( handle.packed() );
    }
} // namespace sw
