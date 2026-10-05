#include "pch.h"

#include "Core/Network/BitStream.h"

#include "Core/Math/MathUtil.h"

#include <cstring>

namespace sw
{
    namespace
    {
        struct BitReaderInternal
        {
            static constexpr int32 kMaxByteCount = 0x7FFFFFFF / 8; ///< 비트 위치가 int32 라 읽을 수 있는 바이트 상한
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // BitWriter
    // ------------------------------------------------------------------------------
    BitWriter::BitWriter()
        : _buffer{}
        , _bitCount{ 0 }
    {
    }

    BitWriter::BitWriter( vector<uint8>&& reuseBuffer )
        : _buffer{ std::move( reuseBuffer ) }
        , _bitCount{ 0 }
    {
        _buffer.clear();
    }

    vector<uint8> BitWriter::releaseBytes()
    {
        vector<uint8> bytes = std::move( _buffer );
        _buffer.clear();
        _bitCount = 0;
        return bytes;
    }

    void BitWriter::writeBits( uint32 value, int32 bitCount )
    {
        if ( bitCount <= 0 )
            return;
        bitCount = MathUtil::min( bitCount, 32 );
        if ( bitCount < 32 )
            value &= ( 1u << bitCount ) - 1u;
        // 낮은 비트부터 — 바이트 안에서도 낮은 비트부터 채운다. 한 번에 바이트의 남은 칸만큼씩(32 비트도 5 번 안).
        const int32 newBitCount = _bitCount + bitCount;
        _buffer.resize( static_cast<size_t>( ( newBitCount + 7 ) >> 3 ), 0 );
        uint8* pBytes = _buffer.data();
        while ( bitCount > 0 )
        {
            const int32  bitOffset = _bitCount & 7;
            const int32  chunkBits = MathUtil::min( bitCount, 8 - bitOffset );
            const uint32 chunk     = value & ( ( 1u << chunkBits ) - 1u );
            pBytes[_bitCount >> 3] = static_cast<uint8>( pBytes[_bitCount >> 3] | ( chunk << bitOffset ) );
            value >>= chunkBits;
            bitCount -= chunkBits;
            _bitCount += chunkBits;
        }
    }

    void BitWriter::writeInt( int32 value, int32 minValue, int32 maxValue )
    {
        const int32 clamped = MathUtil::clamp( value, minValue, maxValue );
        writeBits( static_cast<uint32>( static_cast<int64>( clamped ) - minValue ), BitMath::computeBitsRequired( minValue, maxValue ) );
    }

    void BitWriter::writeFloat( float32 value )
    {
        uint32 bits = 0;
        std::memcpy( &bits, &value, sizeof( bits ) );
        writeBits( bits, 32 );
    }

    void BitWriter::writeQuantizedFloat( float32 value, float32 minValue, float32 maxValue, float32 resolution )
    {
        const float32 step     = MathUtil::max( 1.0e-6f, resolution );
        const uint32  maxIndex = static_cast<uint32>( ( maxValue - minValue ) / step + 0.5f );
        const float32 clamped  = MathUtil::clamp( value, minValue, maxValue );
        const uint32  index    = MathUtil::min( maxIndex, static_cast<uint32>( ( clamped - minValue ) / step + 0.5f ) );
        writeBits( index, BitMath::computeBitsRequired( 0, maxIndex ) );
    }

    void BitWriter::writeVarUint( uint64 value )
    {
        do
        {
            const uint32 chunk = static_cast<uint32>( value & 0x7Fu );
            value >>= 7;
            writeBits( chunk | ( value != 0 ? 0x80u : 0u ), 8 );
        } while ( value != 0 );
    }

    void BitWriter::writeVarInt( int64 value ) { writeVarUint( ( static_cast<uint64>( value ) << 1 ) ^ static_cast<uint64>( value >> 63 ) ); }

    void BitWriter::writeBytes( const uint8* pData, int32 byteCount )
    {
        if ( byteCount <= 0 )
            return;
        if ( ( _bitCount & 7 ) == 0 )
        {
            // 바이트 경계 — 통째로 복사한다.
            const size_t offset = static_cast<size_t>( _bitCount >> 3 );
            _buffer.resize( offset + static_cast<size_t>( byteCount ) );
            std::memcpy( _buffer.data() + offset, pData, static_cast<size_t>( byteCount ) );
            _bitCount += byteCount * 8;
            return;
        }
        for ( int32 index = 0; index < byteCount; ++index )
            writeBits( pData[index], 8 );
    }

    void BitWriter::writeBlob( const uint8* pData, int32 byteCount )
    {
        const int32 size = MathUtil::max( 0, byteCount );
        writeVarUint( static_cast<uint64>( size ) );
        writeBytes( pData, size );
    }

    void BitWriter::reserve( int32 byteCount )
    {
        if ( byteCount > 0 )
            _buffer.reserve( static_cast<size_t>( byteCount ) );
    }

    void BitWriter::alignToByte()
    {
        const int32 remainder = _bitCount & 7;
        if ( remainder != 0 )
            writeBits( 0, 8 - remainder );
    }

    void BitWriter::clear()
    {
        _buffer.clear();
        _bitCount = 0;
    }

