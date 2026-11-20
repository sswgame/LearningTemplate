// EXPECT-ERROR: is a private or protected nested type, but its owner 'sw::fixture::PlainOwner' is not REFLECT
// 바깥 형식이 REFLECT 가 아니면 생성 등록기가 숨은 이름에 닿을 friend 가 없다 — 생성 단계에서 멈춘다(컴파일 접근 오류로 미루지 않는다).
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    class PlainOwner
    {
    private:
        REFLECT()
        struct HiddenRecord
        {
            REFLECT_BODY();
            PROPERTY()
            int32 _count{ 0 };
        };
    };
} // namespace sw::fixture
