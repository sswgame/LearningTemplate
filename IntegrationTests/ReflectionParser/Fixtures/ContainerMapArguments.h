// EXPECT-ERROR: needs 2 type argument(s) the parser can read
// 맵 컨테이너는 키 · 값 두 형식 인자를 clang 형식에서 읽는다 — 하나뿐이면 예전에는 키 · 값이 빈 채로 이어 갔다.
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT_CONTAINER( Map )
    template <typename T>
    class OneArgumentMap
    {
    public:
        vector<T> _list;
    };

    REFLECT()
    struct ContainerMapArgumentsOwner
    {
        REFLECT_BODY();
        PROPERTY()
        OneArgumentMap<int32> _map{};
    };
} // namespace sw::fixture
