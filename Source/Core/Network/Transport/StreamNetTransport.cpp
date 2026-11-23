#include "pch.h"

#include "Core/Network/Transport/StreamNetTransport.h"

#include "Core/Concurrency/ScopedLock.h"

#include <chrono>

namespace sw
{
    namespace
    {
        struct StreamNetTransportInternal
        {
            /** @brief 편지함이 드는 데이터그램 상한입니다 — 넘으면 버린다(UDP 소켓의 받는 버퍼가 차면 커널이 버리는 것과 같다). */
            static constexpr size_t kMaxInboxPackets = 4096;

            /** @brief 호스트마다 세는 키입니다(포트를 뺀 주소). */
            static NetAddress toHostKey( const NetAddress& address )
            {
                NetAddress host = address;
                host._port      = 0;
                return host;
            }
        };
    } // namespace

    StreamNetTransport::StreamNetTransport( unique_ptr<IStreamTransport> pStream )
        : _handler{ *this }
        , _mutex{}
        , _receivedCondition{}
        , _mapHandleByAddress{}
        , _mapPeer{}
        , _mapCountByHost{}
        , _listInbox{}
        , _localAddress{}
        , _serverAddress{}
        , _maxConnectionsPerRemote{ 0 }
        , _bOpen{ SW_FALSE }
        , _bConnecting{ SW_FALSE }
        , _bWakeRequested{ SW_FALSE }
        , _pStream{ std::move( pStream ) }
    {
    }

    StreamNetTransport::~StreamNetTransport()
    {
        close();
    }

    bool StreamNetTransport::listen( const NetAddress& bindAddress, const StreamTransportConfig& settings, int32 maxConnectionsPerRemote )
    {
        close();
        if ( _pStream == nullptr || maxConnectionsPerRemote < 1 )
            return false;
        _maxConnectionsPerRemote = maxConnectionsPerRemote;
        _serverAddress           = NetAddress{};
        _bOpen                   = SW_TRUE; // 콜백이 오기 전이다 — 아직 이 스레드만 만진다
        if ( _pStream->initialize( &_handler, settings ) == false || _pStream->listen( bindAddress ) == false )
        {
            close();
            return false;
        }
        _localAddress       = bindAddress;
        _localAddress._port = _pStream->getListenPort();
        return true;
    }

    bool StreamNetTransport::connect( const NetAddress& server, const StreamTransportConfig& settings )
    {
        close();
        if ( _pStream == nullptr || server.isValid() == false )
            return false;
        _serverAddress = server;
        _bOpen         = SW_TRUE; // 콜백이 오기 전이다 — 아직 이 스레드만 만진다
        if ( _pStream->initialize( &_handler, settings ) == false )
        {
            close();
            return false;
        }
        startConnect();
        return true;
    }

