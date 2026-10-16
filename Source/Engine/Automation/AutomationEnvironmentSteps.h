/**
 * @file AutomationEnvironmentSteps.h
 * @brief 자동화 시나리오의 환경 단계(`DevCommand`, `ResizeWindow`)를 정의합니다. 단계는 이 .cpp 의 정적 등록자가 등록합니다(Shipping 은 Engine 을 통째로 링크한다 — `sw_linkWholeArchive`).
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    /** @struct AutomationEnvironmentSteps @brief 환경 단계 모음입니다. */
    struct AutomationEnvironmentSteps
    {
        /** @brief 개발 명령 단계의 엘리먼트 이름입니다. 이 단계가 든 시나리오는 개발 명령이 없는 구성(Shipping)에서 건너뜁니다(종료 코드 13). */
        static constexpr const utf8* kDevCommandKind = "DevCommand";
    };
} // namespace sw
