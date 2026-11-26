// EXPECT-ERROR: is in none of
// TARGET-OPTION: -fmade-up-option-for-the-fixture
// 타깃 컴파일 옵션이 바꾸는 표 · 넘기는 접두어 · 버리는 접두어 어디에도 없으면 생성이 멈춘다 — 뜻 있는 새 옵션이 조용히 빠지지 않게.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct UnknownTargetOptionRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw::fixture
