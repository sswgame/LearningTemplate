/**
 * @file ValidationTestComponent.h
 * @brief 오브젝트 검증 단위 테스트(`TestObjectValidation.cpp`)가 다는 컴포넌트입니다 — 개발 구성만 코드젠하고 "Add Component" 메뉴에 나오지 않는다.
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    /** @brief 검증 함수가 있는 컴포넌트입니다 — 오브젝트 로드 · 저장 · 인스펙터 편집 길을 본다. */
    REFLECT( HideInMenu, Validate = validateSpeed )
    struct ValidatedTestComponent : public Component
    {
        REFLECT_BODY();

        PROPERTY()
        float32 _speed = 1.0f;

        /** @brief 속도는 0 보다 크다. */
        void validateSpeed( ValidationContext& context ) const
        {
            if ( _speed <= 0.0f )
                context.addError( "speed must be positive" );
        }
    };
} // namespace sw
