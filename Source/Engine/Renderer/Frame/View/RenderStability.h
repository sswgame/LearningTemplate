/**
 * @file RenderStability.h
 * @brief 사진 앞 "렌더가 자리를 잡았다" 판정입니다 — 시간 누적 렌더(TAA 기록이 쌓인 정지 프레임 수) · 노출 적응 수렴(남은 노출 오차의 상한) · 에셋 일 없음.
 * @details 주 렌더러가 프레임마다 주 시점의 값을 올리고(`RenderStabilitySample::publish`, 렌더 스레드가 쓰고 게임 스레드가 읽는다), 감시 값 `Render.StillFrames` · `Render.ExposureErrorEv` ·
 *          `Render.Stable` 가 읽는다(`AutomationCaptureWatches.cpp`). 문턱은 엔진 설정 `EngineConfig::_renderStability` 이다(기본값 없음).
 *          - 정지 프레임: TAA 기록을 버리지 않았고(컷 · 첫 프레임 · TAA 가 빠진 뒤가 아님) 카메라 뷰-투영이 그대로이며 물체도 움직이지 않은 프레임이 이어진 수다.
 *            지터 누적이 수렴하려면 이 수가 기록 섞는 비율의 역수 몇 배는 되어야 한다(언리얼 TAA 의 히스토리 수렴, 자동화 스크린샷 앞 "warm-up frames").
 *          - 노출 오차: 기록을 버린 프레임은 측정값을 그대로 쓰므로 0 이고, 카메라가 움직인 프레임은 목표가 범위 끝까지 바뀔 수 있으므로 (최대 EV − 최소 EV) 로 되돌린다.
 *            정지 프레임마다 지수 적응의 남은 비율(1 − 다가가는 비율, 밝아짐 · 어두워짐 가운데 느린 쪽)을 곱한다 — GPU 값을 읽지 않는 상한이다.
 *            카메라가 서 있어도 화면 밝기가 바뀌는 일(불이 켜짐 · 물체 등장)은 보지 않는다 — 그 경우는 시나리오가 그 사건을 먼저 기다린다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 주 시점의 정착 값 한 벌입니다. */
    struct SW_API RenderStabilitySample
    {
        uint32  _stillFrameCount{ 0 };    ///< 이어진 정지 프레임 수(TAA 기록이 쌓인 프레임)
        float32 _exposureErrorEv{ 0.0f }; ///< 노출 적응의 남은 오차 상한(EV). 자동 노출을 쓰지 않으면 0

        /** @brief 렌더 스레드가 주 시점의 이번 값을 올립니다(엔진 범위 서브시스템이 든다 — `RenderStability.cpp`). */
        static void publish( const RenderStabilitySample& sample );
        /** @brief 게임 스레드가 마지막으로 올린 값을 읽습니다. */
        [[nodiscard]] static RenderStabilitySample readPublished();
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

    /** @brief `EngineConfig::_renderStability` 의 문턱 두 개입니다(판정 함수가 받는 모양). */
    struct RenderStabilityThreshold
    {
        uint32  _minStillFrames{ 0 };        ///< 정지 프레임이 이만큼 이어져야 한다
        float32 _maxExposureErrorEv{ 0.0f }; ///< 노출 오차 상한이 이 아래여야 한다
    };

    /** @struct RenderStabilityUtil @brief 정착 값의 갱신과 판정입니다(순수 함수 — 단위 테스트가 바로 부른다). */
    struct SW_API RenderStabilityUtil
    {
        /** @brief 이번 프레임의 정지 프레임 수입니다. 기록을 버렸거나 카메라 · 물체가 움직였으면 0, 아니면 하나 늘린다. */
        [[nodiscard]] static uint32 advanceStillFrames( uint32 previous, bool bTAAHistoryReset, bool bCameraStill, bool bObjectMotion );
        /**
         * @brief 이번 프레임의 노출 오차 상한(EV)입니다.
         * @param bReset 기록을 버린 프레임 — 측정값을 그대로 써 오차가 없다
         * @param bMoved 카메라가 움직였다 — 목표가 범위 끝까지 바뀔 수 있다
         * @param rangeEv 최대 EV − 최소 EV(0 이면 고정 노출이라 오차가 없다)
         * @param slowerAdaptRate 이번 프레임에 목표로 다가간 비율(밝아짐 · 어두워짐 가운데 작은 쪽, 0..1)
         */
        [[nodiscard]] static float32 advanceExposureError( float32 previous, bool bReset, bool bMoved, float32 rangeEv, float32 slowerAdaptRate );
        /** @brief 정지 프레임 · 노출 오차 · 남은 에셋 일(@p pendingJobCount)이 모두 문턱 안이면 true 입니다. */
        [[nodiscard]] static bool isStable( const RenderStabilitySample& sample, uint32 pendingJobCount, const RenderStabilityThreshold& threshold );
    };

} // namespace sw
