/**
 * @file DynamicBitset.cpp
 * @brief DynamicBitset 구현
 */
#include "pch.h"
#include "DynamicBitset.h"
#include "Core/Utility/Log/Logger.h"

#if defined( _MSC_VER )
	#include <intrin.h>
#endif

namespace sw
{

	DynamicBitset::DynamicBitset( const uint32 size )
		: _bitCount{ size }
	{
		const uint32 blockCount = calculateBlockCount( size );
		_blockList.resize( blockCount, 0 );
	}

	DynamicBitset::DynamicBitset( const std::string& str )
		: _bitCount{ static_cast<uint32>( str.length() ) }
	{
		const uint32 blockCount = calculateBlockCount( _bitCount );
		_blockList.resize( blockCount, 0 );

		for ( uint32 index = 0; index < _bitCount; ++index )
		{
			const utf8 c = str[_bitCount - 1 - index];
			if ( c == '1' )
			{
				_blockList[index / kBitsPerBlock] |= ( 1ULL << ( index % kBitsPerBlock ) );
			}
			else if ( c != '0' )
			{
				SW_LOG_ERROR( "DynamicBitset: Invalid character %# in bitset string", c );
			}
		}
	}

	DynamicBitset::DynamicBitset( const uint32 size, const uint64 value )
		: _bitCount{ size }
	{
		const uint32 blockCount = calculateBlockCount( size );
		_blockList.resize( blockCount, 0 );
		if ( blockCount > 0 )
		{
			_blockList[0] = value;
		}
		sanitize();
	}

	void DynamicBitset::resize( const uint32 newSize, const bool value )
	{
		const uint32 oldBitCount   = _bitCount;
		const uint32 newBlockCount = calculateBlockCount( newSize );

		_bitCount = newSize;
		_blockList.resize( newBlockCount, value ? ~static_cast<BlockType>( 0 ) : 0 );

		if ( newSize > oldBitCount && value )
		{

			for ( uint32 index = oldBitCount; index < newSize; ++index )
			{
				set( index, true );
			}
		}

		sanitize();
	}

	bool DynamicBitset::operator[]( const uint32 pos ) const
	{
		SW_LOG_ASSERT( pos < _bitCount, "Bit position out of range" );
		const uint32 blockIndex = getBlockIndex( pos );
		return ( _blockList[blockIndex] & bitMask( pos ) ) != 0;
	}

	DynamicBitset& DynamicBitset::set()
	{
		std::fill( _blockList.begin(), _blockList.end(), ~static_cast<BlockType>( 0 ) );
		sanitize();
		return *this;
	}

	DynamicBitset& DynamicBitset::set( const uint32 pos, const bool value )
	{
		SW_LOG_ASSERT( pos < _bitCount, "Bit position out of range" );

		const uint32	blockIndex = getBlockIndex( pos );
		const BlockType mask	   = bitMask( pos );

		if ( value )
			_blockList[blockIndex] |= mask;
		else
			_blockList[blockIndex] &= ~mask;

		return *this;
	}

	DynamicBitset& DynamicBitset::reset()
	{
		std::fill( _blockList.begin(), _blockList.end(), 0 );
		return *this;
	}

	DynamicBitset& DynamicBitset::flip()
	{
		for ( BlockType& block : _blockList )
		{
			block = ~block;
		}
		sanitize();
		return *this;
	}

	DynamicBitset& DynamicBitset::flip( const uint32 pos )
	{
		SW_LOG_ASSERT( pos < _bitCount, "Bit position out of range" );

		const uint32 blockIndex = getBlockIndex( pos );
		_blockList[blockIndex] ^= bitMask( pos );
		return *this;
	}

	bool DynamicBitset::all() const
	{
		if ( _bitCount == 0 )
			return true;

		const uint32 fullBlockCount = _bitCount >> kBlockShift;

		for ( uint32 index = 0; index < fullBlockCount; ++index )
		{
			if ( _blockList[index] != ~static_cast<BlockType>( 0 ) )
				return false;
		}

		const uint32 restBits = _bitCount & kBlockMask;
		if ( restBits != 0 )
		{
			const BlockType mask = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
			if ( ( _blockList[fullBlockCount] & mask ) != mask )
				return false;
		}

		return true;
	}

	bool DynamicBitset::any() const
	{
		for ( const BlockType& block : _blockList )
		{
			if ( block != 0 )
				return true;
		}
		return false;
	}

