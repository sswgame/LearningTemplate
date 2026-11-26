// EXPECT-ERROR: has no State role in ReflectBuiltins.xxx
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // float4x4 는 내장 표에서 State 역할이 없다 — 상태 직렬화 · 상태 해시가 실행 중 FATAL 로 멈추던 것을 생성 단계에서 막는다.
    REFLECT()
    struct RuntimeStateUnsupportedType
    {
        REFLECT_BODY();
        PROPERTY( RuntimeState )
        float4x4 _transform{};
    };
} // namespace sw::fixture
