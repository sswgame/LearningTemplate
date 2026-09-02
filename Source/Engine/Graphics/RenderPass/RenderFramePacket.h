/**
 * @file RenderFramePacket.h
 * @brief Game Thread → Render Thread frame submission data.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RenderPass/GpuScene.h"

namespace sw
{
    struct RenderFramePacket;

    class IRHIDevice;
    class Material;

    SW_DECLARE_DELEGATE( void, PresentHookDelegate, IRHIDevice&, RenderFramePacket& );

    /// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷
    struct RenderFramePacket
    {
        GpuScene               _gpuScene;
        float4                 _clearColor;
        float3                 _cameraPos;
        float4x4               _viewProj;
        float4x4               _lightViewProj;
        Material*              _pSceneMaterial;
        RHITextureHandle       _gameRenderTarget; ///< 0 = backbuffer path
        uint32                 _viewportWidth;
        uint32                 _viewportHeight;
        uint64                 _frameIndex;
        uint8                  _bHasViewProj : 1;
        uint8                  _bValid       : 1;
        [[maybe_unused]] uint8 _reserved     : 6;

        RenderFramePacket()
            : _gpuScene{}
            , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
            , _cameraPos{ 0.0f, 1.2f, 3.2f }
            , _viewProj{}
            , _lightViewProj{}
            , _pSceneMaterial{ nullptr }
            , _gameRenderTarget{ 0 }
            , _viewportWidth{ 0 }
            , _viewportHeight{ 0 }
            , _frameIndex{ 0 }
            , _bHasViewProj{ SW_FALSE }
            , _bValid{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw
