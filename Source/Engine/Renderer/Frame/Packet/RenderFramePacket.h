/**
 * @file RenderFramePacket.h
 * @brief 게임 스레드 → 렌더 스레드로 넘기는 프레임 제출 데이터입니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Renderer/Frame/Capture/PresentHookDelegate.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/Light/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷입니다.
    struct RenderFramePacket
    {
        GPUSceneSnapshot _gpuScene; ///< GT 가 만든 것 모두. 소유권도 함께 실린다(GPUSceneSnapshot 참고)
        float4           _clearColor;
        /** @brief 주 시점의 눈 위치입니다. `_bHasViewProj` 가 0 이면 값이 없다(0 벡터) — 기본 카메라 위치로 채우지 않는다(그 패킷은 그리지 않는다). */
        float3   _cameraPos;
        float4x4 _viewProj;
        /**
         * @brief 이 프레임의 그림자 배치입니다. 아틀라스 슬롯, 주 시점의 방향광 캐스케이드(`_mainView`), 점광과 스폿 타일 및 그 라이트 버퍼 원소를 담습니다.
         * @details 주 시점 그림자는 그림자 빛이 없어도 유효합니다(그림자 없는 뷰). 씬을 그리지 않은 프레임만 비어 있고 렌더러가 그림자 없는 뷰로 둡니다. 렌더러가 맞바꾸기로 받습니다(저장소가 돕니다).
         */
        FrameShadowData _shadow;
        /** @brief xyz = 빛이 나아가는 방향, w = 세기입니다. */
        float4 _lightDirIntensity;
        /** @brief rgb = 빛 색, a = 환경광입니다. */
        float4 _lightColorAmbient;
        /**
         * @brief 이 프레임의 **모든** 라이트(방향광 + 점광)입니다. 값으로 싣습니다.
         * @details 렌더 스레드는 씬을 볼 수 없으므로 라이트도 패킷으로만 옵니다.
         *          위의 `_light*` 셋은 **키라이트 하나**로, 그림자 행렬과 앰비언트가 거기서 나옵니다.
         *          라이트 목록이 비어도 키라이트 하나로 그리는 폴백 경로이기도 합니다.
         */
        vector<GPULight> _listLight;
        /** @brief 이번 프레임의 추가 뷰(CCTV, 백미러, 분할 화면, PiP)입니다. 쉬는 뷰도 실립니다(`RenderViewRequest::_bRender`). */
        vector<RenderViewRequest> _listView;
        /**
         * @brief 화면 2D(UI, 월드 글자) 그리기 목록과 글리프 아틀라스 업로드입니다. 렌더 스레드가 위젯을 볼 수 없으므로 이것만 넘어갑니다.
         * @details 렌더러가 자기 것과 바꿔치기로 받습니다(`FrameRenderer::executePacket`). 저장소가 돌아 다음 프레임에 다시 쓰입니다.
         */
        CanvasFrameData _canvas;
        /** @brief 주 시점의 출력 사각형, 해상도 배율, 끌 기능, 컷 표시입니다. */
        RenderViewSettings _mainView;
        /** @brief 비지 않으면 이 패킷을 그린 뒤 화면에 나간 이미지를 이 경로에 PPM 으로 씁니다(자동화 시나리오의 `<Screenshot>`). */
        string _screenshotPath;
        /**
         * @brief 이번 프레임에 그릴 미리보기 씬 패킷입니다(앞 `_scenePreviewCount` 개만 유효). 패킷마다 씬 하나, 스냅샷 하나, 출력 타깃 하나입니다.
         * @details 저장소는 남깁니다. 링을 돌며 다시 채우므로 미리보기 패킷의 스냅샷과 라이트 목록이 프레임마다 새로 할당되지 않습니다.
         *          렌더 스레드는 씬 키(`_sceneKey`)마다 따로 둔 렌더러로 그립니다(`FrameRenderer::executeScenePreviewPacket`). 미리보기 패킷 안의 이 목록은 항상 비어 있습니다.
         */
        vector<unique_ptr<RenderFramePacket>> _listScenePreview;
        uint32                                _scenePreviewCount;
        /** @brief 이 패킷이 그리는 씬의 키입니다. 0(`kInvalidSceneKey`)이면 활성 씬(주 패킷), 아니면 미리보기 씬입니다. */
        uint32           _sceneKey;
        RHITextureHandle _outputRenderTarget; ///< 주 출력의 오프스크린 RT(에디터 게임 뷰, 씬 뷰만 보일 때는 씬 뷰). 0 = 백버퍼 경로
        uint32           _viewportWidth;
        uint32           _viewportHeight;
        uint64           _frameIndex;
        /** @brief `_viewProj` · `_cameraPos` 가 카메라에서 온 주 시점이면 1 입니다. 0 인 패킷은 그리지 않습니다(`FrameRenderer::executePacket` — 기본 카메라로 대신 그리지 않는다). `setMainView` 로만 켭니다. */
        uint8 _bHasViewProj : 1;
        uint8 _bValid       : 1;
        /** @brief 씬에 DirectionalLightComponent 가 있어 라이트 필드가 유효하면 1 입니다. */
        uint8                  _bHasLight : 1;
        [[maybe_unused]] uint8 _reserved  : 5;

        RenderFramePacket()
            : _gpuScene{}
            , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
            , _cameraPos{}
            , _viewProj{}
            , _shadow{}
            , _lightDirIntensity{}
            , _lightColorAmbient{}
            , _listView{}
            , _canvas{}
            , _mainView{}
            , _screenshotPath{}
            , _listScenePreview{}
            , _scenePreviewCount{ 0 }
            , _sceneKey{ 0 }
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
         * @brief 주 시점(카메라의 뷰-투영과 위치)을 한 번에 싣고 `_bHasViewProj` 를 켭니다.
         * @details 행렬 · 위치 · 플래그를 따로 쓰면 한쪽이 빠진 패킷이 생긴다(행렬 없이 플래그만, 위치는 기본값 그대로).
         */
        void setMainView( const float4x4& viewProj, const float3& cameraPos )
        {
            _viewProj     = viewProj;
            _cameraPos    = cameraPos;
            _bHasViewProj = SW_TRUE;
        }

        /**
         * @brief 새 프레임을 채우기 전에 값 필드를 기본값으로 되돌립니다. **저장소는 남깁니다.**
         * @details 패킷은 GT 의 스크래치 하나가 링 슬롯과 바꿔 가며 돕니다(`RenderThread::submit`). 그래서 이 객체에는 몇 프레임
         *          전의 값이 남아 있고, 조건부로만 쓰는 필드(빛, 뷰-투영)는 여기서 지워야 합니다. 벡터와 스냅샷은 비우기만 해
         *          용량이 남습니다.
         */
        void resetForFrame()
        {
            _clearColor = float4{ 0.12f, 0.15f, 0.18f, 1.0f };
            _cameraPos  = float3{};
            _viewProj   = float4x4{};
            _shadow.clear();
            _lightDirIntensity = float4{};
            _lightColorAmbient = float4{};
            _listLight.clear();
            _listView.clear();
            _canvas.clear();
            _mainView = RenderViewSettings{};
            _screenshotPath.clear();
            _scenePreviewCount  = 0;
            _sceneKey           = 0;
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
