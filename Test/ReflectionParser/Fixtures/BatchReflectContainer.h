// BATCH-WITH: Batch/BatchOtherHeader.h
// EXPECT-GEN: struct Registrar<sw::fixture::BatchContainerOwner>
// EXPECT-GEN: "_list"
// EXPECT-GEN: ::sw::ContainerKind::Sequence
// 다른 헤더의 REFLECT_CONTAINER 형식(클래스 템플릿)은 그 선언 앞의 표식으로 컨테이너임을 안다 — 필드는 PROPERTY() 만 적는다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "Batch/BatchOtherHeader.h"

namespace sw::fixture
{
    REFLECT()
    struct BatchContainerOwner
    {
        REFLECT_BODY();
        PROPERTY()
        BatchSequence<int32> _list{};
    };
} // namespace sw::fixture
