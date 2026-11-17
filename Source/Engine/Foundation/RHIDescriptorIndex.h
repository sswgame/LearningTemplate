/**
 * @file RHIDescriptorIndex.h
 * @brief bindless 디스크립터 인덱스 형식(`RHIDescriptorIndex`)과 무효 값입니다.
 * @details 게임이 보는 헤더(`TextureCache.h` · `MaterialInstance.h`)가 텍스처의 안정 SRV 번호를 들고 다닌다. 타깃 게이트(Config/Rules/Layers/TargetLayers.toml)는 게임 쪽에서
 *          `Engine/Graphics/RHI` 를 막으므로, 정수 별칭 하나뿐인 이 형식을 RHI 밖 바닥 층으로 내렸다(언리얼 `FRHIDescriptorHandle` 의 인덱스를 머티리얼이 들고 다니는 자리).
 *          RHI 쪽은 `RHIHandles.h` 가 이 헤더를 include 해 그대로 쓴다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief bindless 리소스 인덱스입니다. 디스크립터 힙이나 세트 안의 바인딩 인덱스를 가리킵니다. */
    using RHIDescriptorIndex = uint32;

    /** @brief 유효하지 않은 디스크립터 인덱스입니다. */
    constexpr RHIDescriptorIndex kInvalidDescriptorIndex = constant::invalid_index::kUint32;
} // namespace sw
