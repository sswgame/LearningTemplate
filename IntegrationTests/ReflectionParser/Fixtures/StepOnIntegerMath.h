// EXPECT-ERROR: Step applies to float32 fields (and float32 math types) only
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // int2 는 고정 배치 수학 표에서 정수 성분이라 float32 수학 타입처럼 Step 을 받지 않는다.
    REFLECT()
    struct StepOnIntegerMathType
    {
        REFLECT_BODY();
        PROPERTY( Step = 1, Min = 0, Max = 10 )
        int2 _cell{};
    };
} // namespace sw::fixture
