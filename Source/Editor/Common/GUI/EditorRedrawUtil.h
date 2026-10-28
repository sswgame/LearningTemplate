/**
 * @file EditorRedrawUtil.h
 * @brief 에디터가 이번 프레임 뒤에 곧바로 다음 프레임을 그려야 하는 까닭(`EditorRedrawReason`)을 모읍니다(`EditorAPI::getRedrawReasons`).
 * @details 호스트의 앱 루프는 까닭이 없는 프레임이 이어지면 창 메시지까지 잔다(`FramePacingState::EditorIdle`). 까닭을 빠뜨리면 그 화면이 다음 창 메시지
 *          (또는 깨움 상한 100 ms)까지 멈추므로, 시간으로 움직이는 것은 모두 여기 든다 — 시나리오 `editor.idle` 이 깨어나는 길을 본다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @struct EditorRedrawUtil @brief 다시 그릴 까닭을 모으는 정적 함수 모음입니다. */
    struct EditorRedrawUtil
    {
        /**
         * @brief ImGui 입력 · 잡은 항목 · 플레이 · 알림 · 패널의 연속 프레임 요청을 모아 `EditorRedrawReason` 비트 묶음으로 냅니다. 에디터 UI 갱신 뒤에 부릅니다.
         * @return 에디터 컨텍스트나 ImGui 컨텍스트가 아직 없으면(그릴 에디터 UI 가 없다) 0 입니다.
         */
        [[nodiscard]] static uint32 collectReasons();
    };
} // namespace sw::editor
