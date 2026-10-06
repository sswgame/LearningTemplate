/**
 * @file ControlAutomationSteps.h
 * @brief 자동화 시나리오의 행동 층 단계 `<Intent>` · `<Possess>` — 입력 맵 없이 폰에 의도를 넣고 빙의를 옮깁니다(형식은 `Base/Control` README).
 */
#pragma once
#include "Core/Common/Macros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct ControlAutomationSteps
     * @brief 단계 등록자는 그 .cpp 의 정적 객체입니다 — Shipping 정적 링크에서 그 오브젝트 파일이 빠지지 않게 조종 시스템이 `ensureLinked` 를 부릅니다.
     */
    struct SW_GF_API ControlAutomationSteps
    {
        /** @brief 아무것도 하지 않습니다(링크를 붙드는 기호). */
        static void ensureLinked();
    };
} // namespace sw
