// EXPECT-ERROR: holds a data path
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw::fixture
{
    /** @brief 맵 값의 데이터 경로는 옮기기 · 쿠킹이 고쳐 쓸 모양이 아니라 생성 오류다. */
    REFLECT()
    struct DataPathInMapType
    {
        REFLECT_BODY();
        PROPERTY()
        unordered_map<string, DataPath> _mapPath;
    };
} // namespace sw::fixture
