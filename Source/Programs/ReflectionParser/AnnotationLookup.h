/**
 * @file AnnotationLookup.h
 * @brief 커서에 붙은 애노테이션을 AST 에서 찾습니다 — 소스 글을 다시 읽지 않습니다.
 * @details 두 모양이 있습니다.
 *          - 멤버 · 열거자의 매크로(`PROPERTY` · `FUNCTION` · `ENUMERATOR`)와 본문 표식(`REFLECT_BODY`)은 선언에 붙는 자식 속성(AnnotateAttr)입니다.
 *          - 형식 앞의 매크로(`REFLECT` · `ENUM` · `REFLECT_CONTAINER`)는 파서 모드에서 **표식 선언** `static_assert( true, "REFLECT;" #__VA_ARGS__ );` 입니다.
 *            형식 키워드 앞의 GNU 속성은 clang 이 형식 선언에서 버리므로(`-Wignored-attributes`) 속성으로는 남지 않는다. 표식은 바로 다음 형제 선언의 것입니다.
 *          "이 레코드가 REFLECT 인가" 는 본문 표식으로 묻습니다 — REFLECT 와 REFLECT_BODY 는 서로 필수라(`AstVisitor`) 다른 헤더의 선언도 자식만 보면 됩니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/AnnotationApply.h"
#include "Programs/ReflectionParser/AnnotationFields.h"
#include "Programs/ReflectionParser/ParserDefines.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 커서에 붙은 애노테이션을 AST 에서 찾습니다 — 자식 속성과 형식 앞 표식 선언. */
    struct AnnotationLookup
    {
        /** @brief 자식 속성 중 접두사를 담은 첫 애노테이션의 철자입니다. 없으면 빈 문자열입니다. */
        static string findAnnotateAttr( CXCursor cursor, string_view prefix );

        /**
         * @brief @p cursor 가 @p desc 의 표식 선언(`static_assert( true, "<접두사>…" )`)이면 그 철자(`REFLECT;Abstract, …`)입니다. 아니면 빈 문자열입니다.
         * @details libclang 은 문자열 리터럴 철자를 C 이스케이프로 다시 써서 돌려준다(따옴표 · 역슬래시 · 제어 문자, UTF-8 바이트는 `\ooo` 팔진 — 실측).
         *          그것을 풀어 원본 바이트로 돌려준다.
         */
        static string readMarker( CXCursor cursor, const ReflectAnnotationDesc& desc );

        /**
         * @brief 다른 헤더의 선언 앞에 놓인 표식을 찾습니다 — 선언의 어휘 부모를 따라가 바로 앞 형제가 @p desc 의 표식인지 봅니다.
         * @details 대상 헤더의 선언은 `AstVisitor` 가 순회하며 짝짓는다. 이것은 칸 형식(열거형 · 컨테이너)처럼 순회 밖에서 만나는 선언용이다.
         * @return 표식 철자, 없으면 빈 문자열
         */
        static string findPreviousSiblingMarker( CXCursor declaration, const ReflectAnnotationDesc& desc );

        /** @brief 레코드 본문에 `REFLECT_BODY()` 표식(마커 함수의 자식 속성)이 있는지 봅니다. 정의를 봅니다(다른 헤더의 선언도). */
        static bool isReflectedRecord( CXCursor declaration );

        /**
         * @brief 애노테이션 토큰을 DTO 에 넣고, 필드 표(PredefinedAnnotationField.xxx)에 없는 토큰이 있으면 알립니다.
         * @details 모르는 토큰을 **조용히 버리지 않습니다** — 예: 에디터 색 선택기 요청의 철자는 `Meta = "Color"` 인데 `PROPERTY( …, Color, … )`
         *          로 적으면 버려지고, 멤버 이름에 color 가 들어 있으면 인스펙터의 이름 휴리스틱이 증상을 가립니다.
         * @return 모르는 토큰이 없으면 true
         */
        template <typename TParsed>
        [[nodiscard]] static bool applyAnnotation( const string_view spelling, TParsed& target, const string_view owner )
        {
            vector<string> listUnknownToken;
            AnnotationApply::apply( spelling, target, listUnknownToken );
            reportUnknownTokens( *getAnnotationScope<TParsed>()._pDesc, owner, listUnknownToken );
            return listUnknownToken.empty();
        }

    private:
        struct AttrSearch;
        struct SiblingSearch;
        static CXChildVisitResult attrSearchVisitor( CXCursor cursor, CXCursor, CXClientData data );
        static CXChildVisitResult stringLiteralVisitor( CXCursor cursor, CXCursor, CXClientData data );
        static CXChildVisitResult siblingSearchVisitor( CXCursor cursor, CXCursor, CXClientData data );
        static CXChildVisitResult bodyMarkerVisitor( CXCursor cursor, CXCursor, CXClientData data );
        /** @brief libclang 이 다시 쓴 문자열 리터럴 철자(`"…"`, C 이스케이프 · 팔진 `\ooo`)를 원본 바이트로 풉니다. 모양이 다르면 빈 문자열입니다. */
        static string unescapeLiteralSpelling( string_view spelling );
        static void   reportUnknownTokens( const ReflectAnnotationDesc& desc, string_view owner, const vector<string>& listUnknownToken );
    };
} // namespace sw
