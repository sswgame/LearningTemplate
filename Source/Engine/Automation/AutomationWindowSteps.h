/**
 * @file AutomationWindowSteps.h
 * @brief 자동화 실행기가 창 단계 구현(`AutomationWindowSteps.cpp`)에서 빌리는 것 — 자기 창을 전경으로 가져오기입니다.
 */
#pragma once

namespace sw
{
    /** @brief 자동화의 창 도우미입니다(전부 static). */
    struct AutomationWindowUtil
    {
        /**
         * @brief 활성 창을 전경으로 가져옵니다. 이미 전경이면 아무것도 하지 않습니다.
         * @return 이 호출 뒤 창이 전경이면 true 입니다. 창이 없거나 이 세션이 전경을 줄 수 없으면(원격 · 잠긴 화면 · 다른 창이 쥠) false, Windows 밖에서는 늘 false 입니다.
         */
        [[nodiscard]] static bool bringToForeground();
    };
} // namespace sw
