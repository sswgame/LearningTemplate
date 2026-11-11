/**
 * @file PropertyResolver.h
 * @brief 프로퍼티 · 함수 인자의 형식 철자(컴포넌트 핸들 · 에셋 참조)와 애노테이션이 이름으로 적은 상수를 값으로 풉니다.
 * @details 모양이 틀리면 오류를 남기고 false 입니다 — 조용히 빼면 그 칸은 저장 · 범위 검사 없이 넘어갑니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 프로퍼티 · 함수 인자의 형식 철자(컴포넌트 핸들 · 에셋 참조)와 애노테이션이 이름으로 적은 상수를 값으로 풉니다. */
    struct PropertyResolver
    {
        /**
         * @brief PROPERTY 에 컴포넌트 핸들이 있으면 저장할 수 있는 모양인지 확인하고 내장 `ComponentHandle` 로 정규화합니다. 아니면 오류를 남기고 false 입니다.
         * @details 저장할 수 있는 모양은 **타입 핸들 `TComponentHandle<T>` 한 값**, 또는 **그 시퀀스(한 겹, `vector` 등)** 입니다. 정규화하면 값 · 원소의 타입 이름은
         *          내장 `ComponentHandle` 이 되어 직렬화기 · 인스펙터가 기존 처리기를 그대로 쓰고, 대상 `T` 는 `_handleTargetTypeName` 에 실려
         *          읽은 뒤의 연결이 타입을 확인한다. 맵 · 중첩 컨테이너 · 타입 없는 `ComponentHandle` 은 대상 타입을 확인할 자리가 없어 생성 오류다.
         *          리플렉션 구조체의 필드도 이 검사를 지난다.
         */
        [[nodiscard]] static bool resolveComponentHandleProperty( ParsedPropertyInfo& prop, const string& owner );

        /**
         * @brief PROPERTY 에 타입 있는 에셋 참조(`AssetRef<T>`)가 있으면 내장 형식 `AssetRef` 로 정규화하고 경로 칸 메타를 켭니다. 쓸 수 없는 모양이면 오류를 남기고 false 입니다.
         * @details 값이 대상 종류를 품으므로(`AssetRefBase`) 처리기 하나가 모든 `T` 를 읽는다. 형식이 경로 칸임을 말하므로 `AssetPath` · `AssetType` 을 칸마다 적지 않는다 —
         *          프리로드 · 핫 리로드 알림 · 참조 칸 생성 목록이 이 메타를 본다. 받는 모양은 값 하나와 한 겹 시퀀스이고, 맵 · 집합 · 중첩은 고쳐 쓸 자리를 도구가 모른다.
         */
        [[nodiscard]] static bool resolveAssetRefProperty( ParsedPropertyInfo& prop, const string& owner );

        /**
         * @brief FUNCTION 인자에 타입 핸들(`TComponentHandle<T>`)이 있으면 내장 `ComponentHandle` 로 정규화하고 대상 `T` 를 싣습니다. 쓸 수 없는 모양이면 오류를 남기고 false 입니다.
         * @details 호출 쪽(인스펙터의 선택기 · 콘솔)은 타입 없는 핸들을 넘기고, 생성한 호출 썽크가 `ComponentHandleArgument::cast` 로 대상 타입을 확인해 바꾼다.
         *          확인에는 그 씬의 매니저가 들어서 **컴포넌트의 비정적 메서드**만 받는다(생성자 · 정적 함수 · 컴포넌트가 아닌 타입은 생성 오류다).
         *          컨테이너에 든 타입 핸들은 대상 타입을 확인할 자리가 없어 오류다. 타입 없는 `ComponentHandle` 은 예전처럼 그대로 둔다.
         */
        [[nodiscard]] static bool resolveComponentHandleParameters( ParsedFunctionInfo& method, bool bComponentOwner, const string& owner );

        /** @brief 애노테이션이 상수 이름으로 적은 값(`MaxCount` · `Min` · `Max`)을 선언에서 찾아 값으로 풉니다. */
        [[nodiscard]] static bool resolvePropertyConstants( CXCursor memberCursor, ParsedPropertyInfo& prop, string_view owner );

    private:
        /** @brief @p spelling 에 식별자 `ComponentHandle` 이나 `TComponentHandle` 이 낱말로 들어 있으면 true 입니다(`vector<sw::ComponentHandle>` 같은 템플릿 인자 포함). */
        static bool isComponentHandleSpelling( const string& spelling );
        /** @brief 정규화한 표기 @p name 이 @p templateOpen 으로 여는 템플릿(`TComponentHandle<대상>` · `AssetRef<대상>`)이면 true 이고, 대상 타입 이름을 @p outTarget 에 넣습니다. */
        [[nodiscard]] static bool tryReadTemplateTarget( const string& name, string_view templateOpen, string& outTarget );
        /** @brief @p text 안의 @p from 을 모두 @p to 로 바꿉니다. */
        static void replaceAllText( string& text, const string& from, const string& to );
        /** @brief 한 범위의 자식에서 이름 조각(`A::B::kX`)을 차례로 따라가 변수 선언을 찾는 상태입니다(`constantSearchVisitor`). */
        struct ConstantSearch;
        static CXChildVisitResult constantSearchVisitor( CXCursor cursor, CXCursor, CXClientData data );
        /** @brief @p scope 의 자식에서 @p listPart 를 따라간 변수 선언을 찾습니다. 없으면 null 커서입니다. */
        static CXCursor findConstantIn( CXCursor scope, const vector<string>& listPart );
        /**
         * @brief 상수 이름(`kMaxSlotCount`, `LedgerConstant::kMaxMemoSize`)이 가리키는 변수 선언을 찾습니다. 없으면 null 커서입니다.
         * @details `MaxCount` 와 `Min` · `Max` 가 같은 규칙으로 씁니다. 이름 찾기는 C++ 를 좁게 따릅니다. 필드가 든 타입과 그 바깥 타입을 안쪽부터 보고,
         *          그다음 바깥 네임스페이스 경로를 안쪽부터 붙여 번역 단위 맨 위에서 찾습니다(네임스페이스는 여러 블록에 걸쳐 열리므로 맨 위에서 모든 블록을 봅니다).
         *          기반 클래스 · using 은 보지 않습니다.
         */
        static CXCursor findNamedConstant( CXCursor memberCursor, string_view name );
        /**
         * @brief `MaxCount = 상수 이름` 의 상수를 선언에서 찾아 값을 `_maxCount` 에 넣습니다. 숫자로 적었으면(`_maxCountName` 이 비었으면) 할 일이 없습니다.
         * @details 상한의 기준을 C++ 상수 하나로 둡니다. 생성기가 그 값을 읽으므로 코덱 · 정규 문자열(스키마 해시) · `PropertyMetadata::_maxCount` 가
         *          모두 같은 수를 봅니다. 상수를 바꾸면 그 헤더를 읽는 생성물이 다시 만들어집니다(depfile). 이름 찾기는 `findNamedConstant` 입니다.
         *          찾지 못했거나 정수 상수가 아니거나 1 이상의 32 비트 값이 아니면 생성 오류입니다 — 상한을 조용히 빼면 그 필드는 코덱 없이 넘어갑니다.
         */
        [[nodiscard]] static bool resolveMaxCountName( CXCursor memberCursor, ParsedPropertyInfo& prop, string_view owner );
        /**
         * @brief `Min` · `Max` 글의 상수 이름 항(`kMaxLevel`, `-LedgerConstant::kMaxX`)을 상수의 값으로 바꾸고 숫자 경계를 채웁니다.
         * @details 필드 이름 항은 `_` 로 시작하므로(필드 이름 규칙) 그 밖의 이름이 상수입니다. 상수를 바꾸면 코덱 · 스키마 해시 · 인스펙터 범위가 함께 바뀝니다.
         *          정수나 실수 상수만 받고, 못 찾거나 숫자가 아니면 생성 오류입니다 — 경계를 조용히 빼면 범위 검사가 사라집니다.
         */
        [[nodiscard]] static bool resolveRangeConstants( CXCursor memberCursor, ParsedPropertyInfo& prop, string_view owner );
    };
} // namespace sw
