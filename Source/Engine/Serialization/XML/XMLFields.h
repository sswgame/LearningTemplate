/**
 * @file XMLFields.h
 * @brief XML 원소 하나의 속성을 리플렉션 구조체 하나로 읽고 쓰는 창구입니다(`readProperties` · `writeProperties`). 엔진 · 게임 데이터가 모두 이 길로 칸을 읽는다.
 * @details 원소마다 받는 칸 · 필수 · 범위 · 기본값은 그 구조체의 `PROPERTY` 가 정본이다 — 읽는 코드가 속성 이름을 손으로 늘어놓지 않는다.
 *          속성 하나하나의 손 읽기(스키마 구조체가 없는 값)는 `XMLReadUtil` 이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/TypeFqn.h"

#include <initializer_list>

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
    class XMLNode;

    /**
     * @brief 칸 오류 하나를 받는 곳입니다(@p pID 는 `"영역.사건"` 글자 상수). 주지 않으면 오류는 곧바로 로그에 쓰인다.
     * @details 오류를 모았다가 파일 끝에 한꺼번에 내는 읽기(`CharacterDataReader`)가 넘긴다 — 같은 오류가 로그와 목록에 두 번 나지 않는다.
     */
    struct XMLFieldsIssueSink
    {
        void ( *_pfnAdd )( void* pContext, const utf8* pID, string_view text ){ nullptr };
        void* _pContext{ nullptr };
    };

    /**
     * @struct XMLFields
     * @brief 리플렉션 스키마로 원소 속성을 읽고 쓰는 함수 묶음입니다.
     */
    struct SW_API XMLFields
    {
        /** @brief 속성 이름을 다른 읽기가 맡는지 답하는 함수입니다(`XMLNameCheck::AcceptNameFunction` 과 같은 모양). */
        using AcceptAttributeFunction = bool ( * )( const utf8* pName );
        /** @brief 칸 @p prop 의 속성 이름입니다(`readProperties` 의 이름 규칙 — 직접 적은 `Name` 은 그대로, C++ 이름은 앞의 `_` · 불리언 `b` 를 뗀다). */
        static string getAttributeName( const PropertyInfo& prop );
        /**
         * @brief 원소 @p node 의 속성을 리플렉션 Def(@p pInstance, @p typeInfo) 하나로 엄격하게 읽습니다 — 스키마(필드 표)가 기본값과 필수를 선언하고 이 함수가 로드 시점에 검증합니다.
         * @details 속성 이름은 PROPERTY 이름에서 앞의 `_` 를 뗀 것이고, 불리언은 `b` 도 떼고 첫 글자를 소문자로 쓴다(`_bShootOnSight` → `shootOnSight`, `_runningCost` → `runningCost`).
         *          적지 않은 칸은 Def 의 초기값(스키마 기본값)이고, `PROPERTY( Required )` 칸이 빠지면 오류다. 모르는 속성 · 읽지 못한 값 · `Min`/`Max` 밖 값도 이름과 함께 오류다.
         *          읽는 코드가 리터럴로 기본값을 채우거나 범위를 조용히 자르지 않게 하는 창구다(언리얼 DataTable 행 구조체 · Godot Resource 의 export 칸 자리).
         *          @p listHandRead 의 속성은 부르는 쪽이 따로 읽으므로 모르는 이름으로 보지 않는다(색 · 이름 목록처럼 칸 하나로 담지 않는 값).
         *          @p pfnHandRead 는 열린 이름 묶음(일정 조건 속성처럼 다른 읽기가 맡는 이름)을 답한다.
         * @return 오류가 하나도 없으면 true 입니다. 오류가 있어도 읽을 수 있는 칸은 채웁니다.
         */
        [[nodiscard]] static bool readProperties( const XMLNode& node, void* pInstance, const TypeInfo& typeInfo, string_view sourceName,
                                                  std::initializer_list<string_view> listHandRead = {}, AcceptAttributeFunction pfnHandRead = nullptr, const XMLFieldsIssueSink& sink = {} );
        /** @brief `readProperties` 의 타입 지정 버전입니다(`TDef::StaticType()`). */
        template <typename TDef>
        [[nodiscard]] static bool readProperties( const XMLNode& node, TDef& outDef, string_view sourceName, std::initializer_list<string_view> listHandRead = {},
                                                  AcceptAttributeFunction pfnHandRead = nullptr )
        {
            return readProperties( node, &outDef, *TDef::StaticType(), sourceName, listHandRead, pfnHandRead );
        }
        /**
         * @brief `readProperties` 의 짝입니다 — @p pInstance 의 칸을 같은 이름 규칙(앞의 `_` · 불리언 `b` 를 뗀다)의 속성으로 @p node 에 씁니다.
         * @details 필수 칸(`Required`)은 늘 쓰고, 나머지는 @p pDefaults(같은 타입의 기본 인스턴스)와 글이 다를 때만 쓴다 — 손으로 쓴 파일처럼 기본값을 생략한다.
         *          @p pDefaults 가 없으면 모든 칸을 쓴다(`writeEveryField`).
         *          컨테이너 칸은 속성 하나에 담기지 않아 쓰지 않는다(그런 칸은 부르는 쪽이 자식 원소로 쓴다).
         */
        static void writeProperties( const XMLNode& node, const void* pInstance, const void* pDefaults, const TypeInfo& typeInfo );
        /** @brief `writeProperties` 의 타입 지정 버전입니다. 기본값은 `TDef{}` 입니다. */
        template <typename TDef>
        static void writeProperties( const XMLNode& node, const TDef& def )
        {
            const TDef defaults{};
            writeProperties( node, &def, &defaults, *TDef::StaticType() );
        }
        /**
         * @brief 기본값과 같은 칸도 모두 씁니다(@p pDefaults 없는 `writeProperties`).
         * @details 물려받기 형식(머티리얼 부모 펼치기)은 적지 않은 칸이 부모 값을 뜻한다 — 기본값을 생략하면 자식의 기본값이 부모 값으로 바뀐다.
         */
        template <typename TDef>
        static void writeEveryField( const XMLNode& node, const TDef& def )
        {
            writeProperties( node, &def, nullptr, *TDef::StaticType() );
        }
        /**
         * @brief 적지 않음이 뜻을 가지는 열거 속성 @p pName 을 읽습니다(스키마 칸의 기본값으로 담지 못하는 경우 — 예: 액션의 `valueType` 은 적지 않으면 바인딩이 정한다).
         * @details 값은 열거자 이름(리플렉션 `ENUM()`)이다. 없으면 @p outValue 를 비우고 true, 모르는 이름이면 오류를 남기고 false 입니다.
         *          부르는 쪽은 이 속성을 `readProperties` 의 손 읽기 목록에 둔다.
         */
        template <typename E>
        [[nodiscard]] static bool readOptionalEnum( const XMLNode& node, const utf8* pName, optional<E>& outValue, string_view sourceName )
        {
            E value{};
            outValue.reset();
            bool bPresent = false;
            if ( readOptionalEnumText( node, pName, &value, typeFqn<E>(), sourceName, bPresent ) == false )
                return false;
            if ( bPresent )
                outValue = value;
            return true;
        }
        /** @brief `readOptionalEnum` 의 몸통입니다(@p pValue 는 열거 값 하나의 주소). */
        [[nodiscard]] static bool readOptionalEnumText( const XMLNode& node, const utf8* pName, void* pValue, const hashed_string& enumTypeFqn, string_view sourceName, bool& outbPresent );
    };
} // namespace sw
