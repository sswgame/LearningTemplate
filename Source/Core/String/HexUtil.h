/**
 * @file HexUtil.h
 * @brief 바이트 · 정수를 16진 글로 쓰고 읽는 정본입니다 — 다이제스트 · 해시 · 키 · GUID 의 글 표현이 모두 이것을 씁니다.
 * @details 호출부마다 `"0123456789abcdef"` 표를 두고 니블을 손으로 뽑으면 대소문자 · 자릿수 채움 · 바이트 순서가 갈립니다.
 *          한 줄 서식 안의 정수 하나는 `formatstring` 의 `%x` · `%016llx` 로도 됩니다. 이것은 바이트 열과 고정 폭 정수용입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 16진 글자의 대소문자입니다. */
    enum class HexCase : uint8
    {
        Lower,
        Upper,
    };

    /** @brief 16진 쓰기 · 읽기 도우미입니다(전부 static). */
    struct SW_API HexUtil
    {
        /** @brief @p pData 의 @p size 바이트를 바이트마다 두 글자로 @p inoutText 뒤에 붙입니다(앞 바이트가 먼저). */
        static void appendBytes( string& inoutText, const uint8* pData, size_t size, HexCase hexCase = HexCase::Lower );
        /** @brief `appendBytes` 를 새 글로 돌려줍니다. */
        [[nodiscard]] static string encodeBytes( const uint8* pData, size_t size, HexCase hexCase = HexCase::Lower );
        /** @brief @p value 를 큰 자리부터 @p digitCount 글자(1 ~ 16, 모자라면 0 채움)로 붙입니다. */
        static void appendUint64( string& inoutText, uint64 value, uint32 digitCount = 16, HexCase hexCase = HexCase::Lower );
        /** @brief 두 글자씩 바이트로 읽습니다(대소문자 무관). 길이가 홀수이거나 16진 글자가 아니면 false 이고 @p outBytes 는 비웁니다. */
        [[nodiscard]] static bool decodeBytes( string_view text, vector<uint8>& outBytes );
    };
} // namespace sw
