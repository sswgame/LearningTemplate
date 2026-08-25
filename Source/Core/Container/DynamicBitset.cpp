#include "pch.h"

#include "Core/Container/DynamicBitset.h"

namespace sw
{

	DynamicBitset::DynamicBitset( const uint32 size )
		: _bitCount{ size }
		, _listBlocks{}
	{
		const uint32 blockCount = calculateBlockCount( size );
		_listBlocks.resize( blockCount, 0 );
	}

	DynamicBitset::DynamicBitset( string_view str )
		: _bitCount{ static_cast<uint32>( str.length() ) }
		, _listBlocks{}
	{
		const uint32 blockCount = calculateBlockCount( _bitCount );
		_listBlocks.resize( blockCount, 0 );

		for ( uint32 bitIndex = 0; bitIndex < _bitCount; ++bitIndex )
		{
			const utf8 c = str[_bitCount - 1 - bitIndex];
			if ( c == '1' )
				_listBlocks[bitIndex / kBitsPerBlock] |= ( 1ULL << ( bitIndex % kBitsPerBlock ) );
			else if ( c != '0' )
				SW_LOG_ERROR( "DynamicBitset: Invalid character %# in bitset string", c );
		}
	}

	DynamicBitset::DynamicBitset( const uint32 size, const uint64 value )
		: _bitCount{ size }
		, _listBlocks{}
	{
		const uint32 blockCount = calculateBlockCount( size );
		_listBlocks.resize( blockCount, 0 );
		if ( blockCount > 0 )
			_listBlocks[0] = value;
		sanitize();
	}

	void DynamicBitset::resize( const uint32 newSize, const bool value )
	{
		const uint32 oldBitCount   = _bitCount;
		const uint32 newBlockCount = calculateBlockCount( newSize );

		_bitCount = newSize;
		_listBlocks.resize( newBlockCount, value ? ~static_cast<BlockType>( 0 ) : 0 );

		if ( newSize > oldBitCount && value )
		{
			for ( uint32 bitIndex = oldBitCount; bitIndex < newSize; ++bitIndex )
			{
				set( bitIndex, true );
			}
		}

		sanitize();
	}

	bool DynamicBitset::operator[]( const uint32 pos ) const
	{
		SW_LOG_ASSERT( pos < _bitCount, "Bit position out of range" );
		const uint32 blockIndex = getBlockIndex( pos );
		return ( _listBlocks[blockIndex] & bitMask( pos ) ) != 0;
	}

	DynamicBitset& DynamicBitset::set()
	{
		std::fill( _listBlocks.begin(), _listBlocks.end(), ~static_cast<BlockType>( 0 ) );
		sanitize();
		return *this;
	}

	DynamicBitset& DynamicBitset::set( const uint32 pos, const bool value )
	{
		SW_LOG_ASSERT( pos < _bitCount, "Bit position out of range" );

		const uint32	blockIndex = getBlockIndex( pos );
		const BlockType mask	   = bitMask( pos );

		if ( value )
			_listBlocks[blockIndex] |= mask;
		else
			_listBlocks[blockIndex] &= ~mask;

		return *this;
	}

	DynamicBitset& DynamicBitset::reset()
	{
		std::fill( _listBlocks.begin(), _listBlocks.end(), 0 );
		return *this;
	}

	DynamicBitset& DynamicBitset::flip()
	{
		for ( BlockType& block : _listBlocks )
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
		_listBlocks[blockIndex] ^= bitMask( pos );
		return *this;
	}

	bool DynamicBitset::all() const
	{
		if ( _bitCount == 0 )
			return true;

		const uint32 fullBlockCount = _bitCount >> kBlockShift;

		for ( uint32 blockIndex = 0; blockIndex < fullBlockCount; ++blockIndex )
		{
			if ( _listBlocks[blockIndex] != ~static_cast<BlockType>( 0 ) )
				return false;
		}

		const uint32 restBits = _bitCount & kBlockMask;
		if ( restBits != 0 )
		{
			const BlockType mask = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
			if ( ( _listBlocks[fullBlockCount] & mask ) != mask )
				return false;
		}

		return true;
	}

	bool DynamicBitset::any() const
	{
		for ( const BlockType& block : _listBlocks )
		{
			if ( block != 0 )
				return true;
		}
		return false;
	}

