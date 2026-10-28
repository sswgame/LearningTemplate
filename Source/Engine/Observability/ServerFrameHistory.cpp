#include "pch.h"

#include "Engine/Observability/ServerFrameHistory.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"

namespace sw
{
    ServerFrameHistory::ServerFrameHistory()
        : _listSample{}
        , _nextIndex{ 0 }
        , _count{ 0 }
    {
    }

    void ServerFrameHistory::initialize( uint32 capacity )
    {
        _listSample.assign( capacity, ServerFrameSample{} );
        _nextIndex = 0;
        _count     = 0;
    }

    void ServerFrameHistory::record( const ServerFrameSample& sample )
    {
        const uint32 capacity = getCapacity();
        if ( capacity == 0 )
            return;
        _listSample[_nextIndex] = sample;
        _nextIndex              = ( _nextIndex + 1 ) % capacity;
        if ( _count < capacity )
            ++_count;
    }

    bool ServerFrameHistory::tryGetSample( uint32 index, ServerFrameSample& outSample ) const
    {
        if ( index >= _count )
            return false;
        const uint32 capacity    = getCapacity();
        const uint32 oldestIndex = ( _nextIndex + capacity - _count ) % capacity;
        outSample                = _listSample[( oldestIndex + index ) % capacity];
        return true;
    }

    bool ServerFrameHistory::tryGetLatest( ServerFrameSample& outSample ) const
    {
        return _count > 0 && tryGetSample( _count - 1, outSample );
    }

    uint32 ServerFrameHistory::writeCrashTail( utf8* pOutText, uint32 capacity, uint32 maxTicks ) const
    {
        if ( pOutText == nullptr || capacity == 0 )
            return 0;
        pOutText[0]        = '\0';
        const uint32 total = getCapacity();
        const uint32 count = _count < total ? _count : total; // 읽는 도중 값이 바뀌어도 칸 수는 용량을 넘지 않는다
        if ( total == 0 || count == 0 )
            return 0;
        const uint32 take  = count < maxTicks ? count : maxTicks;
        const uint32 first = count - take;
        uint32       length{ 0 };
        const auto   append = [&]( const ServerFrameSample* pSample )
        {
            const uint32 room = capacity - length;
            if ( room < 2 )
                return false;
            if ( pSample == nullptr )
                formatstring( pOutText + length, room, "ticks    : last %# of %# kept (oldest first)\n", take, count );
            else
                formatstring( pOutText + length, room, "tick     : %# body_us=%# connections=%# dropped=%#\n", pSample->_tick, pSample->_bodyMicroseconds, pSample->_connectionCount,
                              pSample->_droppedTickTotal );
            length += static_cast<uint32>( StringUtil::strlen( pOutText + length ) );
            return true;
        };
        if ( append( nullptr ) == false )
            return 0;
        const uint32 oldestIndex = ( _nextIndex + total - count ) % total;
        for ( uint32 index = first; index < count; ++index )
        {
            const ServerFrameSample sample = _listSample[( oldestIndex + index ) % total];
            if ( append( &sample ) == false )
                break;
        }
        return length;
    }

    void ServerFrameHistory::writeReport( string& outText, string_view reason, string_view sessionID, string_view extraText ) const
    {
        int64  sumMicroseconds{ 0 };
        int64  maxMicroseconds{ 0 };
        uint64 maxTick{ 0 };
        for ( uint32 index = 0; index < _count; ++index )
        {
            ServerFrameSample sample;
            if ( tryGetSample( index, sample ) == false )
                continue;
            sumMicroseconds += sample._bodyMicroseconds;
            if ( sample._bodyMicroseconds > maxMicroseconds )
            {
                maxMicroseconds = sample._bodyMicroseconds;
                maxTick         = sample._tick;
            }
        }
        const int64 averageMicroseconds = _count > 0 ? sumMicroseconds / static_cast<int64>( _count ) : 0;
        outText.append( "reason " ).append( reason ).append( "\n" );
        outText.append( "session " ).append( sessionID ).append( "\n" );
        outText.append( "samples " ).append( to_string( _count ) ).append( " capacity " ).append( to_string( getCapacity() ) ).append( "\n" );
        outText.append( "body_us avg " ).append( to_string( averageMicroseconds ) ).append( " max " ).append( to_string( maxMicroseconds ) ).append( " at tick " ).append( to_string( maxTick ) ).append( "\n" );
        for ( uint32 index = 0; index < _count; ++index )
        {
            ServerFrameSample sample;
            if ( tryGetSample( index, sample ) == false )
                continue;
            outText.append( "tick=" ).append( to_string( sample._tick ) );
            outText.append( " body_us=" ).append( to_string( sample._bodyMicroseconds ) );
            outText.append( " connections=" ).append( to_string( sample._connectionCount ) );
            outText.append( " dropped=" ).append( to_string( sample._droppedTickTotal ) ).append( "\n" );
        }
        if ( extraText.empty() == false )
            outText.append( extraText ).append( "\n" );
    }
} // namespace sw
