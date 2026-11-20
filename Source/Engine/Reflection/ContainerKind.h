/**
 * @file ContainerKind.h
 * @brief 리플렉션 컨테이너 종류 열거입니다. 프로퍼티 기술(`PropertyInfo.h`)이 값으로 들어 래퍼 템플릿(`ReflectionContainers.h`)과 나눴다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /// @brief 컨테이너 종류입니다. 목록은 PredefinedContainerKind.xxx 에 있습니다.
    enum class ContainerKind : uint8
    {
#define REGISTER_CONTAINER_KIND( Name ) Name,
#include "Core/Predefined/PredefinedContainerKind.xxx"

#undef REGISTER_CONTAINER_KIND
    };
} // namespace sw
