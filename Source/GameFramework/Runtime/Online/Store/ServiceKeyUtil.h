/**
 * @file ServiceKeyUtil.h
 * @brief 서비스 저장소의 키와 레코드 바이트 도우미입니다. 고정 16자 16 진수(키 순서가 수 순서와 같다)와 레코드 문자열 쓰기, 읽기를 둡니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 서비스 저장소 키 · 레코드 도우미입니다. */
    struct SW_GF_API ServiceKeyUtil
    {
        /** @brief `appendHex64` 이 쓰는 자릿수입니다(uint64 는 16 진수 16자). 키에서 수 부분을 잘라 낼 때 씁니다. */
        static constexpr int32 kHexWidth = 16;

        /** @brief @p value 를 소문자 16 진수 16자로 붙입니다. 그래서 키 사전순이 수 순서와 같습니다. */
        static void appendHex64( string& outKey, uint64 value );
        /** @brief `appendHex64` 의 16자를 읽습니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseHex64( string_view text, uint64& outValue );
        static string             makeHex64( uint64 value );

        /** @brief id 하나를 레코드 값(가변 정수)으로 씁니다. 다른 표의 레코드를 가리키는 연결 레코드(게스트 → 계정, 계정 → 세션)가 쓴다. */
        static vector<uint8> encodeVarID( uint64 id );
        /**
         * @brief `encodeVarID` 의 값을 읽습니다. 깨졌으면 0 입니다.
         * @details 서비스 id(계정 · 세션 · 거래)는 0 을 내지 않는다 — 0 은 "없음" 이라 부르는 쪽이 같은 검사로 "없음 · 깨짐" 을 함께 거른다(대체값이 아니라 형식의 약속).
         */
        static uint64 decodeVarID( const vector<uint8>& bytes );

        /** @brief 길이 + UTF-8 바이트를 씁니다. */
        static void writeString( BitWriter& outWriter, string_view text );
        /** @brief `writeString` 으로 쓴 것을 읽습니다. 길이가 @p maxSize 를 넘거나 모자라면 false 입니다. */
        [[nodiscard]] static bool readString( BitReader& reader, int32 maxSize, string& outText );
    };
} // namespace sw
