#include "pch.h"

#include "Core/Network/Transport/NetTransport.h"

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
            std::this_thread::sleep_for( std::chrono::duration<float64>( timeoutSeconds ) );
        return false;
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
        std::scoped_lock<mutex> lock{ _mutex };
        return _droppedCount;
    }

    uint64 LoopbackNetwork::getDeliveredCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _deliveredCount;
    }

    LoopbackTransport* LoopbackNetwork::createEndpoint( uint16 port )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const NetAddress        address = NetAddress::makeLoopback( port );
        for ( const LoopbackTransport& endpoint : _listEndpoint )
        {
            if ( endpoint.getLocalAddress() == address )
                return nullptr;
        }
        _listEndpoint.emplace_back( this, address );
        return &_listEndpoint.back();
    }

    void LoopbackNetwork::enqueue( const NetAddress& from, const NetAddress& to, const uint8* pData, int32 size )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        InFlight                packet;
        packet._buffer.assign( pData, pData + size );
        packet._from = from;
        packet._to   = to;
        _listInFlight.push_back( std::move( packet ) );
    }

    void LoopbackNetwork::deliverInFlight()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        // 보낸 순서대로 — 한 끝점이 보낸 것은 순서가 지켜진다.
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
            // 받을 끝점이 없으면(닫힌 포트) 버린다 — UDP 와 같다.
            _deliveredCount += bDelivered ? 1u : 0u;
            _droppedCount += bDelivered ? 0u : 1u;
        }
        _listInFlight.clear();
    }

    bool LoopbackNetwork::takeDatagram( LoopbackTransport* pEndpoint, NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return pEndpoint->popInbox( outFrom, outBuffer );
    }

    bool LoopbackNetwork::hasDatagram( const LoopbackTransport* pEndpoint ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return pEndpoint->hasInbox();
    }

    // ------------------------------------------------------------------------------
    // LoopbackTransport
    // ------------------------------------------------------------------------------
    LoopbackTransport::LoopbackTransport( LoopbackNetwork* pNetwork, const NetAddress& address )
        : _pNetwork{ pNetwork }
        , _listInbox{}
        , _address{ address }
    {
    }

    bool LoopbackTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        if ( size <= 0 || size > kNetMaxPacketSize || pData == nullptr )
            return false;
        _pNetwork->enqueue( _address, to, pData, size );
        return true;
    }

    bool LoopbackTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer ) { return _pNetwork->takeDatagram( this, outFrom, outBuffer ); }

    bool LoopbackTransport::waitForReceive( float64 timeoutSeconds )
    {
        // 배달은 누군가의 `update`(`deliverInFlight`)가 한다 — 깨울 수단 없이 짧게 나눠 자며 받은 편지함을 본다.
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( timeoutSeconds * 1000.0 ) );
        while ( _pNetwork->hasDatagram( this ) == false )
        {
            if ( deadline.isExpired() )
                return false;
            std::this_thread::sleep_for( std::chrono::microseconds( 200 ) );
        }
        return true;
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

    void LoopbackTransport::deliver( const NetAddress& from, vector<uint8>&& buffer ) { _listInbox.push_back( Datagram{ std::move( buffer ), from } ); }
} // namespace sw