    void StreamNetTransport::close()
    {
        bool bWasOpen = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            bWasOpen = _bOpen == SW_TRUE;
            _bOpen   = SW_FALSE; // 늦게 온 `send` 가 닫는 동안 다시 걸지 않는다
        }
        if ( bWasOpen )
            _pStream->shutdown(); // 돌아오면 콜백은 더 오지 않는다
        ScopedLock<mutex> lock{ _mutex };
        _bConnecting  = SW_FALSE;
        _localAddress = NetAddress{};
        _mapHandleByAddress.clear();
        _mapPeer.clear();
        _mapCountByHost.clear();
        _listInbox.clear();
    }

    int32 StreamNetTransport::pollIO( int32 timeoutMilli )
    {
        return _pStream != nullptr ? _pStream->pollIO( timeoutMilli ) : 0;
    }

    bool StreamNetTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        const bool bCannotSendPacket = size <= 0 || size > kNetMaxPacketSize || pData == nullptr;
        if ( bCannotSendPacket )
            return false;
        StreamConnectionHandle handle;
        bool                   bReconnect = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _bOpen == SW_FALSE )
                return false;
            const auto found = _mapHandleByAddress.find( to );
            if ( found != _mapHandleByAddress.end() )
                handle = found->second;
            else
                bReconnect = _serverAddress.isValid() && to == _serverAddress;
        }
        if ( handle.isValid() == false )
        {
            if ( bReconnect )
                startConnect(); // 이미 거는 중이면 아무 일도 하지 않는다
            return false;
        }
        vector<uint8> frame;
        if ( StreamFrameEncoder::appendFrame( frame, StreamFrameKind::Message, 0, pData, size, kNetMaxPacketSize ) == false )
            return false;
        const StreamSendResult result = _pStream->send( handle, frame.data(), static_cast<int32>( frame.size() ) );
        return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
    }

    bool StreamNetTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _listInbox.empty() )
            return false;
        outFrom   = _listInbox.front()._from;
        outBuffer = std::move( _listInbox.front()._buffer );
        _listInbox.pop_front();
        return true;
    }

    bool StreamNetTransport::waitForReceive( float64 timeoutSeconds )
    {
        UniqueLock<mutex> lock{ _mutex };
        if ( timeoutSeconds > 0.0 )
        {
            _receivedCondition.wait_for( lock, std::chrono::duration<float64>( timeoutSeconds ), [this]
            { return _listInbox.empty() == false || _bWakeRequested == SW_TRUE; } );
        }
        _bWakeRequested = SW_FALSE;
        return _listInbox.empty() == false;
    }

    void StreamNetTransport::wake()
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _bWakeRequested = SW_TRUE;
        }
        _receivedCondition.notify_all();
    }

    void StreamNetTransport::startConnect()
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _bOpen == SW_FALSE || _bConnecting == SW_TRUE )
                return;
            _bConnecting = SW_TRUE; // 부르기 전에 켠다 — 열림 · 닫힘이 다른 스레드에서 먼저 올 수 있다
        }
        if ( _pStream->connect( _serverAddress ).isValid() )
            return; // 결과는 onStreamOpened · onStreamClosed 다
        ScopedLock<mutex> lock{ _mutex };
        _bConnecting = SW_FALSE;
    }

    void StreamNetTransport::handleOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted )
    {
        bool bReject = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            const NetAddress& address = bAccepted ? remote : _serverAddress;
            if ( bAccepted )
            {
                int32& hostCount = _mapCountByHost[StreamNetTransportInternal::toHostKey( address )];
                bReject          = hostCount >= _maxConnectionsPerRemote;
                if ( bReject == false )
                    ++hostCount;
            }
            if ( bReject == false )
            {
                shared_ptr<Peer> peer        = make_shared<Peer>();
                peer->_address               = address;
                _mapPeer[handle.packed()]    = std::move( peer );
                _mapHandleByAddress[address] = handle;
            }
        }
        if ( bReject )
            _pStream->close( handle, StreamCloseMode::Abort );
    }

    void StreamNetTransport::handleReceived( StreamConnectionHandle handle, const uint8* pData, int32 size )
    {
        shared_ptr<Peer> peer;
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        found = _mapPeer.find( handle.packed() );
            if ( found != _mapPeer.end() )
                peer = found->second;
        }
        if ( peer == nullptr || peer->_bRejected == SW_TRUE )
            return;
        peer->_decoder.append( pData, size );
        bool bReceived = false;
        while ( true )
        {
            StreamFrameView               frame;
            const StreamFrameDecodeResult result = peer->_decoder.next( frame );
            if ( result == StreamFrameDecodeResult::NeedMore )
                break;
            const bool bDatagram = result == StreamFrameDecodeResult::Frame && frame._kind == StreamFrameKind::Message && frame._flags == 0 && frame._bodySize > 0;
            if ( bDatagram == false )
            {
                peer->_bRejected = SW_TRUE;
                _pStream->close( handle, StreamCloseMode::Abort ); // 이 길의 상대가 아니다(깨진 길이 · 다른 프레임)
                break;
            }
            NetReceivedDatagram datagram;
            datagram._buffer.assign( frame._pBody, frame._pBody + frame._bodySize );
            datagram._from = peer->_address;
            ScopedLock<mutex> lock{ _mutex };
            if ( _listInbox.size() < StreamNetTransportInternal::kMaxInboxPackets )
                _listInbox.push_back( std::move( datagram ) );
            bReceived = true;
        }
        if ( bReceived )
            _receivedCondition.notify_all();
    }

    void StreamNetTransport::handleClosed( StreamConnectionHandle handle )
    {
        ScopedLock<mutex> lock{ _mutex };
        _bConnecting     = SW_FALSE; // 클라이언트의 연결은 하나다 — 다음 `send` 가 다시 건다
        const auto found = _mapPeer.find( handle.packed() );
        if ( found == _mapPeer.end() )
            return; // 열리지 못했거나 열자마자 거절했다
        const NetAddress address      = found->second->_address;
        const auto       foundAddress = _mapHandleByAddress.find( address );
        if ( foundAddress != _mapHandleByAddress.end() && foundAddress->second == handle )
            _mapHandleByAddress.erase( foundAddress ); // 같은 주소로 다시 열린 연결은 남긴다
        const auto foundHost = _mapCountByHost.find( StreamNetTransportInternal::toHostKey( address ) );
        if ( foundHost != _mapCountByHost.end() && --foundHost->second == 0 )
            _mapCountByHost.erase( foundHost );
        _mapPeer.erase( found );
    }
} // namespace sw
