/**
 * @file SourceTokens.h
 * @brief 인자 이름 · 기본 인자처럼 AST 타입에 남지 않는 것을 소스 토큰에서 읽습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 인자 이름 · 기본 인자처럼 AST 타입에 남지 않는 것을 소스 토큰에서 읽습니다. */
    struct SourceTokens
    {
        /** @brief 커서 선언의 소스 토큰에 식별자 @p name 이 그대로 적혀 있는지 봅니다(매크로가 만든 이름이면 false). */
        [[nodiscard]] static bool isSpelledInSource( CXCursor cursor, const string& name );

        /**
         * @brief 인자 선언 하나의 기본 인자(`= 식`)를 C++ 글 그대로 읽습니다. 없으면 빈 문자열입니다.
         * @details 기본 인자는 타입에 남지 않아 AST 의 인자 커서 범위를 토큰으로 읽는다. 괄호 밖의 첫 `=` 뒤가 식이다.
         */
        static string readDefaultArgument( CXCursor parameterCursor );

        /**
         * @brief `MulticastDelegate<void( int32 amount, TagID tag )>` 의 인자 이름들을 소스 토큰에서 읽습니다.
         * @details 함수 타입의 인자 이름은 타입에 남지 않는다. 필드 선언에 시그니처가 없으면(별칭 `using DamagedEvent = …` 으로 적었으면)
         *          그 별칭 선언을 읽는다. 못 찾으면 빈 목록 — 이름은 표시용이라 없어도 묶기 · 호출은 된다.
         */
        static vector<string> readEventParameterNames( CXCursor fieldCursor );

        /**
         * @brief 필드 @p name 의 헤더 초기값(`= …` · `{ … }`)에 비어 있지 않은 문자열 리터럴이 있는지 봅니다.
         * @details 필드 커서 범위는 클래스 안 초기값까지 덮는다. 애노테이션 괄호 안의 글(`Tooltip = "…"`)은 이름 앞이라 보지 않는다.
         */
        [[nodiscard]] static bool hasStringLiteralDefault( CXCursor fieldCursor, const string& name );
    };
} // namespace sw
