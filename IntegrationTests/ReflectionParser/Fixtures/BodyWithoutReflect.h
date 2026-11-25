// EXPECT-ERROR: REFLECT_BODY() is used in class/struct 'sw::fixture::BodyWithoutReflectRecord', but it lacks REFLECT()!
// REFLECT 와 REFLECT_BODY 는 서로 필수다 — 본문 표식만 있으면 등록기가 없어 StaticType() 이 링크에서야 멈춘다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    struct BodyWithoutReflectRecord
    {
        REFLECT_BODY();
        int32 _value{ 0 };
    };
} // namespace sw::fixture
