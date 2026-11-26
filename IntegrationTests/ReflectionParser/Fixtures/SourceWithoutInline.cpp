// EXPECT-ERROR: does not include "SourceWithoutInline.cpp.gen.inl"
// REFLECT 가 있는 .cpp 가 자기 산출물을 include 하지 않으면 생성 단계가 멈춘다. 주석 안의 include 는 읽지 않은 것이다:
// #include "SourceWithoutInline.cpp.gen.inl"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    struct SourceWithoutInlineRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int32 _value{ 0 };
    };
} // namespace sw::fixture
