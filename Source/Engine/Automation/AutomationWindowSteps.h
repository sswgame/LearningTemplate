/**
 * @file AutomationWindowSteps.h
 * @brief 자동화 시나리오의 창 단계(`PostWindowMessage` · `ExpectCursorClip` · `RequireForeground`) — 단계는 이 .cpp 의 정적 등록자가 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    /** @struct AutomationWindowSteps @brief 창 단계 묶음입니다. */
    struct AutomationWindowSteps
    {
        /**
         * @brief 아무 일도 하지 않습니다 — 실행기가 불러 이 .cpp 가 링크에서 빠지지 않게 합니다.
         * @details 배포본은 엔진을 정적 라이브러리로 한 exe 에 링크하므로 아무도 부르지 않는 오브젝트 파일의 정적 등록자는 빠진다.
         */
        static void ensureLinked();
    };
} // namespace sw
