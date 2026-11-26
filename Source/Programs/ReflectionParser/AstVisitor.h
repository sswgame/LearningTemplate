/**
 * @file AstVisitor.h
 * @brief libclang AST 순회 및 REFLECT/ENUM 메타데이터 수집
 * @details 수집 결과 타입은 ParsedReflection.h, 어노테이션 문자열 적용은 AnnotationApply.* 입니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"

#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"
#include "Programs/ReflectionParser/ParserSession.h"

#include <clang-c/Index.h>

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) AstVisitor — CXTranslationUnit 순회, REFLECT/ENUM 수집
    // ------------------------------------------------------------------------------
    /**
     * @brief libclang CXTranslationUnit 을 순회하며 REFLECT, ENUM 등을 헤더 단위로 수집합니다.
     * @details 선언은 적힌(매크로면 전개된) 파일의 것으로 셉니다. 대상 헤더 밖의 선언(include 된 엔진 · 표준 헤더)은
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

        /** @brief 대상 헤더 가운데 하나라도 오류가 났거나, 대상을 번역 단위에서 찾지 못했으면 true 입니다. */
        bool hasError() const noexcept { return _bHasError == SW_TRUE; }

        /** @brief 대상 헤더마다 모은 것입니다(생성자에 준 순서, 주 파일 하나면 원소 하나). */
        const vector<ParsedHeader>& getParsedHeaders() const noexcept { return _listHeader; }

    private:
        static constexpr int32 kNoTarget = constant::invalid_index::kInt32;

        static CXChildVisitResult visitCursor( CXCursor cursor, CXCursor parent, CXClientData clientData );
        /** @brief 대상 헤더의 글에서 `#if` 아래 놓인 리플렉션 매크로(REFLECT · ENUM · PROPERTY · FUNCTION · REFLECT_CONTAINER · REFLECT_BODY)를 찾아 알립니다. 없으면 true 입니다(W-R3 d). */
        bool validateConditionalReflectMacros( CXFile file, const string& path ) const;
        /** @brief 다듬은 줄이 줄 머리 리플렉션 매크로 호출(`NAME(` · `NAME (`)로 시작하면 true 입니다. */
        static bool startsWithReflectMacro( string_view line );
        /** @brief 커서가 적힌 파일이 몇 번째 대상인지 찾습니다. 대상이 아니면 kNoTarget 입니다. */
        int32 findTargetIndex( CXCursor cursor ) const;
        /** @brief 그 헤더를 실패로 표시합니다(산출물을 쓰지 않는다). 순회는 계속합니다. */
        void markHeaderError( ParsedHeader& header );
        /**
         * @brief @p cursor 의 바깥 형식 사슬에 public 이 아닌 고리가 있는지 @p outHidden 에 적습니다.
         * @details 생성 등록기(`sw::generated::Registrar<…>`)는 바깥 형식의 `REFLECT_BODY()` 가 주는 friend 로만 숨은 이름에 닿는다. 그래서 public 이 아닌 고리의 바깥 형식은
         *          REFLECT 여야 한다 — 아니면 알리고 false 입니다(생성 코드가 컴파일에서야 접근 오류로 멈추지 않게).
         */
        [[nodiscard]] bool resolveHiddenNested( CXCursor cursor, const string& fqn, bool& outHidden ) const;
        /**
         * @brief 형식 앞 표식(`REFLECT` · `ENUM` · `REFLECT_CONTAINER` 의 static_assert) 하나입니다. 다음에 방문하는 형제 선언과 짝짓습니다.
         * @details libclang 에 "앞 형제" API 가 없어 순회 순서로 짝짓는다 — 표식 다음 방문 커서가 같은 부모면 바로 다음 형제다.
         */
        struct PendingMarker
        {
            CXCursor                     _cursor{};
            CXCursor                     _parent{};
            string                       _spelling;
            const ReflectAnnotationDesc* _pDesc{ nullptr };
            int32                        _targetIndex{ kNoTarget };
        };
        /** @brief 표식 선언이면 짝을 기다리는 표식으로 둡니다. 리플렉션 표식이 아닌 static_assert 는 무시합니다. */
        void beginMarker( CXCursor cursor, CXCursor parent, int32 targetIndex );
        /** @brief 기다리던 표식을 꺼냅니다. 부모가 @p parent 와 다르면(범위가 닫혔다) 짝이 없다고 알리고 빈 표식을 돌려줍니다. */
        PendingMarker takePendingMarker( CXCursor parent );
        /** @brief 표식이 있는데 @p next 가 그 표식이 가리킬 선언이 아니면 알리고 그 헤더를 실패로 둡니다. 표식이 없으면 아무것도 하지 않습니다. */
        void reportUnpairedMarker( const PendingMarker& marker, CXCursor next );
        void reportMarker( const PendingMarker& marker, string_view reason );
        /** @brief `REFLECT_CONTAINER( 종류[, 래퍼] )` 의 인자가 맞는지 봅니다. 틀리면 알리고 그 헤더를 실패로 둡니다. */
        void validateContainerMarker( const PendingMarker& marker, CXCursor declaration );
        /** @brief struct · class 하나 — REFLECT 표식이 있으면 수집하고, 없는데 REFLECT_BODY 만 있으면 오류입니다. */
        void onRecordDeclaration( CXCursor cursor, const string* pReflectSpelling, ParsedHeader& outHeader );
        void onStructDeclaration( CXCursor cursor, const string& spelling, ParsedHeader& outHeader );
        void onEnumDeclaration( CXCursor cursor, const string& spelling, ParsedHeader& outHeader );
        /** @brief 열거자마다 이름 뒤의 `ENUMERATOR( … )` 를 읽어 @p inoutEnum 의 열거자에 넣습니다. 모르는 키가 있으면 false 입니다(알림은 이미 했다). */
        [[nodiscard]] bool applyEnumeratorAnnotation( CXCursor enumCursor, ParsedEnumInfo& inoutEnum ) const;
        /** @brief 열거형의 열거자 커서를 선언 순서로 모읍니다(`CursorReader::collectEnumerators` 와 같은 순서). */
        static CXChildVisitResult collectEnumeratorCursor( CXCursor cursor, CXCursor parent, CXClientData clientData );

    private:
        CXTranslationUnit      _translationUnit;
        const ParserSession*   _pSession;
        vector<string>         _listTargetPath;
        vector<CXFile>         _listTargetFile;
        vector<ParsedHeader>   _listHeader;
        mutable CXFile         _pLastFile;       ///< 직전에 본 파일 — 같은 헤더의 선언은 몰려 나온다
        mutable int32          _lastTargetIndex; ///< `_pLastFile` 의 답
        PendingMarker          _pendingMarker;   ///< 짝을 기다리는 표식(없으면 `_pDesc` 가 nullptr)
        uint8                  _bHasError : 1;
        [[maybe_unused]] uint8 _reserved  : 7;
    };
} // namespace sw
