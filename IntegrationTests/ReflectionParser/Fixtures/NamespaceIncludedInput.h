// EXPECT-TYPE: sw::fixture::NamespaceIncludedOuterRecord
// EXPECT-TYPE: sw::fixture::wrapped::NamespaceWrappedRecord
// BATCH-WITH: Batch/NamespaceWrapped.h
// 대상 헤더(Batch/NamespaceWrapped.h)를 대상 밖 헤더(Batch/NamespaceWrapper.h)가 이름공간 안에서 include 한다. 순회는 대상 밖 이름공간을 내려가지 않지만,
// 대상을 include 한 자리를 품은 이름공간은 내려가야 그 형식을 놓치지 않는다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "Batch/NamespaceWrapper.h"

namespace sw::fixture
{
    REFLECT()
    struct NamespaceIncludedOuterRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw::fixture
