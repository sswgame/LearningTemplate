/**
 * @file RHITransition.h
 * @brief 버퍼 · 텍스처 상태 전이와 서브리소스 범위입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @struct RHIBufferTransition
     * @brief 버퍼 하나의 상태 전이입니다(`IRHICommandList::transitionBuffers` 의 한 원소). 모으는 쪽은 `RHIBufferTransitionBatch` 다.
     */
    struct RHIBufferTransition
    {
        RHIBufferHandle _buffer{ 0 };
        RHIBufferState  _state{ RHIBufferState::Common };
    };

    /**
     * @struct RHISubresourceRange
     * @brief 텍스처의 밉 · 면(배열 칸) 구간입니다. 개수 `kAll` 은 "처음부터 끝까지" 다(기본값 — 자원 전체).
     * @details 언리얼 `FRHITransitionInfo` 의 MipIndex · ArraySlice(전부는 `AllSubresources`), Godot RD 의 슬라이스 범위와 같은 자리다.
     */
    struct RHISubresourceRange
    {
        static constexpr uint16 kAll = 0xFFFFu;

        uint16 _firstMip{ 0 };
        uint16 _mipCount{ kAll };
        uint16 _firstSlice{ 0 };
        uint16 _sliceCount{ kAll };

        /** @brief 밉 하나 · 면 하나입니다. */
        static constexpr RHISubresourceRange makeSingle( uint16 mip, uint16 slice ) { return RHISubresourceRange{ mip, 1u, slice, 1u }; }
    };

    /**
     * @struct RHITextureTransition
     * @brief 텍스처 하나의 상태 전이입니다(`IRHICommandList::transitionTextures` 의 한 원소). 그래프 레벨 프롤로그가 레벨의 장벽을 모은다.
     */
    struct RHITextureTransition
    {
        RHITextureHandle    _texture{ 0 };
        RHITextureState     _state{ RHITextureState::ShaderResource };
        RHISubresourceRange _range{}; ///< 옮길 밉 · 면(기본은 전체). 밉 하나를 그리며 다른 밉을 읽는 체인이 쓴다(R-10)
    };
} // namespace sw
