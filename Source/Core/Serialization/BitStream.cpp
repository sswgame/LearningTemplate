#include "pch.h"

#include "Core/Serialization/BitStream.h"

#include "Core/Common/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include <cstring>
#include <limits>

namespace sw
{
    namespace
    {
        struct BitStreamInternal
        {
            static constexpr int32 kMaxByteCount = 0x7FFFFFFF / 8; ///< 비트 위치가 int32 라 읽을 수 있는 바이트 상한

            /** @brief 양자화 간격입니다. 0 이하의 간격으로 나누지 않게 아래를 막습니다. */
            static float32 computeQuantizeStep( float32 resolution ) { return MathUtil::max( 1.0e-6f, resolution ); }

            /** @brief 양자화 단계의 마지막 번호입니다. 단계는 0 부터 이 번호까지입니다. */
            static uint32 computeQuantizeMaxIndex( float32 minValue, float32 maxValue, float32 step )
            {
                return static_cast<uint32>( ( maxValue - minValue ) / step + 0.5f );
            }

            /**
             * @brief @p value 를 가장 가까운 단계 번호로 바꿉니다. 범위 안의 단계로 반올림되지 않으면(범위 밖, NaN) false 입니다.
             * @details 반올림이므로 경계에서 반 단계 안쪽으로 넘친 값(정규화한 사원수의 1.0000001)은 마지막 단계입니다. 비교가 NaN 에서 거짓이 되도록 범위 안을 묻습니다.
             */
            [[nodiscard]] static bool tryComputeQuantizeIndex( float32 value, float32 minValue, float32 maxValue, float32 resolution, uint32& outIndex )
            {
                const float32 step     = computeQuantizeStep( resolution );
                const uint32  maxIndex = computeQuantizeMaxIndex( minValue, maxValue, step );
                const float32 scaled   = ( value - minValue ) / step + 0.5f;
                const bool    bInRange = 0.0f <= scaled && scaled < static_cast<float32>( maxIndex ) + 1.0f;
                if ( bInRange == false )
                    return false;
                outIndex = MathUtil::min( maxIndex, static_cast<uint32>( scaled ) );
                return true;
            }
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
        , _bFailed{ SW_FALSE }
    {
    }

    BitWriter::BitWriter( vector<uint8>&& reuseBuffer )
        : _buffer{ std::move( reuseBuffer ) }
        , _bitCount{ 0 }
        , _bFailed{ SW_FALSE }
    {
        _buffer.clear();
    }

    vector<uint8> BitWriter::releaseBytes()
    {
        vector<uint8> bytes = std::move( _buffer );
        _buffer.clear();
        _bitCount = 0;
        _bFailed  = SW_FALSE;
        return bytes;
    }

    void BitWriter::writeBits( uint32 value, int32 bitCount )
    {
        const bool bValidBitCount = 0 <= bitCount && bitCount <= BitMath::kMaxBitsPerCall;
        SW_ASSERT( bValidBitCount );
        if ( bValidBitCount == false )
        {
            _bFailed = SW_TRUE;
            return;
        }
        if ( bitCount == 0 )
            return;
        if ( bitCount < BitMath::kMaxBitsPerCall )
            value &= ( 1u << bitCount ) - 1u;
        // 낮은 비트부터 — 바이트 안에서도 낮은 비트부터 채운다. 지금 끝 바이트부터 닿는 바이트(32 비트에 비트 위치 7 이면 5 개)를 64 비트 워드 하나로
        // 모아 바이트로 내보낸다(바이트마다 자르고 옮기던 반복이 없다). 와이어 바이트는 같다(`BitStreamWireTest`). 끝 바이트 위쪽과 새로 늘린 바이트는
        // 0 이라(값을 잘라 쓰고, `resize` 가 0 으로 늘린다) 워드에 끝 바이트만 합치면 된다.
        const int32 newBitCount = _bitCount + bitCount;
        _buffer.resize( static_cast<size_t>( ( newBitCount + 7 ) >> 3 ), 0 );
        const int32  bitOffset    = _bitCount & 7;
        const int32  touchedBytes = ( bitOffset + bitCount + 7 ) >> 3;
        uint8*       pBytes       = _buffer.data() + ( _bitCount >> 3 );
        const uint64 word         = static_cast<uint64>( pBytes[0] ) | ( static_cast<uint64>( value ) << bitOffset );
        for ( int32 byteIndex = 0; byteIndex < touchedBytes; ++byteIndex )
        {
            pBytes[byteIndex] = static_cast<uint8>( word >> ( byteIndex * 8 ) );
        }
        _bitCount = newBitCount;
    }

    bool BitWriter::writeInt( int32 value, int32 minValue, int32 maxValue )
    {
        const bool bInRange = minValue <= value && value <= maxValue;
        if ( bInRange == false )
            return false;
        writeBits( static_cast<uint32>( static_cast<int64>( value ) - minValue ), BitMath::computeBitsRequired( minValue, maxValue ) );
        return true;
    }

    void BitWriter::writeFloat( float32 value )
    {
        writeBits( BitUtil::floatToBits( value ), 32 );
    }

    bool BitWriter::writeQuantizedFloat( float32 value, float32 minValue, float32 maxValue, float32 resolution )
    {
        uint32 index{ 0 };
        if ( BitStreamInternal::tryComputeQuantizeIndex( value, minValue, maxValue, resolution, index ) == false )
            return false;
        const uint32 maxIndex = BitStreamInternal::computeQuantizeMaxIndex( minValue, maxValue, BitStreamInternal::computeQuantizeStep( resolution ) );
        writeBits( index, BitMath::computeBitsRequired( 0, maxIndex ) );
        return true;
    }

    bool BitWriter::writeQuantizedFloat3( const float3& value, const float3& minValue, const float3& maxValue, float32 resolution )
    {
        // 세 축을 먼저 다 본다. 한 축만 쓰고 멈추면 부른 쪽의 스트림이 어긋난다.
        const bool bAllInRange = canWriteQuantizedFloat( value._x, minValue._x, maxValue._x, resolution ) &&
                                 canWriteQuantizedFloat( value._y, minValue._y, maxValue._y, resolution ) &&
                                 canWriteQuantizedFloat( value._z, minValue._z, maxValue._z, resolution );
        if ( bAllInRange == false )
            return false;
        return writeQuantizedFloat( value._x, minValue._x, maxValue._x, resolution ) && writeQuantizedFloat( value._y, minValue._y, maxValue._y, resolution ) &&
               writeQuantizedFloat( value._z, minValue._z, maxValue._z, resolution );
    }

    bool BitWriter::canWriteQuantizedFloat( float32 value, float32 minValue, float32 maxValue, float32 resolution )
    {
        uint32 index{ 0 };
        return BitStreamInternal::tryComputeQuantizeIndex( value, minValue, maxValue, resolution, index );
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

    void BitWriter::writeVarInt( int64 value )
    {
        writeVarUint( ( static_cast<uint64>( value ) << 1 ) ^ static_cast<uint64>( value >> 63 ) );
    }

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
        {
            writeBits( pData[index], 8 );
        }
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
        _bFailed  = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // BitReader
    // ------------------------------------------------------------------------------
    BitReader::BitReader( const uint8* pData, int32 byteCount )
        : _pData{ pData }
        , _bitCapacity{ MathUtil::clamp( byteCount, 0, BitStreamInternal::kMaxByteCount ) * 8 }
        , _bitPosition{ 0 }
        , _bOverflow{ SW_FALSE }
    {
    }

    uint32 BitReader::readBits( int32 bitCount )
    {
        const bool bValidBitCount = 0 <= bitCount && bitCount <= BitMath::kMaxBitsPerCall;
        SW_ASSERT( bValidBitCount );
        if ( bValidBitCount == false )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽과 다른 비트 수를 읽으면 그 뒤가 모두 어긋난다 — 깨진 읽기로 멈춘다
            return 0;
        }
        if ( bitCount == 0 )
            return 0;
        if ( _bitPosition + bitCount > _bitCapacity )
        {
            _bOverflow   = SW_TRUE;
            _bitPosition = _bitCapacity;
            return 0;
        }
        // 닿는 바이트를 64 비트 워드 하나로 읽어 한 번에 자른다. 뒤에 8 바이트 넘게 남았으면 8 바이트를 통째로 읽는다(x64 · ARM64 는 리틀 엔디언이라
        // 바이트 순서가 비트 순서와 같다). 끝 근처에서는 닿는 바이트만 읽는다 — 버퍼 밖을 읽지 않는다.
        const int32  bitOffset     = _bitPosition & 7;
        const int32  byteIndex     = _bitPosition >> 3;
        const int32  touchedBytes  = ( bitOffset + bitCount + 7 ) >> 3;
        const int32  byteAvailable = ( _bitCapacity >> 3 ) - byteIndex;
        const uint8* pBytes        = _pData + byteIndex;
        uint64       word          = 0;
        if ( byteAvailable >= static_cast<int32>( sizeof( word ) ) )
        {
            std::memcpy( &word, pBytes, sizeof( word ) );
        }
        else
        {
            for ( int32 touched = 0; touched < touchedBytes; ++touched )
            {
                word |= static_cast<uint64>( pBytes[touched] ) << ( touched * 8 );
            }
        }
        _bitPosition += bitCount;
        const uint64 mask = ( static_cast<uint64>( 1 ) << bitCount ) - 1u;
        return static_cast<uint32>( ( word >> bitOffset ) & mask );
    }

    bool BitReader::readInt( int32 minValue, int32 maxValue, int32& outValue )
    {
        const uint32 raw = readBits( BitMath::computeBitsRequired( minValue, maxValue ) );
        if ( hasOverflowed() )
            return false;
        const int64 value = static_cast<int64>( minValue ) + raw;
        if ( value > maxValue )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 범위 밖을 쓰지 않으므로 깨진 데이터다
            return false;
        }
        outValue = static_cast<int32>( value );
        return true;
    }

    bool BitReader::readFiniteFloat( float32& outValue )
    {
        const float32 value = readFloatBits();
        if ( hasOverflowed() )
            return false;
        if ( MathUtil::isFinite( value ) == false )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 유한한 값만 보내므로 깨진(또는 꾸민) 데이터다
            return false;
        }
        outValue = value;
        return true;
    }

    float32 BitReader::readFloatBits()
    {
        return BitUtil::bitsToFloat( readBits( 32 ) );
    }

    bool BitReader::readQuantizedFloat( float32 minValue, float32 maxValue, float32 resolution, float32& outValue )
    {
        const float32 step     = BitStreamInternal::computeQuantizeStep( resolution );
        const uint32  maxIndex = BitStreamInternal::computeQuantizeMaxIndex( minValue, maxValue, step );
        const uint32  index    = readBits( BitMath::computeBitsRequired( 0, maxIndex ) );
        if ( hasOverflowed() )
            return false;
        if ( index > maxIndex )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 범위 밖의 단계를 쓰지 않으므로 깨진 데이터다
            return false;
        }
        // 마지막 단계는 간격이 범위를 나누어떨어지지 않을 때 최댓값을 조금 넘는다. 범위 안의 단계이므로 최댓값으로 둔다.
        outValue = MathUtil::min( maxValue, minValue + static_cast<float32>( index ) * step );
        return true;
    }

    bool BitReader::readQuantizedFloat3( const float3& minValue, const float3& maxValue, float32 resolution, float3& outValue )
    {
        float3     value;
        const bool bRead = readQuantizedFloat( minValue._x, maxValue._x, resolution, value._x ) && readQuantizedFloat( minValue._y, maxValue._y, resolution, value._y ) &&
                           readQuantizedFloat( minValue._z, maxValue._z, resolution, value._z );
        if ( bRead == false )
            return false;
        outValue = value;
        return true;
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

    uint32 BitReader::readVarUint32()
    {
        const uint64 value = readVarUint();
        if ( hasOverflowed() )
            return 0;
        if ( value > static_cast<uint64>( 0xFFFFFFFFu ) )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 uint32 만 쓰므로 깨진 데이터다 — 잘라 읽으면 다른 값이 된다
            return 0;
        }
        return static_cast<uint32>( value );
    }

    int32 BitReader::readVarUint31()
    {
        const uint64 value = readVarUint();
        if ( hasOverflowed() )
            return 0;
        if ( value > static_cast<uint64>( std::numeric_limits<int32>::max() ) )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 음수가 아닌 int32 만 쓰므로 깨진 데이터다 — 자르면 음수 번호가 된다
            return 0;
        }
        return static_cast<int32>( value );
    }

    int64 BitReader::readVarUint63()
    {
        const uint64 value = readVarUint();
        if ( hasOverflowed() )
            return 0;
        if ( value > static_cast<uint64>( std::numeric_limits<int64>::max() ) )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 음수가 아닌 int64 만 쓰므로 깨진 데이터다
            return 0;
        }
        return static_cast<int64>( value );
    }

    int64 BitReader::readVarInt()
    {
        const uint64 zigZag = readVarUint();
        return static_cast<int64>( zigZag >> 1 ) ^ -static_cast<int64>( zigZag & 1u );
    }

    int32 BitReader::readVarInt32()
    {
        const int64 value = readVarInt();
        if ( hasOverflowed() )
            return 0;
        const bool bInRange = std::numeric_limits<int32>::min() <= value && value <= std::numeric_limits<int32>::max();
        if ( bInRange == false )
        {
            _bOverflow = SW_TRUE; // 쓰는 쪽은 int32 만 쓰므로 깨진 데이터다
            return 0;
        }
        return static_cast<int32>( value );
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
        {
            pOutData[index] = static_cast<uint8>( readBits( 8 ) );
        }
        return true;
    }

    bool BitReader::readBlobSize( int32 maxSize, int32& outSize )
    {
        outSize                        = 0;
        const uint64 size              = readVarUint();
        const bool   bInvalidByteBlock = hasOverflowed() || size > static_cast<uint64>( MathUtil::max( 0, maxSize ) ) || hasBytes( static_cast<int32>( size ) ) == false;
        if ( bInvalidByteBlock )
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
            (void)readBits( 8 - remainder ); // 채움 비트는 값이 필요 없다 — 넘치면 readBits 가 _bOverflow 를 세운다
    }
} // namespace sw
