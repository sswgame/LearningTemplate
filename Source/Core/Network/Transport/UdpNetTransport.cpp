#include "pch.h"

#include "Core/Network/Transport/UdpNetTransport.h"

#include "Core/Network/Transport/PlatformSocketUtil.h"

namespace sw
{
    UdpNetTransport::UdpNetTransport()
        : _localAddress{}
        , _socketHandle{ PlatformSocketUtil::kInvalidSocket }
    {
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

    bool UdpNetTransport::waitForReceive( float64 timeoutSeconds )
    {
        if ( isOpen() == false )
            return INetTransport::waitForReceive( timeoutSeconds );
        const int32 timeoutMilli = static_cast<int32>( timeoutSeconds * 1000.0 + 0.5 );
        return PlatformSocketUtil::waitReadable( _socketHandle, timeoutMilli > 0 ? timeoutMilli : 0 );
    }

    bool UdpNetTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        if ( isOpen() == false )
            return false;
        // 받는 쪽 버퍼에 바로 받는다(사본 없음). 한도보다 한 바이트 크게 잡아 너무 큰 데이터그램을 알아본다.
        // 오류 · 너무 큰 데이터그램은 건너뛰고 다음 것을 본다.
        outBuffer.resize( static_cast<size_t>( kNetMaxPacketSize + 1 ) );
        for ( int32 attempt = 0; attempt < 64; ++attempt )
        {
            int32                     size   = 0;
            const SocketReceiveResult result = PlatformSocketUtil::receiveFrom( _socketHandle, outFrom, outBuffer.data(), static_cast<int32>( outBuffer.size() ), size );
            if ( result == SocketReceiveResult::WouldBlock )
                break;
            if ( result == SocketReceiveResult::Received && size > 0 && size <= kNetMaxPacketSize )
            {
                outBuffer.resize( static_cast<size_t>( size ) );
                return true;
            }
        }
        outBuffer.clear();
        return false;
    }
} // namespace sw