	uint32 DynamicBitset::count() const
	{
		uint32 result{ 0 };
		for ( const BlockType& block : _listBlocks )
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

	string DynamicBitset::to_string() const
	{
		string result{};
		result.reserve( _bitCount );
		for ( uint32 bitIndex = _bitCount; bitIndex > 0; --bitIndex )
		{
			result += ( test( bitIndex - 1 ) ? '1' : '0' );
		}
		return result;
	}

	uint64 DynamicBitset::to_ullong() const
	{
		SW_LOG_ASSERT( _bitCount <= 64, "Bitset too large for uint64" );

		if ( _listBlocks.empty() )
			return 0;

		return _listBlocks[0];
	}

	uint32 DynamicBitset::to_ulong() const
	{
		SW_LOG_ASSERT( _bitCount <= sizeof( uint32 ) * 8, "Bitset too large for uint32" );
		return static_cast<uint32>( to_ullong() );
	}

	uint32 DynamicBitset::memory_usage() const
	{
		return sizeof( *this ) + static_cast<uint32>( _listBlocks.size() ) * sizeof( BlockType );
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
		for ( uint32 blockIndex = 0; blockIndex < _listBlocks.size(); ++blockIndex )
		{
			_listBlocks[blockIndex] &= other._listBlocks[blockIndex];
		}
		return *this;
	}

	DynamicBitset& DynamicBitset::operator|=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 blockIndex = 0; blockIndex < _listBlocks.size(); ++blockIndex )
		{
			_listBlocks[blockIndex] |= other._listBlocks[blockIndex];
		}
		return *this;
	}

	DynamicBitset& DynamicBitset::operator^=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 blockIndex = 0; blockIndex < _listBlocks.size(); ++blockIndex )
		{
			_listBlocks[blockIndex] ^= other._listBlocks[blockIndex];
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
			const size_t size	= _listBlocks.size();
			const size_t bShift = blockShift;
			for ( size_t blockIndex = size; blockIndex > bShift; --blockIndex )
			{
				_listBlocks[blockIndex - 1] = _listBlocks[blockIndex - 1 - bShift];
			}
			for ( uint32 blockIndex = 0; blockIndex < blockShift && blockIndex < _listBlocks.size(); ++blockIndex )
			{
				_listBlocks[blockIndex] = 0;
			}
		}

		if ( bitShift > 0 )
		{
			BlockType carry{ 0 };
			for ( uint32 blockIndex = 0; blockIndex < _listBlocks.size(); ++blockIndex )
			{
				const BlockType newCarry = _listBlocks[blockIndex] >> ( kBitsPerBlock - bitShift );
				_listBlocks[blockIndex]	 = ( _listBlocks[blockIndex] << bitShift ) | carry;
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
			const size_t size	= _listBlocks.size();
			const size_t bShift = blockShift;
			for ( size_t blockIndex = 0; blockIndex + bShift < size; ++blockIndex )
			{
				_listBlocks[blockIndex] = _listBlocks[blockIndex + bShift];
			}
			for ( size_t blockIndex = ( size > bShift ? size - bShift : 0 ); blockIndex < size; ++blockIndex )
			{
				_listBlocks[blockIndex] = 0;
			}
		}

		if ( bitShift > 0 )
		{
			BlockType	 carry{ 0 };
			const size_t size = _listBlocks.size();
			for ( size_t blockIndex = size; blockIndex > 0; --blockIndex )
			{
				const size_t	uIndex	 = blockIndex - 1;
				const BlockType newCarry = _listBlocks[uIndex] << ( kBitsPerBlock - bitShift );
				_listBlocks[uIndex]		 = ( _listBlocks[uIndex] >> bitShift ) | carry;
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
		return _listBlocks == other._listBlocks;
	}

	bool DynamicBitset::operator<( const DynamicBitset& other ) const
	{
		if ( _bitCount != other._bitCount )
			return _bitCount < other._bitCount;

		for ( uint32 blockIndex = static_cast<uint32>( _listBlocks.size() ); blockIndex > 0; --blockIndex )
		{
			if ( _listBlocks[blockIndex - 1] != other._listBlocks[blockIndex - 1] )
				return _listBlocks[blockIndex - 1] < other._listBlocks[blockIndex - 1];
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
		if ( _bitCount == 0 || _listBlocks.empty() )
			return;

		const uint32 restBits = _bitCount & kBlockMask;
		if ( restBits != 0 )
		{
			const uint32	lastBlockIndex = static_cast<uint32>( _listBlocks.size() - 1 );
			const BlockType mask		   = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
			_listBlocks[lastBlockIndex] &= mask;
		}
	}
} // namespace sw
