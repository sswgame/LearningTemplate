/**
 * @file ASCIIUtil.h
 * @brief 문자 하나의 ASCII 대소문자 변환입니다 — `HashUtil` 의 문자열 해시와 `StringUtil::toLowerChar` 가 같은 정의를 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct ASCIIUtil
     * @brief ASCII('A'~'Z')만 바꾸고 그 밖의 문자는 그대로 두는 static 도우미입니다(`utf8` · `utf16` 모두).
     */
    struct ASCIIUtil
    {
        /** @brief ASCII 대문자를 소문자로 바꿉니다. 그 밖의 문자는 그대로입니다. */
        template <typename CharT>
        [[nodiscard]] static constexpr CharT toLower( const CharT ch ) noexcept
        {
            return ( CharT( 'A' ) <= ch && ch <= CharT( 'Z' ) ) ? static_cast<CharT>( ch + ( CharT( 'a' ) - CharT( 'A' ) ) ) : ch;
        }
    };

    static_assert( ASCIIUtil::toLower( 'Q' ) == 'q' && ASCIIUtil::toLower( L'Q' ) == L'q' && ASCIIUtil::toLower( '1' ) == '1', "ASCII 대문자만 소문자가 된다" );
} // namespace sw
