/**
 * @file SourceTokens.h
 * @brief 인자 이름 · 기본 인자처럼 AST 타입에 남지 않는 것을 소스 토큰에서 읽습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 파서 한 번 실행의 토큰화 누계입니다. 끝 줄(`ReflectionParser.TokenizeStats`)이 전체 시간과 나란히 적는다 — 토큰화 몫이 클 때만 저장소 재사용을 검토한다. */
    struct SourceTokenStats
    {
        int64 _callCount{ 0 };
        int64 _tokenCount{ 0 };
        int64 _nanoseconds{ 0 };
    };

    /** @brief 인자 이름 · 기본 인자처럼 AST 타입에 남지 않는 것을 소스 토큰에서 읽습니다. */
    struct SourceTokens
    {
        /** @brief 지금까지의 토큰화 누계입니다. 병렬 파싱 워커가 함께 더하므로 원자값에서 읽습니다. */
        [[nodiscard]] static SourceTokenStats getStats();

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

    private:
        /** @brief 커서 범위의 토큰 철자 · 종류입니다. */
        struct SourceToken;
        /** @brief 위치를 매크로를 **쓴** 자리로 옮깁니다. 매크로 정의 안(명령줄 `-D` 버퍼)의 위치로는 소스 토큰을 읽을 수 없습니다. */
        static CXSourceLocation getExpansionPoint( CXTranslationUnit translationUnit, CXSourceLocation location );
        /**
         * @brief 커서 범위의 소스 토큰입니다.
         * @details 범위의 시작이 애노테이션 매크로(`PROPERTY(...)` 가 전개한 `__attribute__`) 안이면 그 위치는 매크로 정의 쪽이라, 범위를
         *          그대로 토큰으로 읽으면 선언의 토큰이 하나도 나오지 않는다. 양 끝을 전개 위치로 옮겨 읽는다.
         */
        static vector<SourceToken> tokenizeCursor( CXCursor cursor );
        /** @brief 여는 괄호류면 +1, 닫는 괄호류면 -1 입니다. `<` · `>` 도 셉니다(템플릿 인자 안의 쉼표를 가르지 않게). */
        static int32 getBracketDelta( string_view spelling ) noexcept;
        /** @brief 토큰을 C++ 식 글로 잇습니다. 이름 · 숫자끼리만 띄웁니다(`Mode::Fast` · `-1` · `sw::string("a b")`). */
        static string joinTokens( const vector<SourceToken>& listToken, size_t begin, size_t end );
        /** @brief 타입 이름을 이루는 낱말(이름 · `int` 같은 키워드)인지 봅니다. 한정자(`const` · `struct` …)는 아닙니다. */
        static bool isTypeWord( const SourceToken& token ) noexcept;
        /**
         * @brief 인자 선언 토큰 한 덩이(`const DamageEvent& damage`)에서 이름을 꺼냅니다. 이름 없이 적었으면 빈 문자열입니다.
         * @details 마지막 토큰이 이름이고 그 앞에 타입이 남아야 이름이다 — `int32` · `sw::TagID` · `const Foo` 는 이름이 없다.
         */
        static string findParameterName( const vector<SourceToken>& listToken, size_t begin, size_t end );
    };
} // namespace sw
