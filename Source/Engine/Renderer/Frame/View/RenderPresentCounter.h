/**
 * @file RenderPresentCounter.h
 * @brief 렌더 스레드가 장치의 프레임 끝에 넘긴 프레임 수입니다(감시 값 `Render.PresentedFrames`).
 * @details 값은 수렴 값과 같은 엔진 범위 서브시스템이 든다. 정의는 `RenderConvergence.cpp` 에 있다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
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
} // namespace sw
