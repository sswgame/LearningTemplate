/**
 * @file RenderFramePacket.h
 * @brief Game Thread → Render Thread frame submission data.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"

namespace sw
{
    struct RenderFramePacket;

    class IRHIDevice;
    class Material;

    SW_DECLARE_DELEGATE( void, PresentHookDelegate, IRHIDevice&, RenderFramePacket& );

    /// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷
    struct RenderFramePacket
    {
        GpuScene _gpuScene;
        float4   _clearColor;
        float3   _cameraPos;
        float4x4 _viewProj;
        float4x4 _lightViewProj;
        /** @brief xyz = 빛이 나아가는 방향, w = 세기. */
        float4 _lightDirIntensity;
        /** @brief rgb = 빛 색, a = 환경광. */
        float4           _lightColorAmbient;
        Material*        _pSceneMaterial;
        RHITextureHandle _gameRenderTarget; ///< 0 = backbuffer path
        uint32           _viewportWidth;
        uint32           _viewportHeight;
        uint64           _frameIndex;
        uint8            _bHasViewProj : 1;
        uint8            _bValid       : 1;
        /** @brief 씬에 DirectionalLightComponent 가 있어 라이트 필드가 유효하면 1. */
        uint8                  _bHasLight : 1;
        [[maybe_unused]] uint8 _reserved  : 5;

        RenderFramePacket()
            : _gpuScene{}
            , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
            , _cameraPos{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] }
            , _viewProj{}
            , _lightViewProj{}
            , _lightDirIntensity{}
            , _lightColorAmbient{}
            , _pSceneMaterial{ nullptr }
            , _gameRenderTarget{ 0 }
            , _viewportWidth{ 0 }
            , _viewportHeight{ 0 }
            , _frameIndex{ 0 }
            , _bHasViewProj{ SW_FALSE }
            , _bValid{ SW_FALSE }
            , _bHasLight{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw
