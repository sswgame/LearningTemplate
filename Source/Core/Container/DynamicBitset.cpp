#include "pch.h"

#include "Core/Container/DynamicBitset.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include <ostream>

namespace sw
{

    DynamicBitset::DynamicBitset( const uint32 size )
        : _listBlock{}
        , _bitCount{ size }
    {
        const uint32 blockCount = computeBlockCount( size );
        _listBlock.resize( blockCount, 0 );
    }

    bool DynamicBitset::parse( const string_view text, DynamicBitset& outValue )
    {
        if ( text.length() > std::numeric_limits<uint32>::max() )
            return false;

        // 글자마다 분기하지 않는다. 무작위 비트면 분기 예측이 절반을 틀려 65536 비트에서 비트당 3.7 ns 였다(1 ns 미만이 된다).
        // 블록 하나(64 글자)를 모아 쓰고, '0' · '1' 밖의 글자는 OR 로 모았다가 블록마다 한 번 본다.
        const uint32  bitCount = static_cast<uint32>( text.length() );
        DynamicBitset value( bitCount );
        const utf8*   pText      = text.data();
        BlockType*    pBlock     = value._listBlock.data();
        const uint32  blockCount = computeBlockCount( bitCount );
        for ( uint32 blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            const uint32 firstBit = blockIndex << kBlockShift;
            const uint32 bitEnd   = MathUtil::min( firstBit + kBitsPerBlock, bitCount );
            BlockType    block{ 0 };
            BlockType    invalid{ 0 };
            for ( uint32 bitIndex = firstBit; bitIndex < bitEnd; ++bitIndex )
            {
                const BlockType digit = static_cast<BlockType>( static_cast<uint8>( pText[bitCount - 1 - bitIndex] ) - static_cast<uint8>( '0' ) );
                invalid |= digit;
                block |= ( digit & 1 ) << ( bitIndex - firstBit );
            }
            if ( ( invalid & ~static_cast<BlockType>( 1 ) ) != 0 )
                return false;
            pBlock[blockIndex] = block;
        }
        outValue = std::move( value );
        return true;
    }

    DynamicBitset::DynamicBitset( const uint32 size, const uint64 value )
        : _listBlock{}
        , _bitCount{ size }
    {
        const uint32 blockCount = computeBlockCount( size );
        _listBlock.resize( blockCount, 0 );
        if ( blockCount > 0 )
            _listBlock[0] = value;
        sanitize();
    }

    void DynamicBitset::resize( const uint32 newSize, const bool value )
    {
        const uint32 oldBitCount   = _bitCount;
        const uint32 newBlockCount = computeBlockCount( newSize );

        _bitCount = newSize;
        _listBlock.resize( newBlockCount, value ? ~static_cast<BlockType>( 0 ) : 0 );

        // 새 블록은 위에서 채웠다. 옛 마지막 블록의 남는 비트(불변식으로 0)만 켜고, 새 크기 밖은 sanitize 가 지운다.
        const uint32 oldRestBits = oldBitCount & kBlockMask;
        const bool   bGrowOn     = value && newSize > oldBitCount;
        if ( bGrowOn && oldRestBits != 0 )
            _listBlock[getBlockIndex( oldBitCount )] |= ~( ( static_cast<BlockType>( 1 ) << oldRestBits ) - 1 );

        sanitize();
    }

    void DynamicBitset::push_back( const bool value )
    {
        if ( _bitCount == std::numeric_limits<uint32>::max() )
        {
            SW_LOW_LEVEL_ASSERT( false, "Bitset size overflow" );
            return;
        }

        const uint32 bitPosition = _bitCount;
        if ( getBlockIndex( bitPosition ) == _listBlock.size() )
            _listBlock.push_back( 0 );
        ++_bitCount;
        if ( value )
            _listBlock[getBlockIndex( bitPosition )] |= bitMask( bitPosition );
    }

    void DynamicBitset::reportOutOfRange( [[maybe_unused]] const uint32 bitPosition, [[maybe_unused]] const uint32 bitCount )
    {
        SW_LOW_LEVEL_ASSERT( false, "Bit position out of range (%u of %u)", bitPosition, bitCount );
    }

    DynamicBitset& DynamicBitset::set()
    {
        std::fill( _listBlock.begin(), _listBlock.end(), ~static_cast<BlockType>( 0 ) );
        sanitize();
        return *this;
    }

    DynamicBitset& DynamicBitset::reset()
    {
        std::fill( _listBlock.begin(), _listBlock.end(), 0 );
        return *this;
    }

    DynamicBitset& DynamicBitset::flip()
    {
        for ( BlockType& block : _listBlock )
        {
            block = ~block;
        }
        sanitize();
        return *this;
    }

    bool DynamicBitset::all() const
    {
        if ( _bitCount == 0 )
            return true;

        const uint32 fullBlockCount = _bitCount >> kBlockShift;

        for ( uint32 blockIndex = 0; blockIndex < fullBlockCount; ++blockIndex )
        {
            if ( _listBlock[blockIndex] != ~static_cast<BlockType>( 0 ) )
                return false;
        }

        const uint32 restBits = _bitCount & kBlockMask;
        if ( restBits != 0 )
        {
            const BlockType mask = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
            if ( ( _listBlock[fullBlockCount] & mask ) != mask )
                return false;
        }

        return true;
    }

    bool DynamicBitset::any() const
    {
        return VectorUtil::containsIf( _listBlock, [&]( const BlockType& block )
        { return block != 0; } );
    }

    uint32 DynamicBitset::count() const
    {
        uint32 result{ 0 };
        for ( const BlockType& block : _listBlock )
        {
#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_X64 )
            result += static_cast<uint32>( __popcnt64( block ) ); // MSVC 확장 intrinsic(cl · clang-cl), x64 전용
#elif defined( SW_COMPILER_CLANG ) || defined( SW_COMPILER_GCC )
            result += static_cast<uint32>( __builtin_popcountll( block ) );
#else
            BlockType targetBlock = block;
            while ( targetBlock )
            {
                ++result;
                targetBlock &= targetBlock - 1;
            }
#endif
        }
        return result;
    }

    string DynamicBitset::to_string() const
    {
        // 글자를 하나씩 덧붙이지 않고 크기를 잡은 뒤 블록에서 바로 쓴다(맨 왼쪽이 가장 높은 비트).
        string           result( _bitCount, '0' );
        utf8*            pText  = result.data();
        const BlockType* pBlock = _listBlock.data();
        for ( uint32 bitIndex = 0; bitIndex < _bitCount; ++bitIndex )
        {
            const BlockType bit             = ( pBlock[bitIndex >> kBlockShift] >> ( bitIndex & kBlockMask ) ) & 1;
            pText[_bitCount - 1 - bitIndex] = static_cast<utf8>( '0' + bit );
        }
        return result;
    }

    uint64 DynamicBitset::to_ullong() const
    {
        SW_LOW_LEVEL_ASSERT( _bitCount <= 64, "Bitset too large for uint64" );

        if ( _listBlock.empty() )
            return 0;

        return _listBlock[0];
    }

    uint32 DynamicBitset::to_ulong() const
    {
        SW_LOW_LEVEL_ASSERT( _bitCount <= sizeof( uint32 ) * 8, "Bitset too large for uint32" );
        return static_cast<uint32>( to_ullong() );
    }

    uint32 DynamicBitset::memory_usage() const
    {
        return sizeof( *this ) + static_cast<uint32>( _listBlock.size() ) * sizeof( BlockType );
    }

    DynamicBitset DynamicBitset::operator~() const
    {
        DynamicBitset result( *this );
        result.flip();
        return result;
    }

    DynamicBitset& DynamicBitset::operator&=( const DynamicBitset& other )
    {
        // 크기가 다르면 아무것도 하지 않는다. assert 는 Debug 에서만 멈추므로, 그 밖의 빌드에서 아래 루프가 돌면 짧은 쪽
        // `_listBlock` 의 범위 밖을 읽는다. 계약 위반이 메모리 오류로 번지는 것이다.
        if ( _bitCount != other._bitCount )
        {
            SW_LOW_LEVEL_ASSERT( false, "Bitset sizes must match (%u vs %u)", _bitCount, other._bitCount );
            return *this;
        }

        BlockType*       pBlock      = _listBlock.data();
        const BlockType* pOtherBlock = other._listBlock.data();
        const size_t     blockCount  = _listBlock.size();
        for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            pBlock[blockIndex] &= pOtherBlock[blockIndex];
        }
        return *this;
    }

    DynamicBitset& DynamicBitset::operator|=( const DynamicBitset& other )
    {
        // 크기가 다르면 아무것도 하지 않는다. assert 는 Debug 에서만 멈추므로, 그 밖의 빌드에서 아래 루프가 돌면 짧은 쪽
        // `_listBlock` 의 범위 밖을 읽는다. 계약 위반이 메모리 오류로 번지는 것이다.
        if ( _bitCount != other._bitCount )
        {
            SW_LOW_LEVEL_ASSERT( false, "Bitset sizes must match (%u vs %u)", _bitCount, other._bitCount );
            return *this;
        }

        BlockType*       pBlock      = _listBlock.data();
        const BlockType* pOtherBlock = other._listBlock.data();
        const size_t     blockCount  = _listBlock.size();
        for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            pBlock[blockIndex] |= pOtherBlock[blockIndex];
        }
        return *this;
    }

    DynamicBitset& DynamicBitset::operator^=( const DynamicBitset& other )
    {
        // 크기가 다르면 아무것도 하지 않는다. assert 는 Debug 에서만 멈추므로, 그 밖의 빌드에서 아래 루프가 돌면 짧은 쪽
        // `_listBlock` 의 범위 밖을 읽는다. 계약 위반이 메모리 오류로 번지는 것이다.
        if ( _bitCount != other._bitCount )
        {
            SW_LOW_LEVEL_ASSERT( false, "Bitset sizes must match (%u vs %u)", _bitCount, other._bitCount );
            return *this;
        }

        BlockType*       pBlock      = _listBlock.data();
        const BlockType* pOtherBlock = other._listBlock.data();
        const size_t     blockCount  = _listBlock.size();
        for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            pBlock[blockIndex] ^= pOtherBlock[blockIndex];
        }
        return *this;
    }

    DynamicBitset DynamicBitset::operator&( const DynamicBitset& other ) const
    {
        DynamicBitset result( *this );
        result &= other;
        return result;
    }

    DynamicBitset DynamicBitset::operator|( const DynamicBitset& other ) const
    {
        DynamicBitset result( *this );
        result |= other;
        return result;
    }

    DynamicBitset DynamicBitset::operator^( const DynamicBitset& other ) const
    {
        DynamicBitset result( *this );
        result ^= other;
        return result;
    }

    DynamicBitset& DynamicBitset::operator<<=( const uint32 shift )
    {
        if ( shift >= _bitCount || _bitCount == 0 )
        {
            reset();
            return *this;
        }

        if ( shift == 0 )
            return *this;

        const uint32 blockShift = shift >> kBlockShift;
        const uint32 bitShift   = shift & kBlockMask;

        // shift < _bitCount 이므로 blockShift < blockCount 다.
        BlockType*   pBlock     = _listBlock.data();
        const size_t blockCount = _listBlock.size();
        if ( blockShift > 0 )
        {
            std::memmove( pBlock + blockShift, pBlock, ( blockCount - blockShift ) * sizeof( BlockType ) );
            std::fill( pBlock, pBlock + blockShift, static_cast<BlockType>( 0 ) );
        }

        // 위 블록부터 내려가며 아래 블록의 넘친 비트를 받는다 — 한 번 읽은 값만 쓰므로 루프 사이에 의존이 없다.
        if ( bitShift > 0 )
        {
            for ( size_t blockIndex = blockCount - 1; blockIndex > blockShift; --blockIndex )
            {
                pBlock[blockIndex] = ( pBlock[blockIndex] << bitShift ) | ( pBlock[blockIndex - 1] >> ( kBitsPerBlock - bitShift ) );
            }
            pBlock[blockShift] <<= bitShift;
        }

        sanitize();
        return *this;
    }

    DynamicBitset& DynamicBitset::operator>>=( const uint32 shift )
    {
        if ( shift >= _bitCount || _bitCount == 0 )
        {
            reset();
            return *this;
        }

        if ( shift == 0 )
            return *this;

        const uint32 blockShift = shift >> kBlockShift;
        const uint32 bitShift   = shift & kBlockMask;

        // shift < _bitCount 이므로 blockShift < blockCount 다.
        BlockType*   pBlock     = _listBlock.data();
        const size_t blockCount = _listBlock.size();
        const size_t keptCount  = blockCount - blockShift;
        if ( blockShift > 0 )
        {
            std::memmove( pBlock, pBlock + blockShift, keptCount * sizeof( BlockType ) );
            std::fill( pBlock + keptCount, pBlock + blockCount, static_cast<BlockType>( 0 ) );
        }

        // 아래 블록부터 올라가며 위 블록의 넘친 비트를 받는다 — 한 번 읽은 값만 쓰므로 루프 사이에 의존이 없다.
        if ( bitShift > 0 )
        {
            for ( size_t blockIndex = 0; blockIndex + 1 < keptCount; ++blockIndex )
            {
                pBlock[blockIndex] = ( pBlock[blockIndex] >> bitShift ) | ( pBlock[blockIndex + 1] << ( kBitsPerBlock - bitShift ) );
            }
            pBlock[keptCount - 1] >>= bitShift;
        }

        sanitize();
        return *this;
    }

    DynamicBitset DynamicBitset::operator<<( const uint32 shift ) const
    {
        DynamicBitset result( *this );
        result <<= shift;
        return result;
    }

    DynamicBitset DynamicBitset::operator>>( const uint32 shift ) const
    {
        DynamicBitset result( *this );
        result >>= shift;
        return result;
    }

    bool DynamicBitset::operator==( const DynamicBitset& other ) const
    {
        if ( _bitCount != other._bitCount )
            return false;
        // 크기가 같으면 블록 수도 같다. 처음 다른 블록에서 멈추지 않고 차이를 OR 로 모은다 — 벡터화되어 65536 비트에서 원소 비교
        // 루프보다 4 배 넘게 빠르고, memcmp 호출과 달리 블록 하나짜리에서도 호출 비용이 없다.
        const BlockType* pBlock      = _listBlock.data();
        const BlockType* pOtherBlock = other._listBlock.data();
        const size_t     blockCount  = _listBlock.size();
        BlockType        difference{ 0 };
        for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            difference |= pBlock[blockIndex] ^ pOtherBlock[blockIndex];
        }
        return difference == 0;
    }

    bool DynamicBitset::operator<( const DynamicBitset& other ) const
    {
        if ( _bitCount != other._bitCount )
            return _bitCount < other._bitCount;

        for ( uint32 blockIndex = static_cast<uint32>( _listBlock.size() ); blockIndex > 0; --blockIndex )
        {
            if ( _listBlock[blockIndex - 1] != other._listBlock[blockIndex - 1] )
                return _listBlock[blockIndex - 1] < other._listBlock[blockIndex - 1];
        }
        return false;
    }

    std::ostream& operator<<( std::ostream& os, const DynamicBitset& bitset )
    {
        os << bitset.to_string();
        return os;
    }

    void DynamicBitset::sanitize()
    {
        if ( _bitCount == 0 || _listBlock.empty() )
            return;

        const uint32 restBits = _bitCount & kBlockMask;
        if ( restBits != 0 )
        {
            const uint32    lastBlockIndex = static_cast<uint32>( _listBlock.size() - 1 );
            const BlockType mask           = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
            _listBlock[lastBlockIndex] &= mask;
        }
    }
} // namespace sw
