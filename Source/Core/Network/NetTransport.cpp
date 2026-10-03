#include "pch.h"

#include "Core/Network/NetTransport.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    // ------------------------------------------------------------------------------
    // LoopbackNetwork
    // ------------------------------------------------------------------------------
    LoopbackNetwork::LoopbackNetwork( uint32 seed )
        : _listEndpoint{}
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

    LoopbackTransport* LoopbackNetwork::createEndpoint( uint16 port )
    {
        const NetAddress address = NetAddress::makeLoopback( port );
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
            const float32 jitter = ( nextRandom() * 2.0f - 1.0f ) * _conditions._jitter;
            packet._from         = from;
            packet._to           = to;
            packet._deliverTime  = _time + MathUtil::max( 0.0f, _conditions._latency + jitter );
            packet._order        = _order++;
            _listInFlight.push_back( std::move( packet ) );
        }
    }

    void LoopbackNetwork::advance( float64 time )
    {
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

    bool LoopbackTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
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
