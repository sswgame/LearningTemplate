/**
 * @file BitUtil.h
 * @brief 바이트를 합치고 꺼내는 비트 도우미입니다 — 시프트와 마스크를 손으로 적지 않습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct BitUtil
     * @brief 32 비트 값 하나와 바이트 네 개 사이를 오가는 static 도우미입니다. 바이트 순서는 값의 **높은 바이트부터** 인자 순서입니다(엔디언과 무관한 산술 값).
     * @details 메모리 바이트 순서(파일 · 네트워크)는 이 도우미의 일이 아닙니다 — `0x11223344` 는 `packBytes( 0x11, 0x22, 0x33, 0x44 )` 이고, 리틀 엔디언 메모리에는 `44 33 22 11` 로 놓입니다.
     */
    struct BitUtil
    {
        /** @brief 바이트 네 개를 `0xHHMMNNLL`(높은 바이트부터) 로 합칩니다. */
        [[nodiscard]] static constexpr uint32 packBytes( const uint32 highest, const uint32 high, const uint32 low, const uint32 lowest ) noexcept
        {
            return ( ( highest & 0xFFu ) << 24 ) | ( ( high & 0xFFu ) << 16 ) | ( ( low & 0xFFu ) << 8 ) | ( lowest & 0xFFu );
        }

        /** @brief @p value 의 @p byteIndex 번째 바이트를 꺼냅니다(0 이 가장 낮은 바이트, 3 이 가장 높은 바이트). */
        [[nodiscard]] static constexpr uint32 getByte( const uint32 value, const uint32 byteIndex ) noexcept
        {
            return ( value >> ( byteIndex * 8u ) ) & 0xFFu;
        }

        /** @brief float32 의 IEEE 754 비트 패턴입니다(저장 · 해시 · 비트 단위 비교용). 값이 NaN 이어도 비트를 그대로 돌려줍니다. */
        [[nodiscard]] static constexpr uint32 floatToBits( const float32 value ) noexcept { return __builtin_bit_cast( uint32, value ); }

        /** @brief `floatToBits` 의 역입니다. */
        [[nodiscard]] static constexpr float32 bitsToFloat( const uint32 bits ) noexcept { return __builtin_bit_cast( float32, bits ); }

        /** @brief float64 의 IEEE 754 비트 패턴입니다. */
        [[nodiscard]] static constexpr uint64 doubleToBits( const float64 value ) noexcept { return __builtin_bit_cast( uint64, value ); }

        /** @brief `doubleToBits` 의 역입니다. */
        [[nodiscard]] static constexpr float64 bitsToDouble( const uint64 bits ) noexcept { return __builtin_bit_cast( float64, bits ); }
    };

    static_assert( BitUtil::packBytes( 0x11u, 0x22u, 0x33u, 0x44u ) == 0x11223344u, "packBytes 는 높은 바이트부터다" );
    static_assert( BitUtil::getByte( 0x11223344u, 3u ) == 0x11u && BitUtil::getByte( 0x11223344u, 0u ) == 0x44u, "getByte 의 0 은 가장 낮은 바이트다" );
    static_assert( BitUtil::floatToBits( 1.0f ) == 0x3F800000u && BitUtil::bitsToFloat( 0x3F800000u ) == 1.0f, "float32 비트는 IEEE 754 다" );
    static_assert( BitUtil::doubleToBits( 1.0 ) == 0x3FF0000000000000ull && BitUtil::bitsToDouble( 0x3FF0000000000000ull ) == 1.0, "float64 비트는 IEEE 754 다" );
} // namespace sw
