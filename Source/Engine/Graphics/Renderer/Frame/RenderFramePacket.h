/**
 * @file RenderFramePacket.h
 * @brief Game Thread → Render Thread frame submission data.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneSnapshot.h"

namespace sw
{
    struct RenderFramePacket;

    class IRHIDevice;

    SW_DECLARE_DELEGATE( void, PresentHookDelegate, IRHIDevice&, RenderFramePacket& );

    /// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷
    struct RenderFramePacket
    {
        GpuSceneSnapshot _gpuScene; ///< GT 가 만든 것 전부 — 소유를 함께 싣는다(GpuSceneSnapshot 참고)
        float4           _clearColor;
        float3           _cameraPos;
        float4x4         _viewProj;
        float4x4         _lightViewProj;
        /** @brief xyz = 빛이 나아가는 방향, w = 세기. */
        float4 _lightDirIntensity;
        /** @brief rgb = 빛 색, a = 환경광. */
        float4 _lightColorAmbient;
        /**
         * @brief 이 프레임의 **모든** 라이트 (방향광 + 점광). 값으로 싣는다.
         * @details 렌더 스레드는 씬을 볼 수 없으므로(`_pScene` 은 늘 null) 라이트도 패킷으로만 온다.
         *          위의 `_light*` 셋은 **키라이트 하나**로, 그림자 행렬과 앰비언트가 거기서 나온다 —
         *          라이트 목록이 비어도 예전과 같은 그림이 나오는 폴백 경로이기도 하다.
         */
        vector<GpuLight> _listLight;
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

        /**
         * @brief 새 프레임을 채우기 전에 값 필드를 기본값으로 되돌립니다 — **저장소는 남긴다.**
         * @details 패킷은 GT 의 스크래치 하나가 링 자리와 바꿔 가며 돈다(`RenderThread::submit`). 그래서 이 객체에는 몇 프레임
         *          전의 값이 남아 있고, 조건부로만 쓰는 필드(빛·뷰프로젝션)는 여기서 지워야 한다. 벡터·스냅샷은 비우기만 해
         *          용량이 남는다.
         */
        void resetForFrame()
        {
            _clearColor        = float4{ 0.12f, 0.15f, 0.18f, 1.0f };
            _cameraPos         = float3{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] };
            _viewProj          = float4x4{};
            _lightViewProj     = float4x4{};
            _lightDirIntensity = float4{};
            _lightColorAmbient = float4{};
            _listLight.clear();
            _gameRenderTarget = 0;
            _viewportWidth    = 0;
            _viewportHeight   = 0;
            _frameIndex       = 0;
            _bHasViewProj     = SW_FALSE;
            _bValid           = SW_FALSE;
            _bHasLight        = SW_FALSE;
        }
    };
} // namespace sw
