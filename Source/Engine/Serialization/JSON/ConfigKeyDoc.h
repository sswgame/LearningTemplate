/**
 * @file ConfigKeyDoc.h
 * @brief 손으로 읽는 설정 JSON 의 키 표입니다 — 읽기 코드는 이 표로 모르는 키를 거르고, 문서 생성기(`Scripts/common/ConfigReference.py`)는 같은 표로
 *        `docs/Config/` 의 칸 표를 만듭니다. 키가 두 곳(검사 목록 · 문서)에 적히지 않게 합니다.
 * @details 표는 반드시 이 모양으로 적습니다(생성기가 이 모양만 읽는다):
 *          @code
 *          static constexpr ConfigKeyDoc kArrXxxKeyDoc[] = {
 *              { "key", "type", "default", "설명 — 중괄호를 쓰지 않는다" },
 *          };
 *          @endcode
 *          표 바로 위의 `@brief` 문서 주석이 그 절의 설명입니다. 리플렉션 구조체로 읽는 설정은 이것을 쓰지 않습니다(칸 표가 PROPERTY 에서 나온다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class JSONValue;

    /** @brief 키 하나입니다. 문자열 넷 모두 문서에 그대로 나갑니다. */
    struct ConfigKeyDoc
    {
        const utf8* _pKey;         ///< JSON 키
        const utf8* _pType;        ///< 값 모양(`string` · `bool` · `number` · `string[]` · `object` …)
        const utf8* _pDefault;     ///< 적지 않았을 때의 값(없으면 빈 글)
        const utf8* _pDescription; ///< 설명(한국어)
    };
} // namespace sw

namespace sw
{
    /** @brief 키 표로 JSON 오브젝트를 검사합니다. */
    struct SW_API ConfigKeyDocUtil
    {
        /**
         * @brief @p object 의 키가 모두 표에 있는지 봅니다. 모르는 키마다 "@p context: unknown key 'x' (known: a, b, …)" 오류를 남기고 false 입니다.
         * @param pOutUnknownKey 첫 모르는 키를 받을 자리(없어도 된다)
         */
        [[nodiscard]] static bool hasOnlyKnownKeys( const JSONValue& object, const ConfigKeyDoc* pArrKeyDoc, size_t keyCount, string_view context,
                                                    string* pOutUnknownKey = nullptr );

        /** @brief 배열 판입니다. */
        template <size_t N>
        [[nodiscard]] static bool hasOnlyKnownKeys( const JSONValue& object, const ConfigKeyDoc ( &arrKeyDoc )[N], string_view context,
                                                    string* pOutUnknownKey = nullptr )
        {
            return hasOnlyKnownKeys( object, arrKeyDoc, N, context, pOutUnknownKey );
        }
    };
} // namespace sw
