// EXPECT-GEN: sw::fixture::FixtureRecord
// EXPECT-GEN: sw::fixture::FixtureKind
// EXPECT-GEN: _health
// EXPECT-GEN: _level
// EXPECT-GEN: _bAlive
// EXPECT-GEN: _kind
// EXPECT-NOT-GEN: _notReflected
// EXPECT-TYPE: sw::fixture::FixtureRecord
// EXPECT-TYPE: sw::fixture::FixtureKind
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    ENUM()
    enum class FixtureKind : uint8
    {
        First = 0,
        Second
    };

    REFLECT()
    struct FixtureRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _health{ 0 };
        PROPERTY( Min = 0, Max = 10 )
        int32 _level{ 0 };
        PROPERTY()
        FixtureKind _kind{ FixtureKind::First };
        PROPERTY()
        uint8 _bAlive : 1;
        int32 _notReflected{ 0 };
    };
} // namespace sw::fixture
