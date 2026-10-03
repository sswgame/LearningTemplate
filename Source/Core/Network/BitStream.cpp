#include "pch.h"

#include "Core/Network/BitStream.h"

#include "Core/Math/MathUtil.h"

#include <cstring>

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

    void BitWriter::writeBits( uint32 value, int32 bitCount )
    {
        if ( bitCount <= 0 )
            return;
        bitCount = MathUtil::min( bitCount, 32 );
        if ( bitCount < 32 )
            value &= ( 1u << bitCount ) - 1u;
        // 낮은 비트부터 — 바이트 안에서도 낮은 비트부터 채운다.
        for ( int32 bitIndex = 0; bitIndex < bitCount; ++bitIndex )
        {
            const int32 byteIndex = _bitCount >> 3;
            if ( byteIndex >= static_cast<int32>( _buffer.size() ) )
                _buffer.push_back( 0 );
            if ( ( value >> bitIndex ) & 1u )
                _buffer[static_cast<size_t>( byteIndex )] = static_cast<uint8>( _buffer[static_cast<size_t>( byteIndex )] | ( 1u << ( _bitCount & 7 ) ) );
            ++_bitCount;
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
        for ( int32 index = 0; index < byteCount; ++index )
            writeBits( pData[index], 8 );
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
        , _bitCapacity{ MathUtil::max( 0, byteCount ) * 8 }
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
        uint32 value = 0;
        for ( int32 bitIndex = 0; bitIndex < bitCount; ++bitIndex )
        {
            const uint8 byte = _pData[_bitPosition >> 3];
            value |= static_cast<uint32>( ( byte >> ( _bitPosition & 7 ) ) & 1u ) << bitIndex;
            ++_bitPosition;
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

    bool BitReader::readBytes( uint8* pOutData, int32 byteCount )
    {
        if ( byteCount < 0 || _bitPosition + byteCount * 8 > _bitCapacity )
        {
            _bOverflow = SW_TRUE;
            return false;
        }
        for ( int32 index = 0; index < byteCount; ++index )
            pOutData[index] = static_cast<uint8>( readBits( 8 ) );
        return true;
    }

    void BitReader::alignToByte()
    {
        const int32 remainder = _bitPosition & 7;
        if ( remainder != 0 )
            (void)readBits( 8 - remainder );
    }
} // namespace sw
