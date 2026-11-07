/**
 * @file CursorReader.h
 * @brief libclang 커서에서 글자 · 이름 · 열거자를 읽습니다.
 * @details 수집 단계 어디서나 쓰는 바닥입니다. 판단은 하지 않습니다 — 무엇을 모을지는 `AstVisitor` · `MemberCollector` 가 정합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include <clang-c/Index.h>

namespace sw
{
    struct ParsedEnumeratorInfo;

    /** @brief libclang 커서에서 글자 · 이름 · 열거자를 읽습니다. */
    struct CursorReader
    {
        /** @brief libclang 문자열(CXString)을 string 으로 복사한 뒤 해제합니다. */
        static string takeString( CXString cxString );

        /** @brief CXString 을 힙 할당 없이 비교한 뒤 해제합니다. */
        static bool isStringEqual( CXString cxString, string_view target );

        static string getCursorSpelling( CXCursor cursor );

        /** @brief 커서의 네임스페이스 포함 이름(FQN)을 만듭니다. 이름 없는 네임스페이스는 건너뜁니다. */
        static string makeFullyQualifiedName( CXCursor cursor );

        /** @brief 오류 메시지에 쓸 `타입::멤버` 이름입니다. */
        static string makeMemberOwnerName( string_view ownerFQN, string_view memberName );

        /**
         * @brief 베이스 지정자(`: public X`)가 가리키는 **클래스** 선언입니다. 별칭(`using` · `typedef`)은 풀어 냅니다.
         * @details 적힌 타입의 선언을 바로 물으면 `using Base = Component; struct X : Base` 의 답은 별칭 선언이다 — 그러면 부모 FQN 이
         *          별칭 이름(`sw::Base`)이 되어 실행 중에 부모를 못 찾고(상속 병합 · 캐스트가 사슬 중간에서 멈춘다), 컴포넌트 판별도 별칭에서
         *          멈춰 팩토리가 생기지 않는다. 정규 타입(canonical)은 별칭을 벗긴 레코드 타입이다.
         */
        static CXCursor getBaseClassDeclaration( CXCursor baseSpecifier );

        /** @brief 열거형 선언의 열거자 이름 · 값을 @p outListEnumerator 에 더합니다. */
        static void collectEnumerators( CXCursor enumCursor, vector<ParsedEnumeratorInfo>& outListEnumerator );

    private:
        /** @brief enumerator 이름·값을 수집합니다. */
        static CXChildVisitResult enumeratorCollectorVisitor( CXCursor cursor, CXCursor parent, CXClientData data );
    };
} // namespace sw
