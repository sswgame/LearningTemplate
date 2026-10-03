#include "pch.h"

#include "Core/Network/UdpNetTransport.h"

#include "Core/Network/PlatformSocketUtil.h"

namespace sw
{
    UdpNetTransport::UdpNetTransport()
        : _buffer{}
        , _localAddress{}
        , _socketHandle{ PlatformSocketUtil::kInvalidSocket }
    {
        _buffer.resize( static_cast<size_t>( kNetMaxPacketSize * 2 ) );
    }

    UdpNetTransport::~UdpNetTransport() { close(); }

    bool UdpNetTransport::open( uint16 port )
    {
        close();
        _socketHandle = PlatformSocketUtil::openUdpSocket( port );
        if ( _socketHandle == PlatformSocketUtil::kInvalidSocket )
            return false;
        _localAddress = NetAddress::makeLoopback( PlatformSocketUtil::getBoundPort( _socketHandle ) );
        return true;
    }

    void UdpNetTransport::close()
    {
        PlatformSocketUtil::closeSocket( _socketHandle );
        _socketHandle = PlatformSocketUtil::kInvalidSocket;
    }

    bool UdpNetTransport::isOpen() const { return _socketHandle != PlatformSocketUtil::kInvalidSocket; }

    bool UdpNetTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        if ( isOpen() == false || size <= 0 || size > kNetMaxPacketSize )
            return false;
        return PlatformSocketUtil::sendTo( _socketHandle, to, pData, size );
    }

    bool UdpNetTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        if ( isOpen() == false )
            return false;
        // 오류 · 너무 큰 데이터그램은 건너뛰고 다음 것을 본다.
        for ( int32 attempt = 0; attempt < 64; ++attempt )
        {
            int32                     size   = 0;
            const SocketReceiveResult result = PlatformSocketUtil::receiveFrom( _socketHandle, outFrom, _buffer.data(), static_cast<int32>( _buffer.size() ), size );
            if ( result == SocketReceiveResult::WouldBlock )
                return false;
            if ( result == SocketReceiveResult::Received && size > 0 && size <= kNetMaxPacketSize )
            {
                outBuffer.assign( _buffer.begin(), _buffer.begin() + size );
                return true;
            }
        }
        return false;
    }
} // namespace sw
