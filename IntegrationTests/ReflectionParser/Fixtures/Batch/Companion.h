// 사례 헤더의 기본 짝이다(`RunParserFixture.cmake`) — 사례마다 입력 둘을 한 번역 단위로 묶는 빌드의 길(`parseBatch`)을 지나게 한다. 사례로 돌지 않는다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture::companion
{
    REFLECT()
    struct CompanionRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw::fixture::companion
