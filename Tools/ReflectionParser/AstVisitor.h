/**
 * @file AstVisitor.h
 * @brief libclang AST 순회 및 REFLECT/ENUM 메타데이터 수집
 * @details 수집 결과 타입은 ParsedReflection.h, 어노테이션 문자열 적용은 AnnotationApply.* 입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/ParsedReflection.h"
#include "ReflectionParser/ParserSession.h"

#include <clang-c/Index.h>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) AstVisitor — CXTranslationUnit 순회, REFLECT/ENUM 수집
    // ------------------------------------------------------------------------------
    /**
     * @brief libclang CXTranslationUnit 을 순회하며 REFLECT, ENUM 등을 헤더 단위로 수집합니다.
     * @details 선언은 **적힌(매크로면 전개된) 파일**의 것으로 셉니다. 대상 헤더 밖의 선언(include 된 엔진 · 표준 헤더)은
     *          건너뜁니다. 대상은 번역 단위의 주 파일 하나이거나, 여러 헤더를 한 TU 로 묶었을 때의 그 헤더들입니다.
     */
    class AstVisitor
    {
    public:
        /**
         * @param listTargetFile 수집할 헤더 경로들. 비면 번역 단위의 주 파일 하나입니다. 결과는 이 순서로 나옵니다.
         */
        AstVisitor( CXTranslationUnit translationUnit, const ParserSession& session, const vector<string>& listTargetFile = {} );

        /** @brief AST 트리를 방문하며 리플렉션 정보를 수집합니다. 에러가 없으면 true를 반환합니다. */
        bool visit();

        bool hasError() const noexcept { return _bHasError == SW_TRUE; }

        /** @brief 대상 헤더마다 모은 것입니다(생성자에 준 순서, 주 파일 하나면 원소 하나). */
        const vector<ParsedHeader>& getParsedHeaders() const noexcept { return _listHeader; }

    private:
        static CXChildVisitResult visitCursor( CXCursor cursor, CXCursor parent, CXClientData clientData );
        /** @brief 커서가 적힌 파일이 몇 번째 대상인지 찾습니다. 대상이 아니면 kNoTarget 입니다. */
        int32 findTargetIndex( CXCursor cursor ) const;
        void  onStructDeclaration( CXCursor cursor, ParsedHeader& outHeader );
        void  onEnumDeclaration( CXCursor cursor, ParsedHeader& outHeader );

    private:
        static constexpr int32 kNoTarget = -1;

        CXTranslationUnit      _translationUnit;
        const ParserSession*   _pSession;
        vector<string>         _listTargetPath;
        vector<CXFile>         _listTargetFile;
        vector<ParsedHeader>   _listHeader;
        mutable CXFile         _pLastFile;       ///< 직전에 본 파일 — 같은 헤더의 선언은 몰려 나온다
        mutable int32          _lastTargetIndex; ///< `_pLastFile` 의 답
        uint8                  _bHasError : 1;
        [[maybe_unused]] uint8 _reserved  : 7;
    };
} // namespace sw
