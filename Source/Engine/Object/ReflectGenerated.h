/**
 * @file ReflectGenerated.h
 * @brief ReflectionParser 가 만드는 .gen.cpp 와 builtins 생성 코드가 맨 앞에 include 하는 공통 머리 헤더입니다.
 * @details 생성 코드가 쓰는 헤더는 여기에서만 더하고 뺍니다. FileHeader.tpl / BuiltinFileHeader.tpl 은 이 파일을 include 합니다.
 *          생성 TU 전부가 이 파일을 읽으므로 생성 코드(템플릿 · CodeGenerator)가 실제로 쓰는 이름의 헤더만 둡니다. 원본 헤더가 쓰는
 *          이름은 원본 헤더가 직접 include 합니다(`GameObjectManager.h` 를 두면 생성 TU 마다 헤더 약 24 개를 더 읽습니다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Foundation/Foundation.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionDesc.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionUnits.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Serialization/Fixed/FixedFieldDesc.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

namespace sw
{
    class StateIO;
} // namespace sw

namespace sw::generated
{
    /**
     * @brief 형식에 `serializeState( StateIO& )` 가 있는지입니다 — `TypeMetadata::_bOverridesSerializeState` 가 된다(생성기가 이름 · 인자 수로 내리던 판정, `makeTypeDesc`).
     * @details `StateIO` 는 불완전 형식으로 충분하다(참조로만 넘긴다) — 생성 단위마다 `StateIO.h` 를 끌지 않는다. 물려받은 것도 센다.
     */
    template <typename T, typename = void>
    struct HasSerializeState : std::false_type
    {
    };

    template <typename T>
    struct HasSerializeState<T, std::void_t<decltype( std::declval<T&>().serializeState( std::declval<::sw::StateIO&>() ) )>> : std::true_type
    {
    };

    /** @brief `PROPERTY()` 칸의 선언 형식 @p TDecl 을 값으로 담아도 되는지 컴파일에서 봅니다(안 되면 여기서 멈춘다). 늘 true 를 돌려준다 — 한 줄의 `static_assert` 로 여러 칸을 부르게. */
    template <typename TDecl>
    constexpr bool isPropertyValueAllowed()
    {
        using TValue = std::remove_cv_t<std::remove_reference_t<TDecl>>;
        static_assert( std::is_base_of_v<::sw::Component, TValue> == false && std::is_base_of_v<::sw::GameObject, TValue> == false,
                       "GameObject or Component cannot be stored by value inside a PROPERTY(). Use a pointer, or a GameObjectHandle / ComponentHandle for a reference kept across frames." );
        return true;
    }

    /** @brief 타입 하나의 칸 전부입니다 — 생성 코드는 칸마다 줄을 내지 않고 타입마다 한 줄로 부른다. */
    template <typename... TDecl>
    constexpr bool arePropertyValuesAllowed()
    {
        return ( isPropertyValueAllowed<TDecl>() && ... );
    }

    /**
     * @brief 타입 서술자의 바탕입니다 — 크기 · 소멸과, 컴파일러가 아는 사실(틱 · 상태 함수 재정의 · 레지스트리 목록 표시)을 채운다(엔진이 주 틱을 켜는 판정 `HasOnTickOverride_v` 와 같은 답).
     * @details 틱 특성은 컴포넌트만 켠다. 나머지 칸(프로퍼티 표 · 생성 함수 …)은 생성 코드가 `TypeDesc::withX` 로 잇는다.
     */
    template <typename T>
    constexpr TypeDesc makeTypeDesc( const utf8* pName, const utf8* pFullyQualifiedName, const utf8* pParentFQN, const utf8* pModuleName )
    {
        uint32 flags = HasSerializeState<T>::value ? TypeDescFlag::kOverridesSerializeState : 0u;
        if constexpr ( std::is_base_of_v<::sw::Component, T> )
        {
            flags |= ::sw::HasOnTickOverride_v<T> ? TypeDescFlag::kOverridesTick : 0u;
            flags |= ::sw::HasOnSubTickOverride_v<T> ? TypeDescFlag::kOverridesSubTick : 0u;
            flags |= ::sw::HasRegistryList<T>::value ? TypeDescFlag::kRegisteredComponent : 0u;
        }
        return TypeDesc::of<T>( pName, pFullyQualifiedName, pParentFQN, pModuleName ).withFlags( flags );
    }
} // namespace sw::generated
