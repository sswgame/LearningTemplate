/**
 * @file AutomationCaptureWatches.cpp
 * @brief 캡처 준비 판정(`CaptureReadiness`)의 자동화 감시 값 `Assets.Pending` 과 사진 앞 정착 감시 값 `Render.StillFrames` · `Render.ExposureErrorEv` · `Render.Stable` 입니다.
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

        /** @brief 감시 값 `Render.ExposureAdaptation` 입니다. 되읽은 노출의 지금 EV 와 목표 EV 의 차(절댓값, EV)입니다 — 장면 밝기가 바뀐 뒤 적응이 남은 정도. 몇 프레임 늦다. */
        [[nodiscard]] bool readExposureAdaptation( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderStabilitySample::readPublished()._exposureAdaptationEv );
            return true;
        }

        /** @brief 감시 값 `Render.GIAccumulatedFrames` 입니다. 실시간 프로브 GI 기록을 0 에서 다시 쌓기 시작한 뒤 갱신한 그린 프레임 수입니다. */
        [[nodiscard]] bool readGIAccumulatedFrames( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderStabilitySample::readPublished()._giAccumulatedFrames );
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
    SW_AUTOMATION_WATCH( renderExposureAdaptation, "Render.ExposureAdaptation",
                         "Read-back |current EV - target EV| of auto exposure on the main view (a few frames late; 0 = adapted or fixed exposure)", &readExposureAdaptation );
    SW_AUTOMATION_WATCH( renderGIAccumulatedFrames, "Render.GIAccumulatedFrames", "Drawn frames the realtime probe GI history has accumulated since it was last reset (0 = no realtime volume)",
                         &readGIAccumulatedFrames );
    SW_AUTOMATION_WALL_WATCH( renderStable, "Render.Stable",
                              "1 once the main view is stable for a photo: enough still TAA frames, auto exposure converged and no asset job pending (thresholds: EngineConfig _renderStability)",
                              &readRenderStable );
} // namespace sw
