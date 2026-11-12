#include "pch.h"

#include "Core/Network/Transport/NetTransport.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/thread.h"
#include "Core/Time/MonotonicClock.h"

#include <chrono>
#include <thread>

namespace sw
{
    // ------------------------------------------------------------------------------
    // INetTransport
    // ------------------------------------------------------------------------------
    bool INetTransport::waitForReceive( float64 timeoutSeconds )
    {
        if ( timeoutSeconds > 0.0 )
            ThreadUtil::sleepForSeconds( timeoutSeconds );
        return false;
    }

    int32 INetTransport::sendBatch( const NetDatagramView* pViews, int32 count )
    {
        int32 sentCount = 0;
        for ( int32 index = 0; index < count; ++index )
        {
            if ( send( pViews[index]._to, pViews[index]._pData, pViews[index]._size ) )
                ++sentCount;
        }
        return sentCount;
    }

    int32 INetTransport::receiveBatch( NetReceivedDatagram* pOut, int32 maxCount )
    {
        int32 receivedCount = 0;
        while ( receivedCount < maxCount && receive( pOut[receivedCount]._from, pOut[receivedCount]._buffer ) )
        {
            ++receivedCount;
        }
        return receivedCount;
    }

    // ------------------------------------------------------------------------------
    // LoopbackNetwork
    // ------------------------------------------------------------------------------
    LoopbackNetwork::LoopbackNetwork()
        : _mutex{}
        , _listEndpoint{}
        , _listInFlight{}
        , _droppedCount{ 0 }
        , _deliveredCount{ 0 }
    {
    }

    uint64 LoopbackNetwork::getDroppedCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _droppedCount;
    }

    uint64 LoopbackNetwork::getDeliveredCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _deliveredCount;
    }

    LoopbackTransport* LoopbackNetwork::createEndpoint( uint16 port )
    {
        ScopedLock<mutex> lock{ _mutex };
        const NetAddress  address = NetAddress::makeLoopback( port );
        if ( VectorUtil::containsBy<&LoopbackTransport::getLocalAddress>( _listEndpoint, address ) )
            return nullptr;
        _listEndpoint.emplace_back( this, address );
        return &_listEndpoint.back();
    }

    void LoopbackNetwork::enqueue( const NetAddress& from, const NetAddress& to, const uint8* pData, int32 size )
    {
        ScopedLock<mutex> lock{ _mutex };
        InFlight          packet;
        packet._buffer.assign( pData, pData + size );
        packet._from = from;
        packet._to   = to;
        _listInFlight.push_back( std::move( packet ) );
    }

    void LoopbackNetwork::deliverInFlight()
    {
        ScopedLock<mutex> lock{ _mutex };
        // 보낸 순서대로 — 한 엔드포인트가 보낸 것은 순서가 지켜진다.
        for ( InFlight& packet : _listInFlight )
        {
            bool bDelivered = false;
            for ( LoopbackTransport& endpoint : _listEndpoint )
            {
                if ( endpoint.getLocalAddress() == packet._to )
                {
                    endpoint.deliver( packet._from, std::move( packet._buffer ) );
                    bDelivered = true;
                    break;
                }
            }
            // 받을 엔드포인트가 없으면(닫힌 포트) 버린다 — UDP 와 같다.
            _deliveredCount += bDelivered ? 1u : 0u;
            _droppedCount += bDelivered ? 0u : 1u;
        }
        _listInFlight.clear();
    }

    bool LoopbackNetwork::takeDatagram( LoopbackTransport* pEndpoint, NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        ScopedLock<mutex> lock{ _mutex };
        return pEndpoint->popInbox( outFrom, outBuffer );
    }

    bool LoopbackNetwork::hasDatagram( const LoopbackTransport* pEndpoint ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return pEndpoint->hasInbox();
    }

    // ------------------------------------------------------------------------------
    // LoopbackTransport
    // ------------------------------------------------------------------------------
    LoopbackTransport::LoopbackTransport( LoopbackNetwork* pNetwork, const NetAddress& address )
        : _pNetwork{ pNetwork }
        , _listInbox{}
        , _address{ address }
        , _bWakeRequested{ false }
    {
    }

    bool LoopbackTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        const bool bInvalidPacket = size <= 0 || size > kNetMaxPacketSize || pData == nullptr;
        if ( bInvalidPacket )
            return false;
        _pNetwork->enqueue( _address, to, pData, size );
        return true;
    }

    bool LoopbackTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        return _pNetwork->takeDatagram( this, outFrom, outBuffer );
    }

    bool LoopbackTransport::waitForReceive( float64 timeoutSeconds )
    {
        // 날아가는 것을 여기서도 배달한다 — 엔드포인트 스레드는 다음 기한까지 자므로 다른 엔드포인트의 `update` 가 배달해 주기를 기다리면 그 기한만큼 늦는다.
        // 소켓처럼 깨울 수단이 없어 짧게 나눠 자며 받은 편지함과 깨움(`wake`)을 본다.
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( timeoutSeconds * constant::time::kMillisecondsPerSecond ) );
        for ( ;; )
        {
            _pNetwork->deliverInFlight();
            if ( _pNetwork->hasDatagram( this ) )
                return true;
            if ( _bWakeRequested.exchange( false, std::memory_order_acq_rel ) || deadline.isExpired() )
                return false;
            ThreadUtil::sleepForMicroseconds( 200 );
        }
    }

    void LoopbackTransport::wake()
    {
        _bWakeRequested.store( true, std::memory_order_release );
    }

    bool LoopbackTransport::popInbox( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        if ( _listInbox.empty() )
            return false;
        outFrom   = _listInbox.front()._from;
        outBuffer = std::move( _listInbox.front()._buffer );
        _listInbox.pop_front();
        return true;
    }

    void LoopbackTransport::update( float64 time )
    {
        (void)time; // 루프백은 시각을 쓰지 않는다 — 날아가는 것을 모두 배달한다
        _pNetwork->deliverInFlight();
    }

    void LoopbackTransport::deliver( const NetAddress& from, vector<uint8>&& buffer )
    {
        _listInbox.push_back( Datagram{ std::move( buffer ), from } );
    }
} // namespace sw
