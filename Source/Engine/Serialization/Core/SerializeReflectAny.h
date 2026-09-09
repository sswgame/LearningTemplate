/**
 * @file SerializeReflectAny.h
 * @brief `ReflectAny` 의 직렬화 — 인코딩은 Serialization 이 안다.
 *
 * @details `ReflectAny` 자체(타입 태그 + 바이트 버퍼)는 Reflection 의 타입이지만, 그 바이트를
 *          **어떻게 채우는지는 BinarySerializer 의 규약**이다. 그래서 `makeFrom` / `tryGetFrom` 의
 *          정의와 컨텍스트 핸들러 등록이 전부 이쪽(`SerializeReflectAny.cpp`)에 있다.
 *
 * @note 예전에는 이 코드가 `Reflection/ReflectAny.cpp` 에 있었고, `ReflectAny.h` 가
 *       `SerializeContext` 를 전방 선언했다. 그래서 **Reflection 이 Serialization 을 참조**했고
 *       둘이 서로를 참조하는 2-순환이 되어 티어 순서를 정할 수 없었다. 핸들러는 "둘 다 아는
 *       쪽" 이 갖는다 — 그 쪽이 Serialization 이다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    class SerializeContext;

    /** @brief SerializeContext에 ReflectAny 텍스트/바이너리 핸들러를 등록합니다. */
    SW_API void registerReflectAnyHandlers( SerializeContext& ctx );
} // namespace sw
