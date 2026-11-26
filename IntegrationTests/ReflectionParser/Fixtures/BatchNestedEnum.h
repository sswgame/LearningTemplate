// EXPECT-GEN: struct Registrar<sw::fixture::BatchNestOwner>
// EXPECT-GEN: struct Registrar<sw::fixture::BatchNestOwner::Mode>
// EXPECT-GEN: "sw::fixture::BatchNestOwner::Mode", "ParserFixture"
// EXPECT-GEN: OldBatchNestMode
// EXPECT-GEN: struct Registrar<sw::fixture::BatchNestOwner::Inner>
// EXPECT-GEN: offsetof( Self, _innerMode )
// EXPECT-TYPE: sw::fixture::BatchNestOwner::Mode
// EXPECT-TYPE: sw::fixture::BatchNestOwner::Inner
// 형식 안의 중첩 ENUM · REFLECT 는 그 형식 본문 안의 표식으로 짝짓는다 — 표식 인자(Alias)가 바깥이 아니라 중첩 열거형에 붙는다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    class BatchNestOwner
    {
    public:
        REFLECT_BODY();

        ENUM( Alias = "OldBatchNestMode" )
        enum class Mode : uint8
        {
            First = 0,
            Second
        };

        REFLECT()
        struct Inner
        {
            REFLECT_BODY();
            PROPERTY()
            Mode _innerMode{ Mode::First };
        };

        PROPERTY()
        Mode _mode{ Mode::First };
        PROPERTY()
        Inner _inner{};
    };
} // namespace sw::fixture
