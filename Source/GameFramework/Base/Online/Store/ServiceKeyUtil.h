/**
 * @file ServiceKeyUtil.h
 * @brief 서비스 저장소의 키 · 레코드 바이트 도우미 — 고정 16 자리 16 진수(키 순서 = 수 순서), 레코드 문자열 쓰기 · 읽기입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 서비스 저장소 키 · 레코드 도우미입니다. */
    struct SW_GF_API ServiceKeyUtil
    {
        /** @brief `appendHex64` 이 쓰는 자리 수입니다(uint64 = 16 진수 16 자리). 키에서 수 부분을 잘라 낼 때 씁니다. */
        static constexpr int32 kHexWidth = 16;

        /** @brief @p value 를 소문자 16 진수 16 자리로 붙입니다 — 키 사전순이 수 순서와 같다. */
        static void appendHex64( string& outKey, uint64 value );
        /** @brief `appendHex64` 의 16 자리를 읽습니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseHex64( string_view text, uint64& outValue );
        static string             makeHex64( uint64 value );

        /** @brief 길이 + UTF-8 바이트를 씁니다. */
        static void writeString( BitWriter& outWriter, string_view text );
        /** @brief `writeString` 으로 쓴 것을 읽습니다. 길이가 @p maxSize 를 넘거나 모자라면 false 입니다. */
        [[nodiscard]] static bool readString( BitReader& reader, int32 maxSize, string& outText );
    };
} // namespace sw
