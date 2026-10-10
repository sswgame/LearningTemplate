/**
 * @file Base64Util.h
 * @brief Base64(RFC 4648) 쓰기 · 읽기 — 표준(`+/`, 채움 `=`)과 URL 안전(`-_`, 채움 없음 — JWT · PKCE · 공유 코드)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief Base64 도우미입니다. */
    struct SW_API Base64Util
    {
        /** @brief 표준 알파벳 · `=` 채움으로 씁니다. */
        static string encode( const uint8* pData, size_t size );
        /** @brief URL 안전 알파벳 · 채움 없이 씁니다. */
        static string encodeURL( const uint8* pData, size_t size );
        /** @brief 표준 알파벳을 읽습니다(끝의 `=` 채움은 있어도 없어도 된다). 알파벳 밖 글자 · 남는 비트가 0 이 아니면 false 입니다. */
        [[nodiscard]] static bool decode( string_view text, vector<uint8>& outBytes );
        /** @brief URL 안전 알파벳을 읽습니다(채움 없이 · 있어도 된다). 알파벳 밖 글자 · 남는 비트가 0 이 아니면 false 입니다. */
        [[nodiscard]] static bool decodeURL( string_view text, vector<uint8>& outBytes );
    };
} // namespace sw
