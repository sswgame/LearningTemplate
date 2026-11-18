/**
 * @file Editor/Common/Workspace/Undo/EditorUndoEntry.h
 * @brief 포커스 창의 되돌리기 스택을 움직이는 입구 하나입니다 — Ctrl+Z · Ctrl+Y(`edit.undo` · `edit.redo`)와 History 창이 같은 판정을 거친다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct EditorUndoEntry
     * @brief 되돌리기 · 다시 하기 · 그 시점으로 가기 · 기록 지우기의 입구입니다. 편집 잠금(플레이 · 베이크 중 — `EditorPlaySession::areSceneEditsAllowed`)이면 거절하고,
     *        열린 편집(거래 · 상호작용)은 스택이 거절한다(`CommandStack::isEditOpen`).
     * @details 예전에는 History 창이 그리는 도중 스택을 바로 불러 편집 잠금을 건너뛰었다 — 베이크 중에도 기록 줄을 누르면 씬이 되돌아갔다(감사 E-2).
     *          언리얼 `UTransBuffer::CanUndo`(열린 트랜잭션 · PIE 면 거절)를 메뉴 · 단축키 · Undo History 창이 함께 묻는 것과 같은 자리다.
     *          대상 스택은 부르는 순간의 포커스 창의 것이다(`getFocusedCommandStack`). 거절하거나 스택이 없으면 false 다.
     *          **게임 스레드에서 부른다**(게임 단계 · 콘솔 · 자동화) — 되돌리기 실행은 씬을 바꾼다. UI 는 `edit.undo` · `edit.redo`(`EditorCommandEdit::History` — `request` 가
     *          게임 단계로 넘긴다)와 `EditorCommandRegistry::requestHistoryMove` 로 요청하고, 선택 갱신은 다음 UI 프레임 시작이 입힌다(`EditorSelectionUpdateQueue`).
     */
    struct SW_EDITOR_API EditorUndoEntry
    {
        static bool undo();
        static bool redo();
        /** @brief @p stepIndex 시점(0 은 처음 상태)으로 되돌리거나 다시 실행합니다. */
        static bool jumpTo( size_t stepIndex );
        /** @brief 기록을 모두 지웁니다. 열린 편집이 있으면 거절합니다 — 비우면 열린 거래가 쌓던 기록과 깊이를 함께 잃는다. */
        static bool clear();
    };
} // namespace sw::editor