	uint32 DynamicBitset::count() const
	{
		uint32 result = 0;
		for ( const BlockType& block : _blockList )
		{
#if defined( _MSC_VER ) && ( defined( _M_X64 ) || defined( _M_AMD64 ) )
			result += static_cast<uint32>( __popcnt64( block ) );
#elif defined( __GNUC__ ) || defined( __clang__ )
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

	DynamicBitset DynamicBitset::operator~() const
	{
		DynamicBitset result( *this );
		result.flip();
		return result;
	}

	DynamicBitset& DynamicBitset::operator&=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 index = 0; index < _blockList.size(); ++index )
		{
			_blockList[index] &= other._blockList[index];
		}
		return *this;
	}

	DynamicBitset& DynamicBitset::operator|=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 index = 0; index < _blockList.size(); ++index )
		{
			_blockList[index] |= other._blockList[index];
		}
		return *this;
	}

	DynamicBitset& DynamicBitset::operator^=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 index = 0; index < _blockList.size(); ++index )
		{
			_blockList[index] ^= other._blockList[index];
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
		const uint32 bitShift	= shift & kBlockMask;

		if ( blockShift > 0 )
		{
			const size_t size	= _blockList.size();
			const size_t bShift = static_cast<size_t>( blockShift );
			for ( size_t index = size; index > bShift; --index )
			{
				_blockList[index - 1] = _blockList[index - 1 - bShift];
			}
			for ( uint32 index = 0; index < blockShift && index < _blockList.size(); ++index )
			{
				_blockList[index] = 0;
			}
		}

		if ( bitShift > 0 )
		{
			BlockType carry = 0;
			for ( uint32 index = 0; index < _blockList.size(); ++index )
			{
				const BlockType newCarry = _blockList[index] >> ( kBitsPerBlock - bitShift );
				_blockList[index]		 = ( _blockList[index] << bitShift ) | carry;
				carry					 = newCarry;
			}
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
		const uint32 bitShift	= shift & kBlockMask;

		if ( blockShift > 0 )
		{
			const size_t size	= _blockList.size();
			const size_t bShift = static_cast<size_t>( blockShift );
			for ( size_t index = size; index > bShift; --index )
			{
				_blockList[index - 1] = _blockList[index - 1 - bShift];
			}
			for ( uint32 index = 0; index < blockShift && index < _blockList.size(); ++index )
			{
				_blockList[index] = 0;
			}
		}

		if ( bitShift > 0 )
		{

			BlockType	 carry = 0;
			const size_t size  = _blockList.size();
			for ( size_t index = size; index > 0; --index )
			{
				const size_t	uIndex	 = index - 1;
				const BlockType newCarry = _blockList[uIndex] << ( kBitsPerBlock - bitShift );
				_blockList[uIndex]		 = ( _blockList[uIndex] >> bitShift ) | carry;
				carry					 = newCarry;
			}
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
		return _blockList == other._blockList;
	}

	bool DynamicBitset::operator<( const DynamicBitset& other ) const
	{
		if ( _bitCount != other._bitCount )
			return _bitCount < other._bitCount;

		for ( uint32 index = static_cast<uint32>( _blockList.size() ); index > 0; --index )
		{
			if ( _blockList[index - 1] != other._blockList[index - 1] )
				return _blockList[index - 1] < other._blockList[index - 1];
		}
		return false;
	}

	std::string DynamicBitset::to_string() const
	{
		std::string result{};
		result.reserve( _bitCount );
		for ( uint32 index = _bitCount; index > 0; --index )
		{
			result += ( test( index - 1 ) ? '1' : '0' );
		}
		return result;
	}

	uint64 DynamicBitset::to_ullong() const
	{
		SW_LOG_ASSERT( _bitCount <= 64, "Bitset too large for uint64" );

		if ( _blockList.empty() )
			return 0;

		return _blockList[0];
	}

	uint32 DynamicBitset::to_ulong() const
	{
		SW_LOG_ASSERT( _bitCount <= sizeof( uint32 ) * 8, "Bitset too large for uint32" );
		return static_cast<uint32>( to_ullong() );
	}

	uint32 DynamicBitset::memory_usage() const
	{
		return sizeof( *this ) + static_cast<uint32>( _blockList.size() ) * sizeof( BlockType );
	}

	void DynamicBitset::sanitize()
	{
		if ( _bitCount == 0 || _blockList.empty() )
			return;

		const uint32 restBits = _bitCount & kBlockMask;
		if ( restBits != 0 )
		{
			const uint32	lastBlockIndex = static_cast<uint32>( _blockList.size() - 1 );
			const BlockType mask		   = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
			_blockList[lastBlockIndex] &= mask;
		}
	}

	std::ostream& operator<<( std::ostream& os, const DynamicBitset& bitset )
	{
		os << bitset.to_string();
		return os;
	}
} // namespace sw
