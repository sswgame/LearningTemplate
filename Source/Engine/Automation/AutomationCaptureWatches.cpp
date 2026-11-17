/**
 * @file AutomationCaptureWatches.cpp
 * @brief 캡처 준비 판정(`CaptureReadiness`)의 자동화 감시 값 `Assets.Pending` 과 사진 앞 정착 감시 값 `Render.StillFrames` · `Render.ExposureErrorEv` · `Render.Stable` · `Render.PresentedFrames` 입니다.
 * @details 정착은 주 렌더러가 올린 값(`RenderStabilitySample::publish`)과 에셋 일(`Assets.Pending` 과 같은 수)을 엔진 설정의 문턱(`EngineConfig::_renderStability`)으로 본다 — 스크린샷 앞
 *          고정 프레임 대기를 대신한다. 에셋 일은 바깥 일(워커 로드 · GPU)이라 `Render.Stable` 도 벽시계 감시 값이다.
 */
#include "pch.h"

#include "Engine/Automation/AutomationCaptureUtil.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Renderer/Frame/View/RenderStability.h"

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Assets.Pending` 입니다. 씬이 기다리는 에셋 일의 수입니다(`AutomationCaptureUtil::countPendingJobs`). 0 이면 화면이 다 읽힌 상태입니다. */
        [[nodiscard]] bool readAssetsPending( const GameObjectManager* pManager, float64& outValue )
        {
            outValue = static_cast<float64>( AutomationCaptureUtil::countPendingJobs( pManager ) );
            return true;
        }

        /** @brief 감시 값 `Render.StillFrames` 입니다. 주 시점에서 TAA 기록을 버리지 않고 카메라 · 물체가 서 있던 이어진 프레임 수입니다. */
        [[nodiscard]] bool readStillFrames( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderStabilitySample::readPublished()._stillFrameCount );
            return true;
        }

        /** @brief 감시 값 `Render.ExposureErrorEv` 입니다. 주 시점의 노출 적응 남은 오차 상한(EV)입니다. 자동 노출을 쓰지 않으면 0 입니다. */
        [[nodiscard]] bool readExposureErrorEv( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderStabilitySample::readPublished()._exposureErrorEv );
            return true;
        }

        /** @brief 감시 값 `Render.PresentedFrames` 입니다. 렌더 스레드가 장치의 프레임 끝(Present 길)에 넘긴 프레임 수입니다(`RenderPresentCounter`). */
        [[nodiscard]] bool readPresentedFrames( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderPresentCounter::read() );
            return true;
        }

        /** @brief 감시 값 `Render.Stable` 입니다. 정지 프레임 · 노출 오차 · 에셋 일이 모두 문턱 안이면 1 입니다(문턱은 `EngineConfig::_renderStability`). */
        [[nodiscard]] bool readRenderStable( const GameObjectManager* pManager, float64& outValue )
        {
            const RenderStabilityConfig&   config = engine::getSubsystem<EngineConfig>()._renderStability;
            const RenderStabilityThreshold threshold{ config._minStillFrames, config._maxExposureErrorEv };
            const bool                     bStable = RenderStabilityUtil::isStable( RenderStabilitySample::readPublished(), AutomationCaptureUtil::countPendingJobs( pManager ), threshold );
            outValue                               = bStable ? 1.0 : 0.0;
            return true;
        }
    } // namespace

    SW_AUTOMATION_WALL_WATCH( assetsPending, "Assets.Pending", "Meshes still loading, waiting for animation or dithering in, plus textures still loading or uploading and batches waiting for their material PSO (0 = settled)",
                              &readAssetsPending );
    SW_AUTOMATION_WATCH( renderStillFrames, "Render.StillFrames", "Frames in a row the main view kept its TAA history with the camera and objects still (temporal accumulation)",
                         &readStillFrames );
    SW_AUTOMATION_WATCH( renderExposureErrorEv, "Render.ExposureErrorEv", "Upper bound (EV) of how far auto exposure may still be from its target on the main view (0 = fixed exposure)",
                         &readExposureErrorEv );
    SW_AUTOMATION_WALL_WATCH( renderPresentedFrames, "Render.PresentedFrames",
                              "Frames the render thread recorded and handed to the device's end of frame (the Present path; frames advanced without drawing do not count)",
                              &readPresentedFrames );
    SW_AUTOMATION_WALL_WATCH( renderStable, "Render.Stable",
                              "1 once the main view is stable for a photo: enough still TAA frames, auto exposure converged and no asset job pending (thresholds: EngineConfig _renderStability)",
                              &readRenderStable );
} // namespace sw
