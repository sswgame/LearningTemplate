/**
 * @file RHIDeviceCallbacks.h
 * @brief 장치가 부르는 콜백 별칭(GPU 해제 · 프레임 완료 통지 · 네이티브 문맥)과 완료 결과입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    template <typename T>
    class Delegate;

    /** @brief GPU 가 다 쓴 뒤에 부르는 자원 해제 콜백입니다(`RHIReleaseQueue`, `IRHIDevice::enqueueGPURelease`). */
    using RHIResourceReleaseDelegate = Delegate<void()>;
    /**
     * @enum RHIFrameCompletion
     * @brief GPU 완료 통지의 결과입니다(`IRHIDevice::notifyFrameCompletion`).
     * @details 해제(놓기)와 통지를 한 콜백이 겸하면 기다리지 못한 비우기(종료 · 잃은 장치 · 시간 초과)가 끝나지 않은 일을 끝났다고 알린다 — 그래서 통지는 결과를 받는다.
     */
    enum class RHIFrameCompletion : uint8
    {
        Completed, ///< GPU 가 그 프레임을 끝냈다 — 되읽기 · 구간 재사용을 해도 된다
        Abandoned, ///< 끝났는지 모른 채 큐를 비웠다(잃은 장치 · 시간 초과 · 종료) — 결과를 믿지 않는다
    };
    /** @brief GPU 가 그 프레임을 끝냈는지(또는 포기했는지) 알리는 콜백입니다(`IRHIDevice::notifyFrameCompletion`). */
    using RHIFrameCompletionDelegate = Delegate<void( RHIFrameCompletion )>;
    /** @brief 네이티브 컨텍스트를 그것을 지키는 잠금 안에서 받는 콜백입니다(`IRHIDevice::withNativeContext`). 인자는 콜백 안에서만 씁니다. */
    using RHINativeContextVisitor = Delegate<void( void* pContext )>;
} // namespace sw
