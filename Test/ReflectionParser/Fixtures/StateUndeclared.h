// EXPECT-ERROR: 'sw::fixture::StateUndeclaredType::_listEntry' has no state declaration
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // 상태 칸을 가진 형식에 선언 없는 값 멤버를 더하면 저장 · 되돌리기에서 조용히 빠진다 — 생성이 멈춘다.
    REFLECT()
    struct StateUndeclaredType
    {
        REFLECT_BODY();
        PROPERTY( RuntimeState )
        int32         _count{ 0 };
        vector<int32> _listEntry{};
    };
} // namespace sw::fixture
