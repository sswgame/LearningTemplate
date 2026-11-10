/**
 * @file DeclarationValidator.h
 * @brief 수집한 선언이 뜻이 맞는지 봅니다 — 어긋나면 그 헤더의 이름으로 알리고 false 입니다.
 * @details 생성 코드나 실행 중에 드러날 잘못을 생성 단계에서 멈춥니다. 고칠 곳은 늘 선언입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/ParsedReflection.h"

#include <clang-c/Index.h>

namespace sw
{
    struct ParserConfig;

    /** @brief 수집한 선언이 뜻이 맞는지 봅니다 — 어긋나면 그 헤더의 이름으로 알리고 false 입니다. */
    struct DeclarationValidator
    {
        /**
         * @brief 프로퍼티 애노테이션이 이름으로 가리키는 메서드가 있고 모양이 맞는지 봅니다(`RepNotify`).
         * @details 같은 타입이 선언한 메서드만 찾는다 — 생성 코드는 그 메서드를 직접 부르므로 이름이 틀리면 컴파일 오류가 엉뚱한 .gen.cpp 에서 난다.
         *          여기서 그 헤더의 이름으로 멈춘다. `RepNotify` 는 `void fn()` 또는 `void fn( const T& )`(T = 프로퍼티 타입)이다.
         */
        [[nodiscard]] static bool validateMemberFunctions( ParsedTypeInfo& type, const vector<ParsedMethodSignature>& listMethodSignature );

        /**
         * @brief 뜻이 모순인 프로퍼티 플래그 조합을 멈춥니다.
         * @details 기본값은 컨테이너에 둘 수 없고, 기반이 없는 타입의 `EditCondition` 은 그 타입의 프로퍼티를 가리켜야 한다.
         *          `RuntimeState` 는 저작 저장만 빼고 스냅샷 · 세이브에는 쓰는 필드다. `Transient`(어디에도 쓰지 않는다)와 함께면 어느 쪽이 이기는지
         *          선언만 보고 알 수 없고, `Required`(데이터 파일이 반드시 적는다)와 함께면 저작 파일이 쓰지 않는 칸을 필수로 요구한다.
         */
        [[nodiscard]] static bool validatePropertyFlags( const ParsedTypeInfo& type );

        /**
         * @brief 프로퍼티의 옛 이름(`PROPERTY( Alias = … )`)이 같은 타입의 지금 이름이나 다른 옛 이름과 겹치지 않는지 봅니다.
         * @details 이름 해시는 대소문자를 가리지 않으므로 비교도 그렇게 한다. 겹치면 저장된 칸이 어느 프로퍼티로 읽힐지 선언만 보고 알 수 없다.
         *          읽기는 지금 이름을 먼저 보지만(`SerializerUtil::findPropertyIndexByNameHash`), 그 순서에 기대는 선언은 여기서 멈춘다.
         */
        [[nodiscard]] static bool validatePropertyRedirects( const ParsedTypeInfo& type );

        /**
         * @brief 열거자 리다이렉트(`ValueAlias` · `Removed`)를 검사합니다. 틀리면 오류를 남기고 false 입니다.
         * @details 항목마다 `옛 이름:대상` 꼴이어야 하고, 대상은 지금 열거자여야 하며(대소문자까지 같게 적는다. 생성 코드가 그 이름으로 값을 읽는다),
         *          옛 이름은 지금 열거자가 아니어야 하고, 두 목록을 합쳐 한 번만 적는다. 대상이 없는 리다이렉트를 버리면 선언한 이름 변경이 말없이 사라진다.
         */
        [[nodiscard]] static bool validateEnumeratorRedirects( const ParsedEnumInfo& enumInfo );

        /**
         * @brief 표시 메타를 검사하고 정리합니다 — `Units` 는 단위 표에 있어야 하고(커스텀 메타 `Units` 로 싣는다), 표에 있는 단위를 `Meta = "Units=…"` 로
         *        적으면 거절하고, `EditCondition` 은 네 꼴 중 하나여야 하며, C 고정 배열의 원소는 컨테이너일 수 없습니다.
         * @details 여기서는 꼴만 본다. 이름은 `validatePropertyFlags`(기반이 없는 타입)와 생성된 판정 `PropertyMetadata::_pEditCondition` 의 컴파일이 본다.
         */
        [[nodiscard]] static bool applyDisplayMeta( ParsedPropertyInfo& prop, CXType fieldType, string_view owner );

        /** @brief `Interp` 는 시퀀서가 섞을 수 있는 타입에만 — 컨테이너 · 비트필드 · 그 밖의 타입은 그 헤더를 멈춥니다. */
        static bool isInterpAllowed( const ParsedPropertyInfo& prop, string_view owner );

        /**
         * @brief 서버 버스 본문 타입(`kBusVersion`)이 모듈 내보내기 매크로를 달았는지 봅니다. 아니면 알리고 false 입니다.
         * @details `ServerBusTopic<T>` 는 `T::StaticType()` 을 부르고, 그 정의는 `T` 를 선언한 모듈의 `.gen.cpp` 에 있다. 내보내기가 없으면
         *          다른 모듈(서버 키트)의 링크에서야 미해결 기호로 드러난다. C++ 형식으로는 막을 수 없다. 내보내기 여부를 보는 타입 특성이 없고,
         *          리눅스의 visibility 는 컴파일에 드러나지 않는다. 생성기는 AST 의 속성을 보므로 여기서 막는다.
         *          파서는 Base · 키트 매크로를 속성으로 읽는다(Windows 는 `parser_config` 가 `SW_GF_IMPORTS` · `SW_KIT_IMPORTS` 를 정의하고, 리눅스는 늘 visibility 다).
         *          모듈 경계를 넘지 않는 타깃(`--module-local`: 실행 파일 · 게임 모듈)은 보지 않는다.
         */
        [[nodiscard]] static bool validateBusBodyExport( const ParsedTypeInfo& type, bool bBusBody, bool bExported, bool bModuleLocal );

        /** @brief `REFLECT( TickMode = … )` 의 철자가 `Batch` · `Task` · `Main` 중 하나인지 봅니다(엔진 `TickMode` 열거자와 같은 철자 — 생성 코드가 그대로 쓴다). */
        [[nodiscard]] static bool validateTickMode( const ParsedTypeInfo& type );

        /**
         * @brief 멤버에 붙은 애노테이션이 REFLECT 타입 안에 있는지 봅니다. 아니면 알리고 false 입니다.
         * @param pMacroName 멤버 쪽 매크로 이름(오류 메시지용)
         */
        static bool isInsideReflectType( CXCursor member, const utf8* pMacroName, const ParserConfig& config );

    private:
        /** @brief 검증 함수(`void fn( ValidationContext& )`)가 그 이름으로 선언됐는지 봅니다. */
        static bool isValidatorDeclared( const vector<ParsedMethodSignature>& listMethodSignature, string_view name );
        /** @brief 이름이 같은 메서드 중 인자 수가 @p maxParameterCount 이하인 첫 것입니다. 없으면 nullptr 입니다. */
        static const ParsedMethodSignature* findMethodSignature( const vector<ParsedMethodSignature>& listMethodSignature, string_view name, size_t maxParameterCount );
        /** @brief 열거자 목록에서 이름(대소문자 무시)이 같은 열거자입니다. 없으면 nullptr 입니다. */
        static const ParsedEnumeratorInfo* findEnumeratorIgnoreCase( const ParsedEnumInfo& enumInfo, string_view name );
        /** @brief 식별자(영문 · 숫자 · `_`, 숫자로 시작하지 않는다)인지 봅니다. */
        static bool isIdentifier( string_view text ) noexcept;
        /** @brief `name` · `!name` · `name == Value` · `name != Value` 꼴인지 봅니다(값은 식별자 · 정수 · `Enum::Value`). */
        static bool isEditConditionWellFormed( string_view text );
        /** @brief 조건식이 보는 이름이 @p type 이 선언한 프로퍼티(C++ 이름)인지 봅니다. */
        static bool hasEditConditionSubject( const ParsedTypeInfo& type, string_view text );
        /** @brief @p type 이 부모를 가리키는 칸(`InheritParent`)을 선언했는지 봅니다. */
        static bool hasInheritParent( const ParsedTypeInfo& type );
    };
} // namespace sw
