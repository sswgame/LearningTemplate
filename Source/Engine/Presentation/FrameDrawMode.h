/**
 * @file Engine/Presentation/FrameDrawMode.h
 * @brief 이번 프레임을 그리는가(`FrameDrawMode`)입니다 — 렌더러 창구(`IEngineRenderDriver::submitFrame`)와 렌더 패킷이 함께 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 이번 프레임을 그리는가입니다. 정하는 쪽은 `SimulationOnlyFrameQueue::beginFrame` 하나다. */
    enum class FrameDrawMode : uint8
    {
        Draw,           ///< 그린다(Present 포함)
        SimulationOnly, ///< 패킷은 짓고 렌더 스레드가 상태(업로드)만 받는다 — 기록 · Present · 사진을 하지 않는다
    };
} // namespace sw
