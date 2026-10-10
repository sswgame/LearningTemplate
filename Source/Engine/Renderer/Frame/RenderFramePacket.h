/**
 * @file RenderFramePacket.h
 * @brief 게임 스레드 → 렌더 스레드로 넘기는 프레임 제출 데이터입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PresentHookDelegate.h"
#include "Engine/Renderer/Frame/RenderView.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷입니다.
    struct RenderFramePacket
    {
        GPUSceneSnapshot _gpuScene; ///< GT 가 만든 것 모두. 소유를 함께 실음(GPUSceneSnapshot 참고)
        float4           _clearColor;
        float3           _cameraPos;
        float4x4         _viewProj;
        float4x4         _lightViewProj;
        /** @brief 그림자 셰이더 값(`g_ShadowParams`)입니다 — 그림자 행렬과 같은 볼륨에서 나온다(`DirectionalShadowProjection::computeShaderParams`). */
        float4 _shadowParams;
        /** @brief xyz = 빛이 나아가는 방향, w = 세기입니다. */
        float4 _lightDirIntensity;
        /** @brief rgb = 빛 색, a = 환경광입니다. */
        float4 _lightColorAmbient;
        /**
         * @brief 이 프레임의 **모든** 라이트(방향광 + 점광)입니다. 값으로 싣습니다.
         * @details 렌더 스레드는 씬을 볼 수 없으므로(`_pScene` 은 늘 null) 라이트도 패킷으로만 옵니다.
         *          위의 `_light*` 셋은 **키라이트 하나**로, 그림자 행렬과 앰비언트가 거기서 나옵니다.
         *          라이트 목록이 비어도 키라이트 하나로 그리는 폴백 경로이기도 합니다.
         */
        vector<GPULight> _listLight;
        /** @brief 이번 프레임의 추가 뷰(CCTV · 백미러 · 분할 화면 · PiP)입니다. 쉬는 뷰도 실린다(`RenderViewRequest::_bRender`). */
        vector<RenderViewRequest> _listView;
        /**
         * @brief 화면 2D(UI · 월드 글자) 그리기 목록과 글리프 아틀라스 업로드입니다. 렌더 스레드가 위젯을 볼 수 없으므로 이것만 넘어간다.
         * @details 렌더러가 자기 것과 바꿔치기로 받는다(`FrameRenderer::executePacket`) — 저장소가 돌아 다음 프레임에 다시 쓰인다.
         */
        CanvasFrameData _canvas;
        /** @brief 주 시점의 출력 사각형 · 해상도 배율 · 끌 기능 · 컷 표시입니다. */
        RenderViewSettings _mainView;
        /** @brief 비지 않으면 이 패킷을 그린 뒤 화면에 나간 그림을 이 경로에 PPM 으로 씁니다(자동화 시나리오의 `<Screenshot>`). */
        string           _screenshotPath;
        RHITextureHandle _outputRenderTarget; ///< 주 출력의 오프스크린 RT(에디터 게임 뷰 · 씬 뷰만 보일 때는 씬 뷰). 0 = 백버퍼 경로
        uint32           _viewportWidth;
        uint32           _viewportHeight;
        uint64           _frameIndex;
        uint8            _bHasViewProj : 1;
        uint8            _bValid       : 1;
        /** @brief 씬에 DirectionalLightComponent 가 있어 라이트 필드가 유효하면 1 입니다. */
        uint8                  _bHasLight : 1;
        [[maybe_unused]] uint8 _reserved  : 5;

        RenderFramePacket()
            : _gpuScene{}
            , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
            , _cameraPos{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] }
            , _viewProj{}
            , _lightViewProj{}
            , _shadowParams{}
            , _lightDirIntensity{}
            , _lightColorAmbient{}
            , _listView{}
            , _canvas{}
            , _mainView{}
            , _screenshotPath{}
            , _outputRenderTarget{ 0 }
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
         * @brief 새 프레임을 채우기 전에 값 필드를 기본값으로 되돌립니다. **저장소는 남깁니다.**
         * @details 패킷은 GT 의 스크래치 하나가 링 자리와 바꿔 가며 돕니다(`RenderThread::submit`). 그래서 이 객체에는 몇 프레임
         *          전의 값이 남아 있고, 조건부로만 쓰는 필드(빛 · 뷰-투영)는 여기서 지워야 합니다. 벡터 · 스냅샷은 비우기만 해
         *          용량이 남습니다.
         */
        void resetForFrame()
        {
            _clearColor        = float4{ 0.12f, 0.15f, 0.18f, 1.0f };
            _cameraPos         = float3{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] };
            _viewProj          = float4x4{};
            _lightViewProj     = float4x4{};
            _shadowParams      = float4{};
            _lightDirIntensity = float4{};
            _lightColorAmbient = float4{};
            _listLight.clear();
            _listView.clear();
            _canvas.clear();
            _mainView = RenderViewSettings{};
            _screenshotPath.clear();
            _outputRenderTarget = 0;
            _viewportWidth      = 0;
            _viewportHeight     = 0;
            _frameIndex         = 0;
            _bHasViewProj       = SW_FALSE;
            _bValid             = SW_FALSE;
            _bHasLight          = SW_FALSE;
        }
    };
} // namespace sw
