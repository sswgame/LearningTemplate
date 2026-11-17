/**
 * @file ViewTextureMipBiasUtil.h
 * @brief 뷰의 머티리얼 텍스처 밉 바이어스입니다 — 렌더 해상도를 줄여 키우는(업스케일) 뷰가 텍스처를 출력 해상도만큼 선명하게 읽게 한다(감사 R-15).
 * @details 언리얼 `r.ViewTextureMipBiasUtil`(TSR · DLSS 가 log2(렌더 / 출력) 를 더한다), AMD FSR 1 의 권장(음수 밉 바이어스 log2(렌더 / 출력))과 같은 식이다.
 *          셰이더는 픽셀 단계 머티리얼 샘플에만 더한다(`MaterialTexture::sampleIndexTexture`, PassCB `g_TextureMipBias.x`).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    /** @struct ViewTextureMipBiasUtil @brief 렌더 높이와 출력 높이에서 밉 바이어스를 구하는 순수 계산입니다. */
    struct ViewTextureMipBiasUtil
    {
        /** @brief log2( @p renderHeight / @p outputHeight ) 입니다 — 같으면 0(골든 그대로), 줄여 그리면 음수다. 어느 쪽이 0 이면 0 입니다. */
        [[nodiscard]] static float32 compute( uint32 renderHeight, uint32 outputHeight )
        {
            if ( renderHeight == 0 || outputHeight == 0 || renderHeight == outputHeight )
                return 0.0f;
            return MathUtil::log2( static_cast<float32>( renderHeight ) / static_cast<float32>( outputHeight ) );
        }
    };
} // namespace sw
