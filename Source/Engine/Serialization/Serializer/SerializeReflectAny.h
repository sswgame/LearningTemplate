/**
 * @file SerializeReflectAny.h
 * @brief `ReflectAny` 의 직렬화입니다. 인코딩은 Serialization 이 압니다.
 *
 * @details `ReflectAny` 자체(타입 태그 + 바이트 버퍼)는 Reflection 의 타입이지만, 그 바이트를
 *          **어떻게 채우는지는 BinarySerializer 의 규약**입니다. 그래서 `makeFrom` / `tryGetFrom` 의
 *          정의와 컨텍스트 핸들러 등록이 모두 이쪽(`SerializeReflectAny.cpp`)에 있습니다.
 *
 * @note 이 코드를 Reflection 쪽에 두면 **Reflection 이 Serialization 을 참조**해 두 티어가 서로를 참조하는
 *       순환이 됩니다. 핸들러는 "둘 다 아는 쪽" 이 갖습니다. 그쪽이 Serialization 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    class SerializeContext;

    /** @brief SerializeContext 에 ReflectAny 텍스트/바이너리 핸들러를 등록합니다. */
    SW_API void registerReflectAnyHandlers( SerializeContext& context );
} // namespace sw
