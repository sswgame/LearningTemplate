/**
 * @file DebugHUDSectionLines.h
 * @brief 디버그 HUD 섹션 하나의 줄을 모두 적는 곳입니다 — 줄 본문과 그 섹션의 감시 값 묶음. 게임 HUD · 에디터 씬 뷰 HUD · `hud.print` 가 함께 씁니다.
 * @details 감시 값 층(`Engine/Automation/Watch`)이 HUD 레지스트리(`Engine/Console`)보다 위라 둘을 잇는 곳이 여기다.
 */
#pragma once
#include "DevTools/DevToolsAPI.h"

namespace sw
{
    class DebugHUDSectionWriter;

    struct DebugHUDSectionRegistration;

    /** @struct DebugHUDSectionLines @brief 파일 머리말 참고. */
    struct SW_DEVTOOLS_API DebugHUDSectionLines
    {
        /** @brief @p section 의 줄 본문을 부르고 감시 값 묶음을 뒤에 적습니다(@p writer 는 부르는 쪽이 비운다). 게임 스레드에서 부릅니다. */
        static void write( const DebugHUDSectionRegistration& section, DebugHUDSectionWriter& writer );
    };
} // namespace sw
