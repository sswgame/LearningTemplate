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
         * @brief 형식의 정체 — 별칭 · 한정자를 벗긴 정규 형식의 선언 FQN 이고, 템플릿 특수화면 기본 템플릿의 FQN(`sw::AssetRef`)입니다. 레코드 · 열거형이 아니면 빈 문자열입니다.
         * @details 인라인 이름공간(`std::__1`)은 넣지 않는다 — 표(`typeidentity`)가 표준 라이브러리 구현과 무관하게 같은 이름을 쓰게.
         */
        static string getTypeIdentity( CXType type );
        /** @brief @p identity 가 표 @p arrIdentity 의 하나면 true 입니다. */
        template <size_t N>
        static bool isAnyIdentity( const string_view identity, const utf8* const ( &arrIdentity )[N] ) noexcept
        {
            for ( const utf8* pIdentity : arrIdentity )
            {
                if ( identity == pIdentity )
                    return true;
            }
            return false;
        }
        /** @brief 형식이나 그 템플릿 형식 인자 · 배열 원소(끝까지)의 정체가 @p identity 면 true 입니다(`vector<AssetRef<T>>` 은 AssetRef 를 든다). */
        static bool containsTypeIdentity( CXType type, string_view identity );
        /** @brief 문자 원소가 `char` 인 문자열 템플릿(`sw::string` · `std::string`)이면 true 입니다. 넓은 문자열은 아니다. */
        static bool isTextStringType( CXType type );
        /** @brief 이름 · 경로 칸 형식(글 · `typeidentity::kArrNameFieldType`)이거나 그 `vector` 면 true 입니다 — 형식의 정체로 가른다(소스 철자를 보지 않는다). */
        static bool isNameFieldType( CXType type );
        /** @brief 템플릿 특수화 @p type 의 @p index 번째 형식 인자를 **적힌 철자 그대로**(별칭 유지) 돌려줍니다. 없으면 빈 문자열입니다. */
        static string getTemplateArgumentSpelling( CXType type, uint32 index );

        /**
         * @brief 베이스 지정자(`: public X`)가 가리키는 **클래스** 선언입니다. 별칭(`using` · `typedef`)은 풀어 냅니다.
         * @details 적힌 타입의 선언을 바로 물으면 `using Base = Component; struct X : Base` 의 답은 별칭 선언이다 — 그러면 부모 FQN 이
         *          별칭 이름(`sw::Base`)이 되어 실행 중에 부모를 못 찾고(상속 병합 · 캐스트가 사슬 중간에서 멈춘다), 컴포넌트 판별도 별칭에서
         *          멈춰 팩토리가 생기지 않는다. 정규 타입(canonical)은 별칭을 벗긴 레코드 타입이다.
         */
        static CXCursor getBaseClassDeclaration( CXCursor baseSpecifier );

        /** @brief 열거형 선언의 열거자 이름 · 값을 @p outListEnumerator 에 더합니다. */
        static void collectEnumerators( CXCursor enumCursor, vector<ParsedEnumeratorInfo>& outListEnumerator );

        /**
         * @brief 개수 표식 열거자 이름입니다. `ENUM( Count = … )` 에 적은 이름이 있으면 그것, 없으면 끝 열거자가 `Count` 일 때 `Count`(비트 플래그 제외), 아니면 빈 글입니다.
         * @details 등록기(`AstVisitor`)와 고정 배치 코덱(`FixedTypeAnalyzer`)이 같은 규칙을 쓰도록 한 곳에 둔다 — 갈리면 한쪽만 개수를 안다.
         */
        static string resolveCountEnumerator( string_view annotatedCount, const vector<ParsedEnumeratorInfo>& listEnumerator, bool bIsBitFlag );

    private:
        /** @brief enumerator 이름·값을 수집합니다. */
        static CXChildVisitResult enumeratorCollectorVisitor( CXCursor cursor, CXCursor parent, CXClientData data );
    };
} // namespace sw
