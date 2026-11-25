// EXPECT-GEN: _layer
// EXPECT-GEN: _speed
// EXPECT-TYPE: sw::fixture::NameDefaultDeclaredType
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    // 이름 · 경로 칸의 C++ 문자열 기본값이 받아들여지는 꼴 — 엔진 기본값(DefaultName), 형식뿐인 초기값(Required), 저장하지 않는 칸(Transient), 빈 글, 이름 칸이 아닌 타입.
    REFLECT()
    struct NameDefaultDeclaredType
    {
        REFLECT_BODY();
        PROPERTY( DefaultName )
        string _layer{ "Default" };
        PROPERTY( Required )
        string _bone{ "root" };
        PROPERTY( Transient )
        string _cache = "scratch";
        PROPERTY( Tooltip = "Annotation text is not a default" )
        string _empty{ "" };
        PROPERTY()
        float32 _speed{ 1.0f };
    };
} // namespace sw::fixture
