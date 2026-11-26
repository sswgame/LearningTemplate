// EXPECT-ERROR: ENUM() is followed by 'MarkerNotBeforeTypeRecord'
// EXPECT-ERROR: REFLECT() ends its scope without a declaration after it
// 형식 앞 표식(REFLECT · ENUM)은 바로 다음 형제 선언의 것이다 — 종류가 맞지 않는 선언이나 범위 끝이 오면 조용히 버리지 않고 오류다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    ENUM()
    struct MarkerNotBeforeTypeRecord
    {
        int32 _value{ 0 };
    };

    REFLECT()
} // namespace sw::fixture
