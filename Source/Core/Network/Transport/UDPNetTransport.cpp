#include "pch.h"

#include "Core/Network/Transport/UDPNetTransport.h"

#include "Core/Network/Transport/PlatformSocketUtil.h"

namespace sw
{
    UDPNetTransport::UDPNetTransport()
        : _localAddress{}
        , _socketHandle{ PlatformSocketUtil::kInvalidSocket }
    {
    }

    UDPNetTransport::~UDPNetTransport()
    {
        close();
    }

    bool UDPNetTransport::open( uint16 port, bool bLoopbackOnly, UDPPortSharing sharing )
    {
        close();
        _socketHandle = PlatformSocketUtil::openUDPSocket( port, bLoopbackOnly, sharing );
        if ( _socketHandle == PlatformSocketUtil::kInvalidSocket )
            return false;
        _localAddress = NetAddress::makeLoopback( PlatformSocketUtil::getBoundPort( _socketHandle ) );
        return true;
    }

    void UDPNetTransport::close()
    {
        PlatformSocketUtil::closeSocket( _socketHandle );
        _socketHandle = PlatformSocketUtil::kInvalidSocket;
    }

    bool UDPNetTransport::isOpen() const
    {
        return _socketHandle != PlatformSocketUtil::kInvalidSocket;
    }

    bool UDPNetTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        const bool bCannotSendPacket = isOpen() == false || size <= 0 || size > kNetMaxPacketSize;
        if ( bCannotSendPacket )
            return false;
        return PlatformSocketUtil::sendTo( _socketHandle, to, pData, size );
    }

    int32 UDPNetTransport::sendBatch( const NetDatagramView* pViews, int32 count )
    {
        if ( isOpen() == false )
            return 0;
        // 보낼 수 없는 크기는 단건 `send` 처럼 손실로 세고 건너뛴다. 나머지를 모아 시스템 호출 한 번으로 보낸다.
        int32 sentCount = 0;
        for ( int32 first = 0; first < count; first += PlatformSocketUtil::kMaxBatch )
        {
            NetDatagramView arrValid[PlatformSocketUtil::kMaxBatch];
            int32           validCount = 0;
            const int32     last       = first + PlatformSocketUtil::kMaxBatch < count ? first + PlatformSocketUtil::kMaxBatch : count;
            for ( int32 index = first; index < last; ++index )
            {
                if ( pViews[index]._size > 0 && pViews[index]._size <= kNetMaxPacketSize )
                    arrValid[validCount++] = pViews[index];
            }
            sentCount += PlatformSocketUtil::sendBatch( _socketHandle, arrValid, validCount );
        }
        return sentCount;
    }

    int32 UDPNetTransport::receiveBatch( NetReceivedDatagram* pOut, int32 maxCount )
    {
        if ( isOpen() == false || maxCount <= 0 )
            return 0;
        const int32 batchCount = maxCount < PlatformSocketUtil::kMaxBatch ? maxCount : PlatformSocketUtil::kMaxBatch;
        // 한도보다 한 바이트 크게 잡아 너무 큰 데이터그램을 알아본다(단건 `receive` 와 같다). 걸러 내고 남으면 앞으로 모은다.
        for ( int32 attempt = 0; attempt < 4; ++attempt )
        {
            SocketReceiveEntry arrEntry[PlatformSocketUtil::kMaxBatch];
            for ( int32 index = 0; index < batchCount; ++index )
            {
                pOut[index]._buffer.resize( static_cast<size_t>( kNetMaxPacketSize + 1 ) );
                arrEntry[index]._pBuffer    = pOut[index]._buffer.data();
                arrEntry[index]._bufferSize = static_cast<int32>( pOut[index]._buffer.size() );
            }
            const int32 receivedCount = PlatformSocketUtil::receiveBatch( _socketHandle, arrEntry, batchCount );
            if ( receivedCount <= 0 )
                break;
            int32 keptCount = 0;
            for ( int32 index = 0; index < receivedCount; ++index )
            {
                const int32 size = arrEntry[index]._size;
                if ( size <= 0 || size > kNetMaxPacketSize )
                    continue;
                if ( keptCount != index )
                    pOut[keptCount]._buffer.swap( pOut[index]._buffer );
                pOut[keptCount]._from = arrEntry[index]._from;
                pOut[keptCount]._buffer.resize( static_cast<size_t>( size ) );
                ++keptCount;
            }
            if ( keptCount > 0 )
                return keptCount;
        }
        return 0;
    }

    bool UDPNetTransport::waitForReceive( float64 timeoutSeconds )
    {
        if ( isOpen() == false )
            return INetTransport::waitForReceive( timeoutSeconds );
        const int32 timeoutMilli = static_cast<int32>( timeoutSeconds * constant::kMillisecondsPerSecond + 0.5 );
        return PlatformSocketUtil::waitReadable( _socketHandle, timeoutMilli > 0 ? timeoutMilli : 0 );
    }

    bool UDPNetTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
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
            const bool bValidPacketReceived = result == SocketReceiveResult::Received && size > 0 && size <= kNetMaxPacketSize;
            if ( bValidPacketReceived )
            {
                outBuffer.resize( static_cast<size_t>( size ) );
                return true;
            }
        }
        outBuffer.clear();
        return false;
    }
} // namespace sw
