// EXPECT-GEN: using NestC0_3 = typename NestC0_2::value_type;
// EXPECT-GEN: static constexpr NestedContainerDesc kNested0_3{
// 컨테이너 중첩에 깊이 한도가 없다 — 네 겹(vector<vector<vector<vector<int32>>>>)도 단계마다 정적 서술자를 낸다(예전에는 세 겹 너머를 조용히 잘랐다).
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct NestedContainerDeepRecord
    {
        REFLECT_BODY();
        PROPERTY()
        vector<vector<vector<vector<int32>>>> _grid{};
    };
} // namespace sw::fixture
