/**
 * @file DynamicBitset.h
 * @brief 동적 크기 비트셋
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) DynamicBitset — uint64 블록. 생성 · 크기 · 비트 읽기/쓰기
    // ------------------------------------------------------------------------------
    /** @brief 런타임 크기 비트셋. 블록은 uint64 입니다. */
    class SW_API DynamicBitset final
    {
        using BlockType = uint64;

    public:
        /**
         * @brief size 비트를 0으로 둡니다.
         */
        explicit DynamicBitset( uint32 size = 0 );
        /**
         * @brief '0'/'1' 문자열로 비트를 채웁니다.
         */
        explicit DynamicBitset( string_view str );
        /**
         * @brief size 비트를 두고 하위 비트를 value 로 채웁니다.
         */
        explicit DynamicBitset( uint32 size, uint64 value );
        /** @brief 블록 벡터만 버립니다. */
        ~DynamicBitset() = default;
        /** @brief 복사 생성합니다. */
        DynamicBitset( const DynamicBitset& other ) = default;
        /** @brief 이동 생성합니다. */
        DynamicBitset( DynamicBitset&& other ) noexcept = default;
        /** @brief 복사 대입합니다. */
        DynamicBitset& operator=( const DynamicBitset& other ) = default;
        /** @brief 이동 대입합니다. */
        DynamicBitset& operator=( DynamicBitset&& other ) noexcept = default;

        /** @brief 원소 개수를 반환합니다. */
        uint32 size() const { return _bitCount; }

        /** @brief 비어 있는지 반환합니다. */
        bool empty() const { return _bitCount == 0; }

        /** @brief 현재 용량을 반환합니다. */
        uint32 capacity() const { return static_cast<uint32>( _listBlock.capacity() ) * kBitsPerBlock; }

        /** @brief 용량을 크기에 맞춥니다. */
        void shrink_to_fit() { _listBlock.shrink_to_fit(); }

        /** @brief 용량을 예약합니다. */
        void reserve( const uint32 newCapacity ) { _listBlock.reserve( calculateBlockCount( newCapacity ) ); }
        /**
         * @brief 크기를 변경합니다
         */
        void resize( uint32 newSize, bool value = false );

        /** @brief 지정 위치의 원소를 반환합니다. */
        bool operator[]( uint32 bitPosition ) const;

        /** @brief 비트를 검사합니다. */
        bool test( const uint32 bitPosition ) const { return ( *this )[bitPosition]; }

        /**
         * @brief 모든 비트를 켭니다.
         */
        DynamicBitset& set();
        /**
         * @brief bitPosition 비트를 value 로 둡니다.
         */
        DynamicBitset& set( uint32 bitPosition, bool value = true );

        /**
         * @brief 모든 비트를 끕니다.
         */
        DynamicBitset& reset();

        /** @brief 초기 상태로 되돌립니다. */
        DynamicBitset& reset( const uint32 bitPosition ) { return set( bitPosition, false ); }

        /**
         * @brief 모든 비트를 뒤집습니다.
         */
        DynamicBitset& flip();
        /**
         * @brief bitPosition 비트를 뒤집습니다.
         */
        DynamicBitset& flip( uint32 bitPosition );

        /**
         * @brief 모든 비트가 켜져 있으면 true입니다.
         */
        bool all() const;
        /**
         * @brief 켜진 비트가 하나라도 있으면 true입니다.
         */
        bool any() const;

        /** @brief 켜진 비트가 없으면 true입니다. */
        bool none() const { return any() == false; }
        /**
         * @brief 켜진 비트 개수입니다.
         */
        uint32 count() const;

        /**
         * @brief '0'/'1' 문자열로 바꿉니다.
         */
        string to_string() const;
        /**
         * @brief 하위 64비트를 정수로 읽습니다.
         */
        uint64 to_ullong() const;
        /**
         * @brief 하위 32비트를 정수로 읽습니다.
         */
        uint32 to_ulong() const;
        /**
         * @brief 블록 버퍼가 쓰는 바이트 수입니다.
         */
        uint32 memory_usage() const;

        /** @brief 비트를 반전한 복사본을 반환합니다. */
        DynamicBitset operator~() const;
        /** @brief 비트 AND 후 대입합니다. */
        DynamicBitset& operator&=( const DynamicBitset& other );
        /** @brief 비트 OR 후 대입합니다. */
        DynamicBitset& operator|=( const DynamicBitset& other );
        /** @brief 비트 XOR 후 대입합니다. */
        DynamicBitset& operator^=( const DynamicBitset& other );
        /** @brief 비트 AND를 수행합니다. */
        DynamicBitset operator&( const DynamicBitset& other ) const;
        /** @brief 비트 OR를 수행합니다. */
        DynamicBitset operator|( const DynamicBitset& other ) const;
        /** @brief 비트 XOR를 수행합니다. */
        DynamicBitset operator^( const DynamicBitset& other ) const;
        /** @brief 왼쪽 시프트합니다. */
        DynamicBitset& operator<<=( uint32 shift );
        /** @brief 오른쪽 시프트합니다. */
        DynamicBitset& operator>>=( uint32 shift );
        /** @brief 왼쪽 시프트합니다. */
        DynamicBitset operator<<( uint32 shift ) const;
        /** @brief 오른쪽 시프트합니다. */
        DynamicBitset operator>>( uint32 shift ) const;
        /** @brief 같은지 비교합니다. */
        bool operator==( const DynamicBitset& other ) const;

        /** @brief 다른지 비교합니다. */
        bool operator!=( const DynamicBitset& other ) const { return ( *this == other ) == false; }
        /** @brief 사전순으로 작은지 비교합니다. */
        bool operator<( const DynamicBitset& other ) const;
        /** @brief 왼쪽 시프트합니다. */
        friend std::ostream& operator<<( std::ostream& os, const DynamicBitset& bitset );

    private:
        /** @brief 블록 인덱스를 반환합니다. */
        static uint32 getBlockIndex( const uint32 bitPosition ) { return bitPosition >> kBlockShift; }
        /** @brief 블록 내 비트 인덱스를 반환합니다. */
        static uint32 getBitIndexInBlock( const uint32 bitPosition ) { return bitPosition & kBlockMask; }
        /** @brief 필요한 블록 수를 계산합니다. */
        static uint32 calculateBlockCount( const uint32 bitCount ) { return bitCount == 0 ? 0 : ( bitCount + kBitsPerBlock - 1 ) >> kBlockShift; }
        /** @brief 해당 위치의 비트 마스크를 반환합니다. */
        static BlockType bitMask( const uint32 bitPosition ) { return static_cast<BlockType>( 1 ) << getBitIndexInBlock( bitPosition ); }

        /**
         * @brief 마지막 블록의 사용하지 않는 비트를 0으로 지웁니다.
         */
        void sanitize();

        static constexpr uint32 kBitsPerBlock = sizeof( BlockType ) * 8;
        static constexpr uint32 kBlockMask    = kBitsPerBlock - 1;
        static constexpr uint32 kBlockShift   = 6;

        vector<BlockType> _listBlock;
        uint32            _bitCount;
    };
} // namespace sw
