// EXPECT-GEN: _maxRange
// EXPECT-NOT-GEN: SW_SHIPPING
// EXPECT-NOT-GEN: _tooltip
// EXPECT-NOT-GEN: _uiMaxRange
// EXPECT-META: set._type._tooltip = "Split fixture"
// EXPECT-META: set.addProperty( ::sw::hashed_string( "_health" ) )
// EXPECT-META: p._tooltip = "Hit points"
// EXPECT-META: p._uiMaxRange
// EXPECT-META: TypeEditorMetadataRegistrar
// EXPECT-TYPE: sw::fixture::EditorMetadataSplitRecord
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

// 에디터 메타데이터(툴팁 · 슬라이더 범위)는 개발 전용 단위(.gen.meta.cpp)로만 가고, 등록 단위(.gen.cpp)에는 런타임 메타데이터(허용 범위)만 남는다.
namespace sw::fixture
{
    REFLECT( Tooltip = "Split fixture" )
    struct EditorMetadataSplitRecord
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Hit points", Min = 0, Max = 100, UIMax = 50 )
        int32 _health{ 0 };
    };
} // namespace sw::fixture
