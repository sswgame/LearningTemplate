/**
 * @file JSONStringUtil.h
 * @brief JSON 문자열 본문의 이스케이프 규칙 하나입니다 — 엔진 JSON 쓰기(`JSONWriter`) · 구조화 로그(`JSONLinesLogOutput`)가 같은 바이트를 낸다.
 * @details 규칙: 따옴표 · 역슬래시는 `\"` · `\\`, 짧은 이스케이프가 있는 제어 문자는 `\b \f \n \r \t`, 나머지 제어 문자는 `\u00xx`(소문자),
 *          올바른 UTF-8 은 그대로, 잘못된 UTF-8 바이트는 `�` 로 바꾼다(출력이 늘 올바른 JSON 이다 — nlohmann `dump` 의 replace 와 같은 바이트).
 *          규칙이 여럿이면 같은 글이 출력마다 다른 바이트가 되고, 잘못된 UTF-8 을 그대로 흘리는 쪽은 읽는 도구를 깨뜨린다(예전 로그 싱크).
 *          JSON 글을 쓰는 곳이 Core(로그)와 Engine(직렬화) 둘이라 규칙은 아래 층인 Core 에 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct JSONStringUtil @brief JSON 문자열 이스케이프(전부 static). 파일 머리말 참고. */
    struct JSONStringUtil
    {
        /** @brief 이스케이프한 조각을 받는 함수입니다 — 고정 버퍼에 쓰는 쪽(`JSONWriter`)도 할당 없이 받는다. */
        using AppendFunction = void ( * )( void* pContext, string_view piece );

        /** @brief @p text 를 이스케이프해 조각마다 @p pAppend 로 넘깁니다(따옴표는 붙이지 않는다). 그대로 둘 글자는 이어진 한 조각으로 넘긴다. */
        static void escape( string_view text, AppendFunction pAppend, void* pContext );

        /** @brief @p text 를 이스케이프해 @p inoutText 뒤에 붙입니다(따옴표는 붙이지 않는다). */
        static void appendEscaped( string& inoutText, string_view text );
    };
} // namespace sw
