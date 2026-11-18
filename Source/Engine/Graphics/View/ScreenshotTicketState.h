/**
 * @file Engine/Graphics/View/ScreenshotTicketState.h
 * @brief 스크린샷 요청 표 하나의 상태입니다(`ScreenshotRequestQueue::findResult` · `RenderThread::findScreenshotResult`).
 * @details 우편함 헤더(RHI 형식을 든다)와 떼어 둔다 — 결과만 읽는 쪽(에디터 플러그인의 썸네일 캡처)이 RHI 헤더를 전이로 보지 않게.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 요청 표 하나의 상태입니다. */
    enum class ScreenshotTicketState : uint8
    {
        Pending, ///< 아직 처리하지 않았다
        Written, ///< 파일을 썼다
        Failed,  ///< 처리했지만 쓰지 못했다(읽기 · 쓰기 실패, 줄이 넘쳤다)
        Unknown  ///< 받은 적이 없거나 결과 기록 창(`kResultHistoryCount`)을 벗어났다
    };
} // namespace sw
