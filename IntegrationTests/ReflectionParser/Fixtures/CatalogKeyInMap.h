// EXPECT-ERROR: holds a catalog key
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

namespace sw::fixture
{
    struct FixtureMapEntryDef
    {
    };

    /** @brief 맵 값의 카탈로그 키는 쿠커가 표의 칸 줄로 찾을 모양이 아니라 생성 오류다. */
    REFLECT()
    struct CatalogKeyInMapType
    {
        REFLECT_BODY();
        PROPERTY()
        unordered_map<int32, CatalogKey<FixtureMapEntryDef>> _mapKey;
    };
} // namespace sw::fixture
