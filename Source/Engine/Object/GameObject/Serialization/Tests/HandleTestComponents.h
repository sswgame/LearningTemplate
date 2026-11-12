/**
 * @file HandleTestComponents.h
 * @brief 컴포넌트 핸들 직렬화 단위 테스트(`TestMissingComponentHandle.cpp`)가 다는 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 */
#pragma once
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 핸들이 가리키는 대상입니다. 테스트가 저장한 글에서 이 타입 이름을 바꿔 "모듈이 안 뜬 타입" 을 만든다. */
    REFLECT( HideInMenu )
    struct HandleTargetTestComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _value = 7;
    };

    /** @brief 같은 오브젝트의 `HandleTargetTestComponent` 를 타입 핸들로 가리킵니다. */
    REFLECT( HideInMenu )
    struct HandleHolderTestComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY()
        TComponentHandle<HandleTargetTestComponent> _target;
    };
} // namespace sw
