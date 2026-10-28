/**
 * @file DynamicBitset.h
 * @brief 동적 크기 비트셋입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) DynamicBitset — uint64 블록. 생성 · 크기 · 비트 읽기/쓰기
    // ------------------------------------------------------------------------------
    /** @brief 런타임에 크기가 정해지는 비트셋입니다. 블록은 uint64 입니다. */
    class SW_API DynamicBitset final
    {
        using BlockType = uint64;

    public:
        /**
         * @brief 빈 비트셋입니다. 암시적이어야 이 타입을 필드로 둔 구조체를 `T value{}` 로 값 초기화할 수 있습니다(리플렉션 필드).
         */
        DynamicBitset()
            : _listBlock{}
            , _bitCount{ 0 }
        {
        }
        /**
         * @brief size 비트를 모두 0 으로 둡니다.
         */
        explicit DynamicBitset( uint32 size );
        /**
         * @brief size 비트를 잡고, 하위 비트를 value 로 채웁니다.
         */
        explicit DynamicBitset( uint32 size, uint64 value );
        /** @brief 블록 벡터를 해제합니다. */
        ~DynamicBitset() = default;
        /** @brief 복사 생성합니다. */
        DynamicBitset( const DynamicBitset& other ) = default;
        /** @brief 이동 생성합니다. */
        DynamicBitset( DynamicBitset&& other ) noexcept = default;
        /** @brief 복사 대입합니다. */
        DynamicBitset& operator=( const DynamicBitset& other ) = default;
        /** @brief 이동 대입합니다. */
        DynamicBitset& operator=( DynamicBitset&& other ) noexcept = default;

        /**
         * @brief '0' · '1' 문자열(맨 왼쪽이 가장 높은 비트, `to_string` 의 역)을 읽습니다.
         * @details 다른 글자가 하나라도 있으면 false 이고 outValue 는 그대로입니다. 문자열 생성자를 두지 않는 이유:
         *          생성자는 실패를 돌려줄 수 없어, 잘못된 글자를 로그만 남기고 0 으로 읽는 폴백이 됐습니다.
         */
        [[nodiscard]] static bool parse( string_view text, DynamicBitset& outValue );

        /** @brief 비트 수를 반환합니다. */
        uint32 size() const { return _bitCount; }

        /** @brief 비어 있는지 반환합니다. */
        bool empty() const { return _bitCount == 0; }

        /** @brief 현재 용량(비트 수)을 반환합니다. */
        uint32 capacity() const { return static_cast<uint32>( _listBlock.capacity() ) * kBitsPerBlock; }

        /** @brief 용량을 크기에 맞게 줄입니다. */
        void shrink_to_fit() { _listBlock.shrink_to_fit(); }

        /** @brief 용량을 미리 확보합니다. */
        void reserve( const uint32 newCapacity ) { _listBlock.reserve( computeBlockCount( newCapacity ) ); }
        /**
         * @brief 크기를 바꿉니다. 새로 생긴 비트는 value 로 채웁니다.
         */
        void resize( uint32 newSize, bool value = false );
        /**
         * @brief 맨 끝(가장 높은 위치)에 비트 하나를 덧붙입니다.
         */
        void push_back( bool value );

        /**
         * @brief bitPosition 비트를 읽습니다.
         * @details 비트 하나씩 도는 호출(직렬화의 `test` · `set` 루프)이 함수 호출 비용을 내지 않도록 헤더에 둡니다. 범위 검사의
         *          실패 쪽(로그)만 .cpp 의 `reportOutOfRange` 로 뺍니다 — 단언 매크로의 8 KB 버퍼가 호출하는 쪽 스택에 들어오지 않게.
         */
        bool operator[]( const uint32 bitPosition ) const
        {
            if ( bitPosition >= _bitCount )
                reportOutOfRange( bitPosition, _bitCount );
            return ( _listBlock[getBlockIndex( bitPosition )] & bitMask( bitPosition ) ) != 0;
        }

        /** @brief bitPosition 비트가 켜져 있으면 true 입니다. */
        bool test( const uint32 bitPosition ) const { return ( *this )[bitPosition]; }

        /**
         * @brief 모든 비트를 켭니다.
         */
        DynamicBitset& set();
        /**
         * @brief bitPosition 비트를 value 로 둡니다.
         */
        DynamicBitset& set( const uint32 bitPosition, const bool value = true )
        {
            if ( bitPosition >= _bitCount )
                reportOutOfRange( bitPosition, _bitCount );
            BlockType&      block = _listBlock[getBlockIndex( bitPosition )];
            const BlockType mask  = bitMask( bitPosition );
            block                 = value ? ( block | mask ) : ( block & ~mask );
            return *this;
        }

        /**
         * @brief 모든 비트를 끕니다.
         */
        DynamicBitset& reset();

        /** @brief bitPosition 비트를 끕니다. */
        DynamicBitset& reset( const uint32 bitPosition ) { return set( bitPosition, false ); }

        /**
         * @brief 모든 비트를 뒤집습니다.
         */
        DynamicBitset& flip();
        /**
         * @brief bitPosition 비트를 뒤집습니다.
         */
        DynamicBitset& flip( const uint32 bitPosition )
        {
            if ( bitPosition >= _bitCount )
                reportOutOfRange( bitPosition, _bitCount );
            _listBlock[getBlockIndex( bitPosition )] ^= bitMask( bitPosition );
            return *this;
        }

        /**
         * @brief 모든 비트가 켜져 있으면 true 입니다.
         */
        bool all() const;
        /**
         * @brief 켜진 비트가 하나라도 있으면 true 입니다.
         */
        bool any() const;

        /** @brief 켜진 비트가 없으면 true 입니다. */
        bool none() const { return any() == false; }
        /**
         * @brief 켜진 비트 수입니다.
         */
        uint32 count() const;

        /**
         * @brief '0' · '1' 문자열로 바꿉니다.
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
         * @brief 블록 버퍼가 차지하는 바이트 수입니다.
         */
        uint32 memory_usage() const;

        /** @brief 모든 비트를 뒤집은 사본을 반환합니다. */
        DynamicBitset operator~() const;
        /** @brief 비트 AND 한 뒤 대입합니다. */
        DynamicBitset& operator&=( const DynamicBitset& other );
        /** @brief 비트 OR 한 뒤 대입합니다. */
        DynamicBitset& operator|=( const DynamicBitset& other );
        /** @brief 비트 XOR 한 뒤 대입합니다. */
        DynamicBitset& operator^=( const DynamicBitset& other );
        /** @brief 비트 AND 를 구합니다. */
        DynamicBitset operator&( const DynamicBitset& other ) const;
        /** @brief 비트 OR 를 구합니다. */
        DynamicBitset operator|( const DynamicBitset& other ) const;
        /** @brief 비트 XOR 를 구합니다. */
        DynamicBitset operator^( const DynamicBitset& other ) const;
        /** @brief 왼쪽으로 시프트한 뒤 대입합니다. */
        DynamicBitset& operator<<=( uint32 shift );
        /** @brief 오른쪽으로 시프트한 뒤 대입합니다. */
        DynamicBitset& operator>>=( uint32 shift );
        /** @brief 왼쪽으로 시프트한 사본을 반환합니다. */
        DynamicBitset operator<<( uint32 shift ) const;
        /** @brief 오른쪽으로 시프트한 사본을 반환합니다. */
        DynamicBitset operator>>( uint32 shift ) const;
        /** @brief 같은지 비교합니다. */
        bool operator==( const DynamicBitset& other ) const;

        /** @brief 다른지 비교합니다. */
        bool operator!=( const DynamicBitset& other ) const { return ( *this == other ) == false; }
        /** @brief 사전순으로 작은지 비교합니다. */
        bool operator<( const DynamicBitset& other ) const;
        /** @brief 스트림에 '0' · '1' 문자열(to_string())로 씁니다. */
        friend std::ostream& operator<<( std::ostream& os, const DynamicBitset& bitset );

    private:
        /** @brief 비트 위치가 속한 블록의 인덱스를 반환합니다. */
        static uint32 getBlockIndex( const uint32 bitPosition ) { return bitPosition >> kBlockShift; }
        /** @brief 블록 안에서의 비트 인덱스를 반환합니다. */
        static uint32 getBitIndexInBlock( const uint32 bitPosition ) { return bitPosition & kBlockMask; }
        /** @brief 비트 수에 필요한 블록 수를 구합니다. */
        static uint32 computeBlockCount( const uint32 bitCount ) { return bitCount == 0 ? 0 : ( bitCount + kBitsPerBlock - 1 ) >> kBlockShift; }
        /** @brief 해당 위치의 비트 마스크를 반환합니다. */
        static BlockType bitMask( const uint32 bitPosition ) { return static_cast<BlockType>( 1 ) << getBitIndexInBlock( bitPosition ); }

        /**
         * @brief 비트 위치가 범위 밖이라고 단언합니다(Debug 는 멈추고, 그 밖은 Error 로그). 헤더의 비트 접근이 부르는 차가운 쪽입니다.
         */
        static void reportOutOfRange( uint32 bitPosition, uint32 bitCount );

        /**
         * @brief 마지막 블록에서 쓰지 않는 비트를 0 으로 지웁니다.
         */
        void sanitize();

        static constexpr uint32 kBitsPerBlock = sizeof( BlockType ) * 8;
        static constexpr uint32 kBlockMask    = kBitsPerBlock - 1;
        static constexpr uint32 kBlockShift   = 6;

        vector<BlockType> _listBlock;
        uint32            _bitCount;
    };
} // namespace sw
