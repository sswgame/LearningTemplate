/**
 * @file AutomationEnvironmentSteps.h
 * @brief 자동화 시나리오의 환경 단계(`DevCommand`, `ResizeWindow`)를 정의합니다. 단계는 이 .cpp 의 정적 등록자가 등록합니다(Shipping 은 Engine 을 통째로 링크한다 — `sw_linkWholeArchive`).
 */
#pragma once
#include "Core/Foundation/Macros.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @struct AutomationEnvironmentSteps @brief 환경 단계 모음입니다. */
    struct SW_DEVTOOLS_API AutomationEnvironmentSteps
    {
        /** @brief 개발 명령 단계의 엘리먼트 이름입니다. 이 단계가 든 시나리오는 개발 명령이 없는 구성(Shipping)에서 건너뜁니다(종료 코드 13). */
        static constexpr const utf8* kDevCommandKind = "DevCommand";
        /**
         * @brief 마지막 `ResizeWindow` 가 요청한 크기(창의 최소 크기로 자른 값)가 창에 반영됐으면 true 입니다. 요청이 없었으면 true 입니다.
         * @details 요청은 바로 돌아오지만 크기 통보는 플랫폼마다 늦을 수 있다(Win32 는 그 호출 안, X11 · Wayland 는 다음 메시지 처리). 그래서 뒤 단계가 이것으로 기다린다
         *          (`EditorWaitStable`). 반영되면 요청을 잊는다 — 그 뒤 사용자가 창을 끌어 바꾼 크기는 이 단계의 몫이 아니다.
         */
        [[nodiscard]] static bool isWindowResizeApplied();
    };
} // namespace sw
