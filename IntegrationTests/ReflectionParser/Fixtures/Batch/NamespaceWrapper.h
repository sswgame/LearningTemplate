// 사례 NamespaceIncludedInput.h 의 짝 — 대상이 아닌 헤더가 대상 헤더를 이름공간 안에서 include 한다. 사례로 돌지 않는다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture::wrapped
{
#include "NamespaceWrapped.h"
} // namespace sw::fixture::wrapped
