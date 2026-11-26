// EXPECT-ERROR: a read check that ships; property validators are dev-only
// 타입 검증(`REFLECT( Validate )`)은 배포의 읽기 관문이다. 프로퍼티 검증은 배포에서 빠지므로 같은 타입에 두면 생성 오류다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class ValidationContext;
} // namespace sw

namespace sw::fixture
{
    REFLECT( Validate = validateState )
    struct PropertyValidateWithTypeValidate
    {
        REFLECT_BODY();

        PROPERTY( Validate = validateCount )
        int32 _count = 0;

        void validateState( ValidationContext& context ) const;
        void validateCount( ValidationContext& context ) const;
    };
} // namespace sw::fixture
