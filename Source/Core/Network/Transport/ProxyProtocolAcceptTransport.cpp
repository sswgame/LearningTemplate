#include "pch.h"

#include "Core/Network/Transport/ProxyProtocolAcceptTransport.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Network/Transport/ProxyProtocol.h"

namespace sw
{
    ProxyProtocolAcceptTransport::ProxyProtocolAcceptTransport( unique_ptr<IStreamTransport> pInner, vector<uint32> listTrustedProxy )
        : StreamTransportDecorator{ std::move( pInner ) }
        , _innerHandler{ *this }
        , _pHandler{ nullptr }
        , _listTrustedProxy{ std::move( listTrustedProxy ) }
        , _mutex{}
        , _mapConnection{}
    {
    }

    ProxyProtocolAcceptTransport::~ProxyProtocolAcceptTransport()
    {
        if ( _pInner != nullptr )
            _pInner->shutdown(); // 안쪽이 닫힘 콜백을 이 객체로 부르므로 멤버가 사라지기 전에 내린다
    }

    bool ProxyProtocolAcceptTransport::initialize( IStreamHandler* pHandler, const StreamTransportConfig& settings )
    {
        if ( _pInner == nullptr || pHandler == nullptr )
            return false;
        _pHandler = pHandler;
        return _pInner->initialize( &_innerHandler, settings );
    }

    expected<vector<uint32>> ProxyProtocolAcceptTransport::parseTrustedProxies( const vector<string>& listText )
    {
        vector<uint32> listTrustedProxy;
        for ( const string& text : listText )
        {
            NetAddress proxy;
            if ( NetAddress::parse( text, 0, proxy ) == false || proxy.isIpv6() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the trusted proxy list has an entry that is not an IPv4 address", text } );
            listTrustedProxy.push_back( proxy.getIpv4() );
        }
        return listTrustedProxy;
    }

    bool ProxyProtocolAcceptTransport::isTrustedProxy( const NetAddress& address ) const
    {
        for ( const uint32 trusted : _listTrustedProxy )
        {
            if ( address.isIpv6() == false && trusted == address.getIpv4() ) // 신뢰 목록은 IPv4 다(PROXY v2 해석기가 TCP over IPv4 만 받는다)
                return true;
        }
        return false;
    }

    shared_ptr<ProxyProtocolAcceptTransport::Connection> ProxyProtocolAcceptTransport::findConnection( StreamConnectionHandle handle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        found = _mapConnection.find( handle.packed() );
        return found != _mapConnection.end() ? found->second : nullptr;
    }

    NetAddress ProxyProtocolAcceptTransport::getRemoteAddress( StreamConnectionHandle handle ) const
    {
        const shared_ptr<Connection> connection = findConnection( handle );
        if ( connection == nullptr )
            return _pInner->getRemoteAddress( handle ); // 내가 건 연결
        return connection->_bOpened == SW_TRUE ? connection->_client : NetAddress{};
    }

    void ProxyProtocolAcceptTransport::handleOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        if ( bAccepted == false )
        {
            _pHandler->onStreamOpened( handle, remote, bAccepted ); // 내가 건 연결은 머리가 없다
            return;
        }
        shared_ptr<Connection> connection = make_shared<Connection>();
        connection->_proxy                = remote;
        if ( isTrustedProxy( remote ) == false )
        {
            connection->_bRejected = SW_TRUE; // 프록시를 건너뛴 연결이다 — 위로 알리지 않고 닫는다
            SW_LOG_WARNING_ID( "ProxyProtocolAcceptTransport.UntrustedSource", "Stream from %# is not a trusted proxy - closed", remote.toMaskedString() );
        }
        {
            ScopedLock<mutex> lock{ _mutex };
            _mapConnection[handle.packed()] = connection;
        }
        if ( connection->_bRejected == SW_TRUE )
            _pInner->close( handle, StreamCloseMode::Abort );
    }

    void ProxyProtocolAcceptTransport::handleReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        const shared_ptr<Connection> connection = findConnection( handle );
        if ( connection == nullptr )
        {
            _pHandler->onStreamReceived( handle, pData, size );
            return;
        }
        if ( connection->_bRejected == SW_TRUE )
            return;
        if ( connection->_bOpened == SW_TRUE )
        {
            _pHandler->onStreamReceived( handle, pData, size );
            return;
        }
        // 머리를 모은다 — 상한을 넘게 모으지 않는다(해석기가 거짓 길이를 거절한다)
        connection->_header.insert( connection->_header.end(), pData, pData + size );
        ProxyHeader             header;
        const ProxyHeaderResult parsed = ProxyHeader::parse( connection->_header.data(), static_cast<int32>( connection->_header.size() ), header );
        if ( parsed == ProxyHeaderResult::NeedMore )
            return;
        if ( parsed == ProxyHeaderResult::Invalid )
        {
            connection->_bRejected = SW_TRUE;
            SW_LOG_WARNING_ID( "ProxyProtocolAcceptTransport.InvalidHeader", "Trusted proxy %# sent an invalid PROXY v2 header - closed", connection->_proxy.toMaskedString() );
            _pInner->close( handle, StreamCloseMode::Abort );
            return;
        }
        connection->_client  = header._bLocal == SW_TRUE ? connection->_proxy : header._source;
        connection->_bOpened = SW_TRUE;
        _pHandler->onStreamOpened( handle, connection->_client, true );
        const int32 restSize = static_cast<int32>( connection->_header.size() ) - header._headerSize;
        if ( restSize > 0 )
            _pHandler->onStreamReceived( handle, connection->_header.data() + header._headerSize, restSize );
        connection->_header.clear();
        connection->_header.shrink_to_fit();
    }

    void ProxyProtocolAcceptTransport::handleClosed( StreamConnectionHandle handle, StreamCloseReason reason )
    {
        shared_ptr<Connection> connection;
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        found = _mapConnection.find( handle.packed() );
            if ( found != _mapConnection.end() )
            {
                connection = found->second;
                _mapConnection.erase( found );
            }
        }
        if ( connection != nullptr && connection->_bOpened == SW_FALSE )
            return; // 위로 열지 않은 연결의 닫힘은 알리지 않는다
        _pHandler->onStreamClosed( handle, reason );
    }
} // namespace sw
