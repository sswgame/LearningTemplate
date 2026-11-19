// EXPECT-ERROR: has the Local role in ReflectBuiltins.xxx
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // GameObjectHandle 은 생성 순서 id(표의 Local 역할)라 다른 실행의 세이브에서 엉뚱한 오브젝트를 가리킨다.
    REFLECT()
    struct SaveGameOnLocalHandleType
    {
        REFLECT_BODY();
        PROPERTY( SaveGame )
        GameObjectHandle _target;
    };
} // namespace sw::fixture
