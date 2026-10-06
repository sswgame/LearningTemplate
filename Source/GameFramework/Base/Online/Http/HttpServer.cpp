#include "pch.h"

#include "GameFramework/Base/Online/Http/HttpServer.h"

namespace sw
{
    SW_LOG_CALLER( "HttpServer" );
} // namespace sw

namespace sw
{
    HttpServer::HttpServer()
        : _mutex{}
        , _mapPeer{}
        , _listReady{}
        , _transport{}
        , _settings{}
        , _pHandler{ nullptr }
        , _ioThreadCount{ 0 }
        , _handledCount{ 0 }
        , _bInitialized{ SW_FALSE }
    {
    }

    HttpServer::~HttpServer() { shutdown(); }

    bool HttpServer::initialize( unique_ptr<IStreamTransport> transport, const StreamTransportSettings& transportSettings, const HttpServerSettings& settings,
                                 IHttpRequestHandler* pHandler )
    {
        if ( transport == nullptr || pHandler == nullptr || _bInitialized == SW_TRUE )
            return false;
        _transport     = std::move( transport );
        _settings      = settings;
        _pHandler      = pHandler;
        _ioThreadCount = transportSettings._ioThreadCount;
        if ( _transport->initialize( this, transportSettings ) == false || _transport->listen( settings._listenAddress ) == false )
        {
            SW_LOG_ERROR( "HTTP server could not listen" );
            _transport->shutdown();
            _transport.reset();
            return false;
        }
        _bInitialized = SW_TRUE;
        return true;
    }

    void HttpServer::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        _transport->shutdown();
        _transport.reset();
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPeer.clear();
        _listReady.clear();
        _pHandler     = nullptr;
        _bInitialized = SW_FALSE;
    }

    void HttpServer::tick()
    {
        if ( _bInitialized == SW_FALSE )
            return;
        if ( _ioThreadCount == 0 )
            (void)_transport->pollIo( 0 );
        vector<ReadyRequest> listReady;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            listReady.swap( _listReady );
        }
        for ( ReadyRequest& ready : listReady )
        {
            HttpServerResponse response;
            if ( ready._bMalformed == SW_TRUE )
                response._statusCode = 400;
            else
                _pHandler->onHttpRequest( ready._request, response );
            ++_handledCount;
            vector<uint8> bytes;
            HttpWriteUtil::writeResponse( response, bytes );
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              peerIt = _mapPeer.find( ready._handle.packed() );
            if ( peerIt == _mapPeer.end() )
                continue; // 그새 끊겼다
            const bool bWritten = peerIt->second->_link.writePlain( *_transport, bytes.data(), bytes.size() );
            _transport->close( ready._handle, bWritten ? StreamCloseMode::Graceful : StreamCloseMode::Abort );
        }
    }

    uint16 HttpServer::getListenPort() const { return _transport != nullptr ? _transport->getListenPort() : 0; }

    void HttpServer::onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        (void)remote;
        if ( bAccepted == false )
            return;
        std::scoped_lock<mutex> lock{ _mutex };
        unique_ptr<Peer>        peer = make_unique<Peer>();
        if ( peer->_link.initialize( _settings._pTlsContext ) == false )
        {
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        peer->_link.setHandle( handle );
        peer->_parser.reset( HttpMessageKind::Request, _settings._maxRequestBodyBytes );
        _mapPeer[handle.packed()] = std::move( peer );
    }

    void HttpServer::onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const auto              peerIt = _mapPeer.find( handle.packed() );
        if ( peerIt == _mapPeer.end() || peerIt->second->_bComplete == SW_TRUE )
            return;
        Peer& peer = *peerIt->second;
        peer._plainBytes.clear();
        if ( peer._link.readReceived( *_transport, pData, size, peer._plainBytes ) == false )
        {
            _transport->close( handle, StreamCloseMode::Abort );
            return;
        }
        const HttpParseState state = peer._parser.append( peer._plainBytes.data(), peer._plainBytes.size() );
        if ( state == HttpParseState::NeedMore )
            return;
        peer._bComplete     = SW_TRUE;
        ReadyRequest& ready = _listReady.emplace_back();
        ready._handle       = handle;
        if ( state == HttpParseState::Failed )
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
        const bool bQueryOk        = question == string::npos || HttpUtil::decodeForm( string_view( target ).substr( question + 1 ), ready._request._listQuery );
        ready._bMalformed          = bQueryOk ? SW_FALSE : SW_TRUE;
    }

    void HttpServer::onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        (void)reason;
        std::scoped_lock<mutex> lock{ _mutex };
        _mapPeer.erase( handle.packed() );
    }
} // namespace sw
