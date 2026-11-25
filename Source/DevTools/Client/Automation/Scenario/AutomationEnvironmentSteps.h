/**
 * @file AutomationEnvironmentSteps.h
 * @brief 자동화 시나리오의 환경 단계(`DevCommand`, `ResizeWindow`)를 정의합니다. 단계는 이 .cpp 의 정적 등록자가 등록합니다(Shipping 은 Engine 을 통째로 링크한다 — `sw_link_whole_archive`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
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
         * @brief 개발 명령 줄 @p line 의 `{gv_이름}` 을 그 전역 변수의 지금 값으로 바꿉니다. 없는 변수 · 닫히지 않은 자리면 오류입니다.
         * @details 실행마다 다른 값(띄운 서버의 주소 — 빈 포트를 받는다)을 시나리오에 글자로 적지 않는다: 띄우는 쪽이 `-gv_…` 로 준 값을 명령이 받는다.
         */
        [[nodiscard]] static expected<string> expandVariables( string_view line );
        /**
         * @brief 마지막 `ResizeWindow` 가 요청한 크기(창의 최소 크기로 자른 값)가 창에 반영됐으면 true 입니다. 요청이 없었으면 true 입니다.
         * @details 요청은 바로 돌아오지만 크기 통보는 플랫폼마다 늦을 수 있다(Win32 는 그 호출 안, X11 · Wayland 는 다음 메시지 처리). 그래서 뒤 단계가 이것으로 기다린다
         *          (`EditorWaitStable`). 반영되면 요청을 잊는다 — 그 뒤 사용자가 창을 끌어 바꾼 크기는 이 단계의 몫이 아니다.
         */
        [[nodiscard]] static bool isWindowResizeApplied();
    };
} // namespace sw