    // ------------------------------------------------------------------------------
    // BitReader
    // ------------------------------------------------------------------------------
    BitReader::BitReader( const uint8* pData, int32 byteCount )
        : _pData{ pData }
        , _bitCapacity{ MathUtil::clamp( byteCount, 0, BitReaderInternal::kMaxByteCount ) * 8 }
        , _bitPosition{ 0 }
        , _bOverflow{ SW_FALSE }
    {
    }

    uint32 BitReader::readBits( int32 bitCount )
    {
        if ( bitCount <= 0 )
            return 0;
        bitCount = MathUtil::min( bitCount, 32 );
        if ( _bitPosition + bitCount > _bitCapacity )
        {
            _bOverflow   = SW_TRUE;
            _bitPosition = _bitCapacity;
            return 0;
        }
        uint32 value   = 0;
        int32  written = 0;
        while ( written < bitCount )
        {
            const int32  bitOffset = _bitPosition & 7;
            const int32  chunkBits = MathUtil::min( bitCount - written, 8 - bitOffset );
            const uint32 chunk     = ( static_cast<uint32>( _pData[_bitPosition >> 3] ) >> bitOffset ) & ( ( 1u << chunkBits ) - 1u );
            value |= chunk << written;
            written += chunkBits;
            _bitPosition += chunkBits;
        }
        return value;
    }

    int32 BitReader::readInt( int32 minValue, int32 maxValue )
    {
        const uint32 raw = readBits( BitMath::computeBitsRequired( minValue, maxValue ) );
        return static_cast<int32>( MathUtil::min<int64>( maxValue, static_cast<int64>( minValue ) + raw ) );
    }

    float32 BitReader::readFloat()
    {
        const uint32 bits  = readBits( 32 );
        float32      value = 0.0f;
        std::memcpy( &value, &bits, sizeof( value ) );
        return value;
    }

    float32 BitReader::readQuantizedFloat( float32 minValue, float32 maxValue, float32 resolution )
    {
        const float32 step     = MathUtil::max( 1.0e-6f, resolution );
        const uint32  maxIndex = static_cast<uint32>( ( maxValue - minValue ) / step + 0.5f );
        const uint32  index    = MathUtil::min( maxIndex, readBits( BitMath::computeBitsRequired( 0, maxIndex ) ) );
        return MathUtil::min( maxValue, minValue + static_cast<float32>( index ) * step );
    }

    uint64 BitReader::readVarUint()
    {
        uint64 value = 0;
        for ( int32 shift = 0; shift < 64; shift += 7 )
        {
            const uint32 chunk = readBits( 8 );
            value |= static_cast<uint64>( chunk & 0x7Fu ) << shift;
            if ( ( chunk & 0x80u ) == 0 || hasOverflowed() )
                return value;
        }
        _bOverflow = SW_TRUE; // 끝나지 않는 가변 정수 — 깨진 데이터
        return value;
    }

    int64 BitReader::readVarInt()
    {
        const uint64 zigZag = readVarUint();
        return static_cast<int64>( zigZag >> 1 ) ^ -static_cast<int64>( zigZag & 1u );
    }

    bool BitReader::hasBytes( int32 byteCount ) const
    {
        // 비트로 바꾸기 전에 남은 바이트와 비교한다 — `byteCount * 8` 은 2^28 부터 int32 를 넘쳐 음수 · 작은 수가 된다.
        return 0 <= byteCount && byteCount <= ( _bitCapacity - _bitPosition ) / 8;
    }

    bool BitReader::skipBytes( int32 byteCount )
    {
        if ( hasBytes( byteCount ) == false )
        {
            _bOverflow = SW_TRUE;
            return false;
        }
        _bitPosition += byteCount * 8;
        return true;
    }

    bool BitReader::readBytes( uint8* pOutData, int32 byteCount )
    {
        if ( hasBytes( byteCount ) == false )
        {
            _bOverflow = SW_TRUE;
            return false;
        }
        if ( ( _bitPosition & 7 ) == 0 )
        {
            if ( byteCount > 0 )
                std::memcpy( pOutData, _pData + ( _bitPosition >> 3 ), static_cast<size_t>( byteCount ) );
            _bitPosition += byteCount * 8;
            return true;
        }
        for ( int32 index = 0; index < byteCount; ++index )
            pOutData[index] = static_cast<uint8>( readBits( 8 ) );
        return true;
    }

    bool BitReader::readBlobSize( int32 maxSize, int32& outSize )
    {
        outSize           = 0;
        const uint64 size = readVarUint();
        if ( hasOverflowed() || size > static_cast<uint64>( MathUtil::max( 0, maxSize ) ) || hasBytes( static_cast<int32>( size ) ) == false )
        {
            _bOverflow = SW_TRUE;
            return false;
        }
        outSize = static_cast<int32>( size );
        return true;
    }

    bool BitReader::readBlob( vector<uint8>& outBuffer, int32 maxSize )
    {
        int32 size = 0;
        if ( readBlobSize( maxSize, size ) == false )
        {
            outBuffer.clear();
            return false;
        }
        outBuffer.resize( static_cast<size_t>( size ) );
        return size == 0 || readBytes( outBuffer.data(), size );
    }

    bool BitReader::skipBlob( int32 maxSize )
    {
        int32 size = 0;
        return readBlobSize( maxSize, size ) && skipBytes( size );
    }

    void BitReader::alignToByte()
    {
        const int32 remainder = _bitPosition & 7;
        if ( remainder != 0 )
            (void)readBits( 8 - remainder );
    }
} // namespace sw
