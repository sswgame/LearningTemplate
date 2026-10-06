#include "pch.h"

#include "Core/Network/Transport/StreamSendQueue.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct StreamSendQueueInternal
        {
            static constexpr size_t kMaxFreeBuffers = 4;
        };
    } // namespace
} // namespace sw

namespace sw
{
    StreamSendQueue::StreamSendQueue()
        : _listChunk{}
        , _listFreeBuffer{}
        , _queuedBytes{ 0 }
        , _highWatermarkBytes{ 256 * 1024 }
        , _lowWatermarkBytes{ 64 * 1024 }
        , _maxQueuedBytes{ 4 * 1024 * 1024 }
        , _bAboveHighWatermark{ SW_FALSE }
    {
    }

    void StreamSendQueue::configure( int32 highWatermarkBytes, int32 lowWatermarkBytes, int32 maxQueuedBytes )
    {
        _highWatermarkBytes = highWatermarkBytes;
        _lowWatermarkBytes  = lowWatermarkBytes < highWatermarkBytes ? lowWatermarkBytes : highWatermarkBytes / 2;
        _maxQueuedBytes     = maxQueuedBytes > highWatermarkBytes ? maxQueuedBytes : highWatermarkBytes;
    }

    StreamSendResult StreamSendQueue::append( const uint8* pData, int32 size )
    {
        if ( size <= 0 )
            return _bAboveHighWatermark == SW_TRUE ? StreamSendResult::QueuedAboveHighWatermark : StreamSendResult::Queued;
        if ( static_cast<int64>( _queuedBytes ) + size > _maxQueuedBytes )
            return StreamSendResult::QueueFull;
        int32 remaining = size;
        while ( remaining > 0 )
        {
            if ( _listChunk.empty() || static_cast<int32>( _listChunk.back()._bytes.size() ) >= kChunkBytes )
            {
                Chunk chunk;
                if ( _listFreeBuffer.empty() == false )
                {
                    chunk._bytes = std::move( _listFreeBuffer.back() );
                    _listFreeBuffer.pop_back();
                    chunk._bytes.clear();
                }
                else
                {
                    chunk._bytes.reserve( static_cast<size_t>( kChunkBytes ) );
                }
                _listChunk.push_back( std::move( chunk ) );
            }
            vector<uint8>& bytes = _listChunk.back()._bytes;
            const int32    room  = kChunkBytes - static_cast<int32>( bytes.size() );
            const int32    take  = remaining < room ? remaining : room;
            bytes.insert( bytes.end(), pData + ( size - remaining ), pData + ( size - remaining ) + take );
            remaining -= take;
        }
        _queuedBytes += size;
        if ( _queuedBytes > _highWatermarkBytes )
            _bAboveHighWatermark = SW_TRUE;
        return _bAboveHighWatermark == SW_TRUE ? StreamSendResult::QueuedAboveHighWatermark : StreamSendResult::Queued;
    }

    int32 StreamSendQueue::collectSpans( StreamSendSpan* pOutSpan, int32 maxSpanCount, int32 maxBytes ) const
    {
        int32 spanCount = 0;
        int32 byteCount = 0;
        for ( const Chunk& chunk : _listChunk )
        {
            if ( spanCount >= maxSpanCount || byteCount >= maxBytes )
                break;
            const int32 available = static_cast<int32>( chunk._bytes.size() ) - chunk._readOffset;
            if ( available <= 0 )
                continue;
            const int32 take      = available < maxBytes - byteCount ? available : maxBytes - byteCount;
            pOutSpan[spanCount++] = StreamSendSpan{ chunk._bytes.data() + chunk._readOffset, take };
            byteCount += take;
        }
        return spanCount;
    }

    bool StreamSendQueue::consume( int32 byteCount )
    {
        int32 remaining = byteCount < _queuedBytes ? byteCount : _queuedBytes;
        _queuedBytes -= remaining;
        while ( remaining > 0 && _listChunk.empty() == false )
        {
            Chunk&      front     = _listChunk.front();
            const int32 available = static_cast<int32>( front._bytes.size() ) - front._readOffset;
            const int32 take      = remaining < available ? remaining : available;
            front._readOffset += take;
            remaining -= take;
            if ( front._readOffset >= static_cast<int32>( front._bytes.size() ) )
            {
                if ( _listFreeBuffer.size() < StreamSendQueueInternal::kMaxFreeBuffers )
                    _listFreeBuffer.push_back( std::move( front._bytes ) );
                _listChunk.pop_front();
            }
        }
        if ( _bAboveHighWatermark == SW_TRUE && _queuedBytes <= _lowWatermarkBytes )
        {
            _bAboveHighWatermark = SW_FALSE;
            return true;
        }
        return false;
    }

    void StreamSendQueue::clear()
    {
        _listChunk.clear();
        _queuedBytes         = 0;
        _bAboveHighWatermark = SW_FALSE;
    }
} // namespace sw
