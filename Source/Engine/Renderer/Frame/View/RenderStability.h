/**
 * @file RenderStability.h
 * @brief 사진 앞 "렌더가 자리를 잡았다" 판정입니다 — 시간 누적 렌더(TAA 기록이 쌓인 카메라 정지 프레임 수) · 노출 적응 수렴(남은 노출 오차의 상한) · 실시간 GI 수렴(옛 기록의 남은 몫).
 * @details 주 렌더러가 프레임마다 주 시점의 값을 올리고(`RenderStabilitySample::publish`, 렌더 스레드가 쓰고 게임 스레드가 읽는다), 렌더 정착 조건 셋(`RenderTemporalAccumulation` ·
 *          `RenderAutoExposure` · `RenderRealtimeGI`, `RenderStability.cpp` 의 `SW_AUTOMATION_STABILITY_CONDITION`)과 진단 감시 값(`Render.StillFrames` · `Render.ExposureErrorEv` …)이 읽는다.
 *          사진 단계(`<Screenshot stability="Render">`)가 에셋 일과 이 조건을 기다린다. 문턱은 `Config/Dev/ScenarioRun.json` 의 `_conditions` 이다(배포 엔진 설정에 두지 않는다).
 *          - 정지 프레임: TAA 기록을 버리지 않았고(컷 · 첫 프레임 · TAA 가 빠진 뒤가 아님) 카메라 뷰-투영이 그대로인 프레임이 이어진 수다. 조건은 카메라 기준이다 — 캐릭터가 늘 숨 쉬는
 *            장면은 물체 움직임이 끝나지 않아 물체까지 보면 영영 "진행 중" 이었다. 고정 프레임 시간의 결정적 실행에서는 움직이는 장면도 같은 프레임이면 같은 그림이다.
 *            지터 누적이 수렴하려면 이 수가 기록 섞는 비율의 역수 몇 배는 되어야 한다(언리얼 TAA 의 히스토리 수렴, 자동화 스크린샷 앞 "warm-up frames").
 *            TAA 를 돌리지 않는 뷰(Forward · 후처리 꺼짐)는 해당 없음이다 — 기다릴 누적이 없다.
 *          - 노출 오차: 기록을 버린 프레임은 측정값을 그대로 쓰므로 0 이고, 카메라가 움직인 프레임은 목표가 범위 끝까지 바뀔 수 있으므로 (최대 EV − 최소 EV) 로 되돌린다.
 *            정지 프레임마다 지수 적응의 남은 비율(1 − 다가가는 비율, 밝아짐 · 어두워짐 가운데 느린 쪽)을 곱한다 — GPU 값을 읽지 않는 상한이다. 자동 노출을 재지 않는 뷰는 해당 없음이다.
 *            카메라가 서 있어도 화면 밝기가 바뀌는 일(불이 켜짐 · 물체 등장)은 보지 않는다 — 그 경우는 시나리오가 그 사건을 먼저 기다린다.
 *          - 실시간 GI 오차: 프로브 기록이 0 에서 시작하거나 입력(가리개 · 발광 · 빛 · 하늘)이 바뀐 뒤, 갱신마다 (1 − 섞는 비율)을 곱한 옛 기록의 남은 몫이다(볼륨 가운데 가장 큰 값 —
 *            `RealtimeProbeUpdater::computeError`). 변화는 멈춘 뒤의 프레임부터 세고, 이어지거나 되풀이하는 변화는 시작한 뒤부터 센다(`RealtimeProbeAccumulation`). 실시간 볼륨이 없으면 해당 없음이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Scenario/AutomationReadiness.h"

namespace sw
{
    /** @brief 주 시점의 정착 값 한 벌입니다. */
    struct SW_API RenderStabilitySample
    {
        uint32  _stillFrameCount{ 0 };    ///< 이어진 정지 프레임 수(TAA 기록이 쌓인 프레임)
        float32 _exposureErrorEv{ 0.0f }; ///< 노출 적응의 남은 오차 상한(EV). 자동 노출을 쓰지 않으면 0
        /// 되읽은 노출 적응의 지금 EV 와 목표 EV 의 차(절댓값, EV — `FrameAutoExposure` 가 GPU 기록을 몇 프레임 늦게 읽는다). 기록을 버린 뒤에도 장면 밝기가 바뀌면 다시 커진다.
        /// 자동 노출을 쓰지 않거나 아직 읽은 값이 없으면 0
        float32 _exposureAdaptationEv{ 0.0f };
        uint32  _giAccumulatedFrames{ 0 };          ///< 실시간 프로브 GI 기록이 지금 입력에 수렴한 프레임 수(`RealtimeProbeAccumulation`) — 정착 조건의 진척. 실시간 볼륨이 없으면 0
        float32 _giError{ 0.0f };                   ///< 실시간 프로브 GI 기록에 남은 옛 값의 몫(0 이면 다 바뀌었다). 실시간 볼륨이 없으면 0
        uint32  _cameraStillFrameCount{ 0 };        ///< 카메라만 서 있던 이어진 프레임 수(TAA 기록이 쌓인 프레임 — 물체 움직임은 보지 않는다)
        uint8   _bTemporalAccumulation{ SW_FALSE }; ///< 이번 프레임 주 시점이 TAA 나 볼륨 구름을 쌓았다(`FrameViewTarget::_bTAAHistoryWritten` · `_bSkyCloudHistoryWritten`). 아니면 TAA 조건은 해당 없음이다
        uint8   _bAutoExposure{ SW_FALSE };         ///< 이번 프레임 주 시점이 노출을 쟀다(`FrameViewTarget::_bAutoExposureThisFrame`). 아니면 노출 조건은 해당 없음이다
        uint8   _bRealtimeGI{ SW_FALSE };           ///< 이번 프레임에 실시간 프로브를 갱신했다(`RealtimeProbeUpdater::isActive`). 아니면 실시간 GI 조건은 해당 없음이다

        /** @brief 렌더 스레드가 주 시점의 이번 값을 올립니다(엔진 범위 서브시스템이 든다 — `RenderStability.cpp`). */
        static void publish( const RenderStabilitySample& sample );
        /** @brief 게임 스레드가 마지막으로 올린 값을 읽습니다. */
        [[nodiscard]] static RenderStabilitySample readPublished();
        /** @brief (렌더 스레드) 실시간 GI 조건의 문턱입니다 — 갱신기가 "이미 정착했다" 를 조건과 같은 값으로 본다. 시나리오가 문턱을 싣지 않았으면 0 이다(정착을 보는 쪽이 없다). */
        [[nodiscard]] static float32 readMaxGIError();
        /**
         * @brief TAA 정착 조건(`RenderTemporalAccumulation`)입니다 — TAA 를 돌리지 않으면 해당 없음, 카메라 정지 프레임이 @p minStillFrames 이상이면 준비됨, 아니면 진행 중(진척은 정지 프레임 수).
         * @details 카메라가 계속 움직이면 진척이 0 에 머물러 정지 시한(`_stallFrames`)에 진다 — 움직이는 카메라는 `stability="Assets"` 로 찍는다.
         */
        [[nodiscard]] AutomationReadiness evaluateTemporalAccumulation( uint32 minStillFrames ) const;
        /** @brief 노출 정착 조건(`RenderAutoExposure`)입니다 — 노출을 재지 않으면 해당 없음, 오차 상한이 @p maxErrorEv 이하면 준비됨, 아니면 진행 중(진척은 오차 상한의 비트). */
        [[nodiscard]] AutomationReadiness evaluateAutoExposure( float32 maxErrorEv ) const;
        /** @brief 실시간 GI 정착 조건(`RenderRealtimeGI`)입니다 — 실시간 볼륨을 갱신하지 않으면 해당 없음, 오차가 @p maxError 이하면 준비됨, 아니면 진행 중(진척은 쌓인 프레임 수). */
        [[nodiscard]] AutomationReadiness evaluateRealtimeGI( float32 maxError ) const;
    };

    /**
     * @struct RenderPresentCounter
     * @brief 렌더 스레드가 기록해 장치의 프레임 끝(Present 길 — `IRHIDevice::endFrame`)에 넘긴 프레임 수입니다(감시 값 `Render.PresentedFrames`).
     * @details 그리지 않고 진행한 프레임(`<AdvanceSimulation>`)은 세지 않는다. 최소화된 표면처럼 장치가 Present 를 건너뛰는 프레임은 센다 — 그 건너뛰기도 그리기 길이다.
     *          시나리오가 "그린 프레임 N 장" 을 고정 대기 대신 `<WaitUntil>`(앞서 `Store` 한 값 + N 이상)으로 기다린다(언리얼 `GFrameNumberRenderThread` 자리).
     */
    struct SW_API RenderPresentCounter
    {
        /** @brief (렌더 스레드) 프레임 하나를 장치의 프레임 끝에 넘겼다. */
        static void addOne();
        /** @brief (게임 스레드) 지금까지 넘긴 프레임 수입니다. 엔진을 다시 세우면 0 부터다. */
        [[nodiscard]] static uint64 read();
    };

    /** @struct RenderStabilityUtil @brief 정착 값의 갱신과 판정입니다(순수 함수 — 단위 테스트가 바로 부른다). */
    struct SW_API RenderStabilityUtil
    {
        /** @brief 이번 프레임의 정지 프레임 수입니다. 기록을 버렸거나 카메라 · 물체가 움직였으면 0, 아니면 하나 늘린다(카메라 정지 프레임은 @p bObjectMotion 을 false 로 넘긴다). */
        [[nodiscard]] static uint32 advanceStillFrames( uint32 previous, bool bTAAHistoryReset, bool bCameraStill, bool bObjectMotion );
        /**
         * @brief 이번 프레임에 시간 누적을 처음부터 다시 쌓는가 — 뷰 기록을 버렸거나, 이번 프레임에 TAA 를 돌리는데 그 기록이 버려졌다.
         * @details TAA 를 쓰지 않는 파이프라인 · 뷰(Forward 등)는 TAA 기록이 늘 "버림" 이라 그것만 보면 정지 프레임이 영원히 0 이었다(사진 앞 정착이 끝내 서지 않았다 — 지금은 그 뷰의 TAA 조건이 해당 없음이다).
         */
        [[nodiscard]] static bool isAccumulationReset( bool bHistoryReset, bool bTAAThisFrame, bool bTAAHistoryReset );
        /**
         * @brief 이번 프레임의 노출 오차 상한(EV)입니다.
         * @param bReset 기록을 버린 프레임 — 측정값을 그대로 써 오차가 없다
         * @param bMoved 카메라가 움직였다 — 목표가 범위 끝까지 바뀔 수 있다
         * @param rangeEv 최대 EV − 최소 EV(0 이면 고정 노출이라 오차가 없다)
         * @param slowerAdaptRate 이번 프레임에 목표로 다가간 비율(밝아짐 · 어두워짐 가운데 작은 쪽, 0..1)
         */
        [[nodiscard]] static float32 advanceExposureError( float32 previous, bool bReset, bool bMoved, float32 rangeEv, float32 slowerAdaptRate );
    };

} // namespace sw
