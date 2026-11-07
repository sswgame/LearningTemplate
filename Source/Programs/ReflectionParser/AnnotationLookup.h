/**
 * @file AnnotationLookup.h
 * @brief 커서에 붙은 애노테이션을 찾습니다 — 자식 속성(AnnotateAttr)이 원본이고, 소스 텍스트는 그것이 빠졌을 때의 폴백입니다.
 * @details 묻는 쪽은 애노테이션 한 벌(`ReflectAnnotationDesc` — 접두사와 매크로 철자)을 넘깁니다. 자식 속성 검색과 소스 폴백을 호출부마다 조합하지 않습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/AnnotationApply.h"
#include "Programs/ReflectionParser/AnnotationFields.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserSession.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 커서에 붙은 애노테이션을 찾습니다 — 자식 속성(AnnotateAttr)이 원본이고, 소스 텍스트는 그것이 빠졌을 때의 폴백입니다. */
    struct AnnotationLookup
    {
        /** @brief 자식 속성 중 접두사를 담은 첫 애노테이션의 철자입니다. 없으면 빈 문자열입니다. */
        static string findAnnotateAttr( CXCursor cursor, string_view prefix );

        /**
         * @brief AST 에서 속성 커서가 빠졌을 때, 커서 앞 소스 창에 그 애노테이션(매크로 또는 `annotate("…`)이 있는지 봅니다.
         * @details 기본 매크로만 봅니다. `ENUM;BitFlag` 같은 세부 태그에는 폴백하지 않습니다.
         */
        static bool hasSourceAnnotation( CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config );

        /**
         * @brief 커서 앞의 소스에서 매크로 괄호 `(...)` 안을 읽어 `PREFIX;args` 꼴의 애노테이션 철자를 다시 만듭니다.
         * @return 매크로가 없거나 앞 선언의 것이면 빈 문자열
         */
        static string extractSourceAnnotation( CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config );

        /** @brief 그 애노테이션이 붙어 있는지 — 자식 속성, 없으면 소스 창. */
        static bool hasAnnotation( CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config );

        /** @brief 애노테이션 철자 — 자식 속성, 없으면 소스에서 다시 만든다. 없으면 빈 문자열입니다. */
        static string readAnnotation( CXCursor cursor, const ReflectAnnotationDesc& desc, const ParserConfig& config );

        /**
         * @brief 레코드 선언에 `REFLECT()` 가 붙었는지 봅니다. 다른 헤더의 선언도 봅니다.
         * @details clang_getTypeDeclaration 이 반환하는 커서는 AnnotateAttr 자식 순회에서 빠지는 경우가 있어 정의 커서를 보고,
         *          (다른 어노테이션 검사와 마찬가지로) 소스 텍스트 폴백까지 함께 검사합니다.
         */
        static bool isReflectedRecord( CXCursor declaration, const ParserConfig& config );

        /**
         * @brief 애노테이션 토큰을 DTO 에 넣고, AnnotationMeta.txt 에 없는 토큰이 있으면 알립니다.
         * @details 모르는 토큰을 **조용히 버리지 않습니다** — 예: 에디터 색 선택기 요청의 철자는 `Meta = "Color"` 인데 `PROPERTY( …, Color, … )`
         *          로 적으면 버려지고, 멤버 이름에 color 가 들어 있으면 인스펙터의 이름 휴리스틱이 증상을 가립니다.
         * @return 모르는 토큰이 없으면 true
         */
        template <typename TParsed>
        [[nodiscard]] static bool applyAnnotation( const string_view spelling, TParsed& target, const ParserSession& session, const string_view owner )
        {
            vector<string> listUnknownToken;
            AnnotationApply::apply( spelling, target, session._annotationMeta, listUnknownToken );
            reportUnknownTokens( *getAnnotationScope<TParsed>()._pDesc, owner, listUnknownToken );
            return listUnknownToken.empty();
        }

    private:
        struct AttrSearch;
        static CXChildVisitResult attrSearchVisitor( CXCursor cursor, CXCursor, CXClientData data );
        /** @brief 파일 내용 캐시입니다. 한 헤더의 여러 커서가 디스크를 거듭 읽지 않게 합니다(스레드마다 한 벌). */
        static const string& getCachedFileContent( const string& path );
        /** @brief 커서 바로 앞의 소스 텍스트 창입니다. 길이는 parser_config 의 `tuning.source_lookback_bytes`(기본 512)입니다. */
        struct SourceWindow;
        static SourceWindow makeSourceWindow( CXCursor cursor, const ParserConfig& config );
        /** @brief 검색 구간 안에서 주석 밖에 있는 문자열을 뒤에서부터 찾습니다. */
        static size_t rfindOutsideComments( string_view window, string_view searchStr );
        /**
         * @brief 매크로와 커서 사이에 다른 선언의 경계(`;` `{` `}`)가 없는지 봅니다. 있으면 그 매크로는 앞 선언의 것입니다.
         * @details 따옴표 안(애노테이션 문자열)은 경계가 아닙니다 — 따옴표 안도 세면 `Tooltip = "…; …"` 하나로 그 타입의
         *          `REFLECT()` 가 "앞 선언의 것" 이 되어, 멤버마다 "REFLECT() 가 없다" 는 엉뚱한 오류로 빌드가 선다.
         */
        static bool hasNoDeclarationBoundary( string_view text );
        static void reportUnknownTokens( const ReflectAnnotationDesc& desc, string_view owner, const vector<string>& listUnknownToken );
    };
} // namespace sw
