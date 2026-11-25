// `BatchWindowOverflow.h` · `BatchReflectContainer.h` 의 짝 — 다른 헤더의 부모 · 열거형 · 컨테이너 선언을 묶음 안에서 찾는 길을 본다. 사례로 돌지 않는다.
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    ENUM( Flags )
    enum class BatchOtherFlags : uint8
    {
        None   = 0,
        First  = 1 << 0,
        Second = 1 << 1
    };

    REFLECT()
    struct BatchOtherBase
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _baseValue{ 0 };
    };

    /** @brief 직접 만든 순차 컨테이너 — 필드는 PROPERTY() 만 적는다(`REFLECT_CONTAINER`). */
    REFLECT_CONTAINER( Sequence )
    template <typename T>
    class BatchSequence
    {
    public:
        using value_type = T;
        vector<T> _list;
    };
} // namespace sw::fixture
