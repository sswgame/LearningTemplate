#pragma once
/**
 * @file DynamicBitset.h
 * @brief 동적 크기 비트셋
 */

#include "Core/CoreMinimal.h"

#include "Core/Common/Types.h"

namespace sw
{

	class DynamicBitset final
	{
		using BlockType = uint64;

	public:
		/**
		 * @brief 비트셋을 생성합니다
		 */
		explicit DynamicBitset( uint32 size = 0 );
		/**
		 * @brief 비트셋을 생성합니다
		 */
		explicit DynamicBitset( const std::string& str );
		/**
		 * @brief 비트셋을 생성합니다
		 */
		explicit DynamicBitset( uint32 size, uint64 value );
		~DynamicBitset()										   = default;
		DynamicBitset( const DynamicBitset& other )				   = default;
		DynamicBitset( DynamicBitset&& other ) noexcept			   = default;
		DynamicBitset& operator=( const DynamicBitset& other )	   = default;
		DynamicBitset& operator=( DynamicBitset&& other ) noexcept = default;

	public:
		uint32 size() const { return _bitCount; }

		bool empty() const { return _bitCount == 0; }

		uint32 capacity() const { return static_cast<uint32>( _blockList.capacity() ) * kBitsPerBlock; }

		void shrink_to_fit() { _blockList.shrink_to_fit(); }

		void reserve( const uint32 newCapacity ) { _blockList.reserve( calculateBlockCount( newCapacity ) ); }
		/**
		 * @brief 크기를 변경합니다
		 */
		void resize( uint32 newSize, bool value = false );

		bool operator[]( uint32 pos ) const;

		bool test( const uint32 pos ) const { return ( *this )[pos]; }

		/**
		 * @brief 값을 설정합니다
		 */
		DynamicBitset& set();
		/**
		 * @brief 값을 설정합니다
		 */
		DynamicBitset& set( uint32 pos, bool value = true );

		/**
		 * @brief 초기 상태로 되돌립니다
		 */
		DynamicBitset& reset();

		DynamicBitset& reset( const uint32 pos ) { return set( pos, false ); }

		/**
		 * @brief 비트를 반전합니다
		 */
		DynamicBitset& flip();
		/**
		 * @brief 비트를 반전합니다
		 */
		DynamicBitset& flip( uint32 pos );

		/**
		 * @brief 모두 켜져 있는지 반환합니다
		 */
		bool all() const;
		/**
		 * @brief 켜진 비트가 있는지 반환합니다
		 */
		bool any() const;

		bool none() const { return any() == false; }
		/**
		 * @brief 설정된 비트 수를 반환합니다
		 */
		uint32 count() const;

		/**
		 * @brief 문자열로 변환합니다
		 */
		std::string to_string() const;
		/**
		 * @brief unsigned long long으로 변환합니다
		 */
		uint64 to_ullong() const;
		/**
		 * @brief unsigned long으로 변환합니다
		 */
		uint32 to_ulong() const;
		/**
		 * @brief 메모리 사용량을 반환합니다
		 */
		uint32 memory_usage() const;

	public:
		DynamicBitset  operator~() const;
		DynamicBitset& operator&=( const DynamicBitset& other );
		DynamicBitset& operator|=( const DynamicBitset& other );
		DynamicBitset& operator^=( const DynamicBitset& other );
		DynamicBitset  operator&( const DynamicBitset& other ) const;
		DynamicBitset  operator|( const DynamicBitset& other ) const;
		DynamicBitset  operator^( const DynamicBitset& other ) const;
		DynamicBitset& operator<<=( uint32 shift );
		DynamicBitset& operator>>=( uint32 shift );
		DynamicBitset  operator<<( uint32 shift ) const;
		DynamicBitset  operator>>( uint32 shift ) const;
		bool		   operator==( const DynamicBitset& other ) const;

		bool				 operator!=( const DynamicBitset& other ) const { return ( *this == other ) == false; }
		bool				 operator<( const DynamicBitset& other ) const;
		friend std::ostream& operator<<( std::ostream& os, const DynamicBitset& bitset );

	private:
		static uint32	 getBlockIndex( const uint32 pos ) { return pos >> kBlockShift; }
		static uint32	 getBitIndexInBlock( const uint32 pos ) { return pos & kBlockMask; }
		static uint32	 calculateBlockCount( const uint32 bitCount ) { return bitCount == 0 ? 0 : ( bitCount + kBitsPerBlock - 1 ) >> kBlockShift; }
		static BlockType bitMask( const uint32 pos ) { return static_cast<BlockType>( 1 ) << getBitIndexInBlock( pos ); }

		/**
		 * @brief 값을 정제합니다
		 */
		void sanitize();

	private:
		static constexpr uint32 kBitsPerBlock = sizeof( BlockType ) * 8;
		static constexpr uint32 kBlockMask	  = kBitsPerBlock - 1;
		static constexpr uint32 kBlockShift	  = 6;

		std::vector<BlockType> _blockList;
		uint32				   _bitCount;
	};
} // namespace sw
