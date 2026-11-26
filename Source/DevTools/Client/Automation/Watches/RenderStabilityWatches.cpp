/**
 * @file RenderStabilityWatches.cpp
 * @brief 렌더 정착의 진단 감시 값 `Render.StillFrames` · `Render.ExposureErrorEv` · `Render.PresentedFrames` · `Render.ExposureAdaptation` · `Render.GIError` 입니다.
 * @details 주 렌더러가 올린 값(`RenderStabilitySample::publish`)을 그대로 낸다. 사진 앞 정착은 감시 값이 아니라 사진 단계가 기다리는 정착 조건이다(`RenderStability.cpp` 의
 *          `RenderTemporalAccumulation` · `RenderAutoExposure` · `RenderRealtimeGI` — 예전 합성 0/1 감시 값은 "해당 없음" 과 "아직" 을 같은 0 으로 냈다).
 *          값의 주인(`RenderStability`) 곁에서 등록한다 — 전용 서버는 `Renderer/` 를 짓지 않아 이 감시 값도 없다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Renderer/Frame/View/RenderStability.h"

namespace sw
{
    namespace
    {
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

        /** @brief `Render.PresentedFrames` 의 활동 수 — 그린 프레임 수 그 자체입니다. */
        [[nodiscard]] uint64 readPresentedActivity( const GameObjectManager* /*pManager*/ )
        {
            return RenderPresentCounter::read();
        }

        /** @brief 감시 값 `Render.ExposureAdaptation` 입니다. 되읽은 노출의 지금 EV 와 목표 EV 의 차(절댓값, EV)입니다 — 장면 밝기가 바뀐 뒤 적응이 남은 정도. 몇 프레임 늦다. */
        [[nodiscard]] bool readExposureAdaptation( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = static_cast<float64>( RenderStabilitySample::readPublished()._exposureAdaptationEv );
            return true;
        }

        /** @brief 감시 값 `Render.GIError` 입니다. 실시간 프로브 GI 기록에 남은 옛 값의 몫입니다(1 이면 막 시작했다). 실시간 볼륨을 갱신하지 않는 프레임은 값이 없습니다. */
        [[nodiscard]] bool readGIError( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const RenderStabilitySample sample = RenderStabilitySample::readPublished();
            outValue                           = static_cast<float64>( sample._giError );
            return sample._bRealtimeGI != SW_FALSE;
        }
    } // namespace

    SW_AUTOMATION_WATCH( renderStillFrames, "Render.StillFrames", "Frames in a row the main view kept its TAA history with the camera and objects still (temporal accumulation)",
                         &readStillFrames );
    SW_AUTOMATION_WATCH( renderExposureErrorEv, "Render.ExposureErrorEv", "Upper bound (EV) of how far auto exposure may still be from its target on the main view (0 = fixed exposure)",
                         &readExposureErrorEv );
    SW_AUTOMATION_WALL_WATCH( renderPresentedFrames, "Render.PresentedFrames",
                              "Frames the render thread recorded and handed to the device's end of frame (the Present path; frames advanced without drawing do not count)",
                              &readPresentedFrames, &readPresentedActivity );
    SW_AUTOMATION_WATCH( renderExposureAdaptation, "Render.ExposureAdaptation",
                         "Read-back |current EV - target EV| of auto exposure on the main view (a few frames late; 0 = adapted or fixed exposure)", &readExposureAdaptation );
    SW_AUTOMATION_WATCH( renderGIError, "Render.GIError", "Share of the old realtime probe GI history that is still mixed in (1 = just restarted, no value without a realtime volume)", &readGIError );
} // namespace sw

