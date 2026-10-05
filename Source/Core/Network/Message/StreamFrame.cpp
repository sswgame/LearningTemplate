#include "pch.h"

#include "Core/Network/Message/StreamFrame.h"

namespace sw
{
    namespace
    {
        struct StreamFrameInternal
        {
            static uint32 readUint32( const uint8* pData )
            {
                return static_cast<uint32>( pData[0] ) | ( static_cast<uint32>( pData[1] ) << 8 ) | ( static_cast<uint32>( pData[2] ) << 16 ) |
                       ( static_cast<uint32>( pData[3] ) << 24 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    StreamFrameDecoder::StreamFrameDecoder( int32 maxBodySize )
        : _bytes{}
        , _readOffset{ 0 }
        , _maxBodySize{ maxBodySize }
    {
    }

    void StreamFrameDecoder::append( const uint8* pData, int32 size )
    {
        // 읽은 앞부분은 절반을 넘을 때만 지운다 — 작은 조각마다 앞으로 당기면 O(n²) 다.
        if ( _readOffset > 0 && _readOffset * 2 >= static_cast<int32>( _bytes.size() ) )
        {
            _bytes.erase( _bytes.begin(), _bytes.begin() + _readOffset );
            _readOffset = 0;
        }
        _bytes.insert( _bytes.end(), pData, pData + size );
    }

    StreamFrameDecodeResult StreamFrameDecoder::next( StreamFrameView& outFrame )
    {
        const int32 available = static_cast<int32>( _bytes.size() ) - _readOffset;
        if ( available < 4 )
            return StreamFrameDecodeResult::NeedMore;
        const uint8* pHeader = _bytes.data() + _readOffset;
        const uint32 length  = StreamFrameInternal::readUint32( pHeader );
        // 길이는 종류 · 깃발 2 바이트 + 몸 — 몸이 오기 전에 상한을 본다.
        if ( length < 2 || static_cast<uint64>( length ) - 2 > static_cast<uint64>( _maxBodySize ) )
            return StreamFrameDecodeResult::Malformed;
        if ( available < 4 + static_cast<int32>( length ) )
            return StreamFrameDecodeResult::NeedMore;
        const uint8 kind  = pHeader[4];
        const uint8 flags = pHeader[5];
        if ( kind >= static_cast<uint8>( StreamFrameKind::Count ) || ( flags & static_cast<uint8>( ~StreamFrameFlag::kKnownMask ) ) != 0 )
            return StreamFrameDecodeResult::Malformed;
        outFrame._pBody    = pHeader + StreamFrameConstant::kHeaderSize;
        outFrame._bodySize = static_cast<int32>( length ) - 2;
        outFrame._kind     = static_cast<StreamFrameKind>( kind );
        outFrame._flags    = flags;
        _readOffset += 4 + static_cast<int32>( length );
        return StreamFrameDecodeResult::Frame;
    }

    int32 StreamFrameDecoder::getBufferedBytes() const { return static_cast<int32>( _bytes.size() ) - _readOffset; }

    bool StreamFrameEncoder::appendFrame( vector<uint8>& outBytes, StreamFrameKind kind, uint8 flags, const uint8* pBody, int32 bodySize, int32 maxBodySize )
    {
        if ( bodySize < 0 || bodySize > maxBodySize )
            return false;
        const uint32 length                                      = static_cast<uint32>( bodySize ) + 2;
        const uint8  arrHeader[StreamFrameConstant::kHeaderSize] = {
            static_cast<uint8>( length ),
            static_cast<uint8>( length >> 8 ),
            static_cast<uint8>( length >> 16 ),
            static_cast<uint8>( length >> 24 ),
            static_cast<uint8>( kind ),
            flags,
        };
        outBytes.insert( outBytes.end(), arrHeader, arrHeader + StreamFrameConstant::kHeaderSize );
        if ( bodySize > 0 )
            outBytes.insert( outBytes.end(), pBody, pBody + bodySize );
        return true;
    }
} // namespace sw
