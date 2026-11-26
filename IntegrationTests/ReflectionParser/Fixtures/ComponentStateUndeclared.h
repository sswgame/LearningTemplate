// EXPECT-ERROR: 'sw::fixture::UndeclaredStateComponent::_stamina' has no state declaration
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // 상태 칸이 없어도 컴포넌트의 값 멤버는 어느 쪽인지 적는다.
    REFLECT()
    class UndeclaredStateComponent : public Component
    {
    public:
        REFLECT_BODY();

    private:
        float32 _stamina{ 1.0f };
    };
} // namespace sw::fixture
