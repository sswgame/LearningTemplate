#include "pch.h"

#include "Core/Network/Transport/NetTransport.h"

#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace sw
{
    // ------------------------------------------------------------------------------
    // LoopbackNetwork
    // ------------------------------------------------------------------------------
    // ------------------------------------------------------------------------------
    // INetTransport
    // ------------------------------------------------------------------------------
    bool INetTransport::waitForReceive( float64 timeoutSeconds )
    {
        if ( timeoutSeconds > 0.0 )
            std::this_thread::sleep_for( std::chrono::duration<float64>( timeoutSeconds ) );
        return false;
    }

    LoopbackNetwork::LoopbackNetwork( uint32 seed )
        : _mutex{}
        , _listEndpoint{}
        , _listInFlight{}
        , _conditions{}
        , _time{ 0.0 }
        , _order{ 0 }
        , _droppedCount{ 0 }
        , _deliveredCount{ 0 }
        , _randomState{ seed != 0 ? seed : 0x9E3779B9u }
    {
    }

    float32 LoopbackNetwork::nextRandom()
    {
        // xorshift32 — Core 는 GameFramework 의 난수를 쓸 수 없다.
        _randomState ^= _randomState << 13;
        _randomState ^= _randomState >> 17;
        _randomState ^= _randomState << 5;
        return static_cast<float32>( _randomState >> 8 ) / 16777216.0f;
    }

    void LoopbackNetwork::setConditions( const LoopbackConditions& conditions )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _conditions = conditions;
    }

    LoopbackConditions LoopbackNetwork::getConditions() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _conditions;
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
        if ( nextRandom() < _conditions._lossRate )
        {
            ++_droppedCount;
            return;
        }
        const int32 copyCount = nextRandom() < _conditions._duplicateRate ? 2 : 1;
        for ( int32 copy = 0; copy < copyCount; ++copy )
        {
            InFlight packet;
            packet._buffer.assign( pData, pData + size );
            if ( size > 0 && nextRandom() < _conditions._corruptRate )
            {
                const int32 index = MathUtil::min( size - 1, static_cast<int32>( nextRandom() * static_cast<float32>( size ) ) );
                packet._buffer[static_cast<size_t>( index )] ^= 0x5A;
            }
            const float64 jitter = static_cast<float64>( nextRandom() * 2.0f - 1.0f ) * _conditions._jitter;
            packet._from         = from;
            packet._to           = to;
            packet._deliverTime  = _time + MathUtil::max( 0.0, _conditions._latency + jitter );
            packet._order        = _order++;
            _listInFlight.push_back( std::move( packet ) );
        }
    }

    void LoopbackNetwork::advance( float64 time )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _time = MathUtil::max( _time, time );
        // 도착 시각 → 보낸 순서로 배달(같은 시각이면 보낸 순서 — 흔들림이 없으면 순서가 지켜진다).
        std::stable_sort( _listInFlight.begin(), _listInFlight.end(), []( const InFlight& lhs, const InFlight& rhs )
        {
            return lhs._deliverTime != rhs._deliverTime ? lhs._deliverTime < rhs._deliverTime : lhs._order < rhs._order;
        } );
        size_t delivered = 0;
        while ( delivered < _listInFlight.size() && _listInFlight[delivered]._deliverTime <= _time )
        {
            InFlight& packet = _listInFlight[delivered];
            for ( LoopbackTransport& endpoint : _listEndpoint )
            {
                if ( endpoint.getLocalAddress() == packet._to )
                {
                    endpoint.deliver( packet._from, std::move( packet._buffer ) );
                    ++_deliveredCount;
                    break;
                }
            }
            ++delivered;
        }
        _listInFlight.erase( _listInFlight.begin(), _listInFlight.begin() + static_cast<std::ptrdiff_t>( delivered ) );
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
        // 배달은 누군가의 `update`(망 시각 앞으로)가 한다 — 깨울 수단 없이 짧게 나눠 자며 받은 편지함을 본다.
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

    void LoopbackTransport::update( float64 time ) { _pNetwork->advance( time ); }

    void LoopbackTransport::deliver( const NetAddress& from, vector<uint8>&& buffer ) { _listInbox.push_back( Datagram{ std::move( buffer ), from } ); }
} // namespace sw