// Engine/Renderer/Frame/View/RenderStability.cpp 에서 옮긴 사진 앞 렌더 정착 조건(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        /** @brief 문턱이 없을 때의 진행 중입니다 — 기본값으로 지나가지 않고 정지 시한에 이 까닭으로 진다. */
        constexpr const utf8* kNoThresholdWhat = "no threshold - Config/Dev/ScenarioRun.json _mapConditionThreshold was not loaded";

        /** @brief 조건 `RenderTemporalAccumulation` 입니다(`RenderStabilitySample::evaluateTemporalAccumulation`). */
        AutomationReadiness readTemporalAccumulation()
        {
            const optional<uint32> minStillFrames = RenderStabilitySample::getMinStillFramesThreshold();
            if ( minStillFrames.has_value() == false )
                return AutomationReadiness::waiting( 0, kNoThresholdWhat );
            return RenderStabilitySample::readPublished().evaluateTemporalAccumulation( *minStillFrames );
        }

        /** @brief 조건 `RenderAutoExposure` 입니다(`RenderStabilitySample::evaluateAutoExposure`). */
        AutomationReadiness readAutoExposure()
        {
            const optional<float32> maxErrorEv = RenderStabilitySample::getMaxExposureErrorThreshold();
            if ( maxErrorEv.has_value() == false )
                return AutomationReadiness::waiting( 0, kNoThresholdWhat );
            return RenderStabilitySample::readPublished().evaluateAutoExposure( *maxErrorEv );
        }

        /** @brief 조건 `RenderRealtimeGI` 입니다(`RenderStabilitySample::evaluateRealtimeGI`). */
        AutomationReadiness readRealtimeGI()
        {
            const float32 maxError = RenderStabilitySample::readMaxGIError();
            if ( maxError <= 0.0f )
                return AutomationReadiness::waiting( 0, kNoThresholdWhat );
            return RenderStabilitySample::readPublished().evaluateRealtimeGI( maxError );
        }

        /** @brief `RenderTemporalAccumulation` 의 문턱 — 가만히 있던 프레임 수(1 이상 정수, TAA 기록 섞는 비율의 역수 몇 배). */
        expected<void> configureTemporalAccumulation( float64 threshold )
        {
            const uint32 minStillFrames = threshold >= 1.0 && threshold <= static_cast<float64>( MathUtil::kMaxInt32 ) ? static_cast<uint32>( threshold ) : 0u;
            if ( minStillFrames == 0 || static_cast<float64>( minStillFrames ) != threshold )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the threshold is a frame count (a whole number, 1 or more)", "RenderTemporalAccumulation" } );
            RenderStabilitySample::setMinStillFramesThreshold( minStillFrames );
            return {};
        }

        /** @brief `RenderAutoExposure` 의 문턱 — 남은 노출 오차(0 이상, EV). */
        expected<void> configureAutoExposure( float64 threshold )
        {
            if ( threshold < 0.0 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the threshold is an exposure error (0 or more, EV)", "RenderAutoExposure" } );
            RenderStabilitySample::setMaxExposureErrorThreshold( static_cast<float32>( threshold ) );
            return {};
        }

        /** @brief `RenderRealtimeGI` 의 문턱 — 옛 기록의 남은 몫(0 보다 크고 1 보다 작다). */
        expected<void> configureRealtimeGI( float64 threshold )
        {
            if ( threshold <= 0.0 || threshold >= 1.0 )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the threshold is the share of the old history left (between 0 and 1)", "RenderRealtimeGI" } );
            RenderStabilitySample::setMaxGIErrorThreshold( static_cast<float32>( threshold ) );
            return {};
        }
    } // namespace

    // 사진 앞 렌더 정착 — 이력을 든 렌더러 곁에서 등록한다(실행기는 조건 이름만 안다).
    SW_AUTOMATION_STABILITY_CONDITION( renderTemporalAccumulation, "RenderTemporalAccumulation", &readTemporalAccumulation, &configureTemporalAccumulation );
    SW_AUTOMATION_STABILITY_CONDITION( renderAutoExposure, "RenderAutoExposure", &readAutoExposure, &configureAutoExposure );
    SW_AUTOMATION_STABILITY_CONDITION( renderRealtimeGI, "RenderRealtimeGI", &readRealtimeGI, &configureRealtimeGI );
} // namespace sw
