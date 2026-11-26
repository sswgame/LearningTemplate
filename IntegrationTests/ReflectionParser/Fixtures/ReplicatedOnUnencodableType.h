// EXPECT-ERROR: Replicated field is not in the fixed layout encoding table
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // 전체 코덱은 길이 없는 vector 를 "이유 등록" 으로 넘기지만, Replicated 는 명시한 요청이라 생성 오류다.
    REFLECT()
    struct ReplicatedOnUnencodableTypeHost
    {
        REFLECT_BODY();
        PROPERTY( Replicated )
        int32 _health{};
        PROPERTY( Replicated )
        vector<int32> _listItem;
    };
} // namespace sw::fixture
