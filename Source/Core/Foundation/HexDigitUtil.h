/**
 * @file HexDigitUtil.h
 * @brief 16진수 글자와 값 사이의 변환입니다 — 할당 · 로그 없는 기본형이라 크래시 경로와 낮은 티어도 씁니다.
 * @note `string` 을 돌려주는 편의 함수는 `StringUtil`(티어 3)에 있습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 16진수 글자 `a`~`f` 의 대소문자입니다. */
    enum class HexCase : uint8
    {
        Lower,
        Upper,
    };

    /**
     * @struct HexDigitUtil
     * @brief 숫자 → 16진수 글자, 16진수 글자 → 숫자 static 도우미입니다. 읽는 쪽은 대소문자를 가리지 않고 실패하면 출력을 건드리지 않습니다.
     */
    struct HexDigitUtil
    {
        /** @brief 한 자리가 담는 비트 수입니다. */
        static constexpr uint32 kBitsPerDigit = 4;

        /** @brief `uint64` 가 담는 16진수 자리 수입니다. */
        static constexpr uint32 kMaxDigitCount = 16;

        /**
         * @brief @p value 를 높은 자리부터 @p digitCount 글자로 @p pOut 에 씁니다(끝의 NUL 은 쓰지 않습니다).
         * @details 값이 @p digitCount 자리보다 크면 낮은 자리만 남고, 16 자리보다 많이 요구하면 앞을 `0` 으로 채웁니다.
         */
        static void writeDigits( utf8* pOut, uint64 value, uint32 digitCount, HexCase hexCase ) noexcept
        {
            const utf8 letterBase = hexCase == HexCase::Upper ? 'A' : 'a';
            for ( uint32 index = 0; index < digitCount; ++index )
            {
                const uint32 digitIndex = digitCount - 1 - index;
                const uint32 digit      = digitIndex < kMaxDigitCount ? static_cast<uint32>( ( value >> ( digitIndex * kBitsPerDigit ) ) & 0xFu ) : 0u;
                pOut[index]             = static_cast<utf8>( digit < 10u ? '0' + digit : letterBase + ( digit - 10u ) );
            }
        }

        /** @brief 글자 하나(`0`~`9` · `a`~`f` · `A`~`F`)를 0~15 로 읽습니다. 그 밖의 글자면 false 입니다. */
        [[nodiscard]] static constexpr bool tryParseDigit( utf8 digit, uint32& outValue ) noexcept
        {
            if ( '0' <= digit && digit <= '9' )
            {
                outValue = static_cast<uint32>( digit - '0' );
                return true;
            }
            if ( 'a' <= digit && digit <= 'f' )
            {
                outValue = static_cast<uint32>( digit - 'a' ) + 10u;
                return true;
            }
            if ( 'A' <= digit && digit <= 'F' )
            {
                outValue = static_cast<uint32>( digit - 'A' ) + 10u;
                return true;
            }
            return false;
        }

        /** @brief 1~16 글자의 16진수 글자 전체를 값 하나로 읽습니다. 비었거나 16 글자를 넘거나 16진수가 아닌 글자가 있으면 false 이고 @p outValue 는 그대로입니다. */
        [[nodiscard]] static constexpr bool tryParseFixed( string_view text, uint64& outValue ) noexcept
        {
            if ( text.empty() || text.size() > kMaxDigitCount )
                return false;
            uint64 value = 0;
            for ( const utf8 character : text )
            {
                uint32 digit = 0;
                if ( tryParseDigit( character, digit ) == false )
                    return false;
                value = ( value << kBitsPerDigit ) | digit;
            }
            outValue = value;
            return true;
        }

        /**
         * @brief 글자 두 개마다 바이트 하나로 읽어 @p pOut 에 @p byteCount 바이트를 씁니다. 글자 수가 `byteCount * 2` 가 아니거나 16진수가 아닌 글자가 있으면 false 이고, 뒤쪽에서 실패하면 앞 바이트는 이미 써 있습니다(성공한 때만 읽습니다).
         * @note 티어 0 이라 `span`(Container)을 받지 못해 포인터와 개수를 받습니다.
         */
        [[nodiscard]] static bool tryParseBytes( string_view text, uint8* pOut, uint32 byteCount ) noexcept
        {
            if ( text.size() != static_cast<size_t>( byteCount ) * 2 )
                return false;
            for ( size_t byteIndex = 0; byteIndex < byteCount; ++byteIndex )
            {
                const size_t textIndex = byteIndex * 2;
                uint32       high      = 0;
                uint32       low       = 0;
                if ( tryParseDigit( text[textIndex], high ) == false || tryParseDigit( text[textIndex + 1], low ) == false )
                    return false;
                pOut[byteIndex] = static_cast<uint8>( ( high << kBitsPerDigit ) | low );
            }
            return true;
        }
    };
} // namespace sw
