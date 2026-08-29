#include "pch.h"

#include "Core/Container/DynamicBitset.h"

SW_LOG_CALLER( "DynamicBitset" );
namespace sw
{

	DynamicBitset::DynamicBitset( const uint32 size )
		: _listBlock{}
		, _bitCount{ size }
	{
		const uint32 blockCount = calculateBlockCount( size );
		_listBlock.resize( blockCount, 0 );
	}

	DynamicBitset::DynamicBitset( string_view str )
		: _listBlock{}
		, _bitCount{ static_cast<uint32>( str.length() ) }
	{
		const uint32 blockCount = calculateBlockCount( _bitCount );
		_listBlock.resize( blockCount, 0 );

		for ( uint32 bitIndex = 0; bitIndex < _bitCount; ++bitIndex )
		{
			const utf8 c = str[_bitCount - 1 - bitIndex];
			if ( c == '1' )
				_listBlock[bitIndex / kBitsPerBlock] |= ( 1ULL << ( bitIndex % kBitsPerBlock ) );
			else if ( c != '0' )
				SW_LOG_ERROR( "DynamicBitset: Invalid character %# in bitset string", c );
		}
	}

	DynamicBitset::DynamicBitset( const uint32 size, const uint64 value )
		: _listBlock{}
		, _bitCount{ size }
	{
		const uint32 blockCount = calculateBlockCount( size );
		_listBlock.resize( blockCount, 0 );
		if ( blockCount > 0 )
			_listBlock[0] = value;
		sanitize();
	}

	void DynamicBitset::resize( const uint32 newSize, const bool value )
	{
		const uint32 oldBitCount   = _bitCount;
		const uint32 newBlockCount = calculateBlockCount( newSize );

		_bitCount = newSize;
		_listBlock.resize( newBlockCount, value ? ~static_cast<BlockType>( 0 ) : 0 );

		if ( newSize > oldBitCount && value )
		{
			for ( uint32 bitIndex = oldBitCount; bitIndex < newSize; ++bitIndex )
			{
				set( bitIndex, true );
			}
		}

		sanitize();
	}

	bool DynamicBitset::operator[]( const uint32 bitPosition ) const
	{
		SW_LOG_ASSERT( bitPosition < _bitCount, "Bit position out of range" );
		const uint32 blockIndex = getBlockIndex( bitPosition );
		return ( _listBlock[blockIndex] & bitMask( bitPosition ) ) != 0;
	}

	DynamicBitset& DynamicBitset::set()
	{
		std::fill( _listBlock.begin(), _listBlock.end(), ~static_cast<BlockType>( 0 ) );
		sanitize();
		return *this;
	}

	DynamicBitset& DynamicBitset::set( const uint32 bitPosition, const bool value )
	{
		SW_LOG_ASSERT( bitPosition < _bitCount, "Bit position out of range" );

		const uint32	blockIndex = getBlockIndex( bitPosition );
		const BlockType mask	   = bitMask( bitPosition );

		if ( value )
			_listBlock[blockIndex] |= mask;
		else
			_listBlock[blockIndex] &= ~mask;

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

	DynamicBitset& DynamicBitset::flip( const uint32 bitPosition )
	{
		SW_LOG_ASSERT( bitPosition < _bitCount, "Bit position out of range" );

		const uint32 blockIndex = getBlockIndex( bitPosition );
		_listBlock[blockIndex] ^= bitMask( bitPosition );
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
		for ( const BlockType& block : _listBlock )
		{
			if ( block != 0 )
				return true;
		}
		return false;
	}

	uint32 DynamicBitset::count() const
	{
		uint32 result{ 0 };
		for ( const BlockType& block : _listBlock )
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

		if ( _listBlock.empty() )
			return 0;

		return _listBlock[0];
	}

	uint32 DynamicBitset::to_ulong() const
	{
		SW_LOG_ASSERT( _bitCount <= sizeof( uint32 ) * 8, "Bitset too large for uint32" );
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
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 blockIndex = 0; blockIndex < _listBlock.size(); ++blockIndex )
		{
			_listBlock[blockIndex] &= other._listBlock[blockIndex];
		}
		return *this;
	}

	DynamicBitset& DynamicBitset::operator|=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 blockIndex = 0; blockIndex < _listBlock.size(); ++blockIndex )
		{
			_listBlock[blockIndex] |= other._listBlock[blockIndex];
		}
		return *this;
	}

	DynamicBitset& DynamicBitset::operator^=( const DynamicBitset& other )
	{
		SW_LOG_ASSERT( _bitCount == other._bitCount, "Bitset sizes must match" );
		for ( uint32 blockIndex = 0; blockIndex < _listBlock.size(); ++blockIndex )
		{
			_listBlock[blockIndex] ^= other._listBlock[blockIndex];
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
			const size_t size	= _listBlock.size();
			const size_t bShift = blockShift;
			for ( size_t blockIndex = size; blockIndex > bShift; --blockIndex )
			{
				_listBlock[blockIndex - 1] = _listBlock[blockIndex - 1 - bShift];
			}
			for ( uint32 blockIndex = 0; blockIndex < blockShift && blockIndex < _listBlock.size(); ++blockIndex )
			{
				_listBlock[blockIndex] = 0;
			}
		}

		if ( bitShift > 0 )
		{
			BlockType carry{ 0 };
			for ( uint32 blockIndex = 0; blockIndex < _listBlock.size(); ++blockIndex )
			{
				const BlockType newCarry = _listBlock[blockIndex] >> ( kBitsPerBlock - bitShift );
				_listBlock[blockIndex]	 = ( _listBlock[blockIndex] << bitShift ) | carry;
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
			const size_t size	= _listBlock.size();
			const size_t bShift = blockShift;
			for ( size_t blockIndex = 0; blockIndex + bShift < size; ++blockIndex )
			{
				_listBlock[blockIndex] = _listBlock[blockIndex + bShift];
			}
			for ( size_t blockIndex = ( size > bShift ? size - bShift : 0 ); blockIndex < size; ++blockIndex )
			{
				_listBlock[blockIndex] = 0;
			}
		}

		if ( bitShift > 0 )
		{
			BlockType	 carry{ 0 };
			const size_t size = _listBlock.size();
			for ( size_t blockIndex = size; blockIndex > 0; --blockIndex )
			{
				const size_t	uIndex	 = blockIndex - 1;
				const BlockType newCarry = _listBlock[uIndex] << ( kBitsPerBlock - bitShift );
				_listBlock[uIndex]		 = ( _listBlock[uIndex] >> bitShift ) | carry;
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
		return _listBlock == other._listBlock;
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
			const uint32	lastBlockIndex = static_cast<uint32>( _listBlock.size() - 1 );
			const BlockType mask		   = ( static_cast<BlockType>( 1 ) << restBits ) - 1;
			_listBlock[lastBlockIndex] &= mask;
		}
	}
} // namespace sw
