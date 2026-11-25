// EXPECT-ERROR: needs a container kind (Sequence or Map)
// REFLECT_CONTAINER 의 종류 철자가 틀리면(Sequance) 칸에서 읽을 때 "컨테이너가 아님" 으로 조용히 바뀌었다 — 선언한 헤더에서 오류로 멈춘다.
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT_CONTAINER( Sequance )
    template <typename T>
    class BadKindSequence
    {
    public:
        vector<T> _list;
    };
} // namespace sw::fixture
