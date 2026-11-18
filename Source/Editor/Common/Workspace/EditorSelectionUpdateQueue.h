/**
 * @file EditorSelectionUpdateQueue.h
 * @brief 되돌리기 · 다시 하기가 선택에 남기는 고칠 일(지운 오브젝트 빼기 · 되살린 오브젝트 고르기)의 줄입니다 — 기록 쪽은 선택을 쓰지 않고 오브젝트 id 만 남기고, UI 가 선택에 입힙니다.
 * @details **선택은 되돌리기 기록 밖이다.** 선택은 UI 상태라 Ctrl+Z 가 클릭을 되감지 않고, 기록 · 되돌리기 실행(데이터 명령의 알림 `CommandStack::ObjectEditEvent`)은 선택을
 *          바로 고치지 않는다 — Godot 처럼 선택을 히스토리에 넣지 않는다(언리얼은 선택을 트랜잭션에 넣지만 `USelection` 이 UObject 라서다). 알림은 이 줄에 id 를 넣고,
 *          UI 프레임 시작(`ImGuiEditor::prepareFrameState`)과 UI 의 되돌리기 입구(`EditorUndoEntry`)가 `applyTo` 로 선택에 입힌다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    class EditorSelection;

    /**
     * @class EditorSelectionUpdateQueue
     * @brief 선택에 입힐 오브젝트 편집 알림의 줄입니다(값만). 서비스를 모른다 — 테스트가 그대로 몬다.
     */
    class SW_EDITOR_API EditorSelectionUpdateQueue
    {
    public:
        /** @brief 기록 쪽: 알림 @p event 를 줄에 넣습니다. 선택에 할 일이 없는 알림(`Modified`)은 넣지 않는다. */
        void push( const CommandStack::ObjectEditEvent& event );
        /**
         * @brief UI 쪽: 줄의 알림을 넣은 순서대로 @p selection 에 입히고 줄을 비웁니다. 지운 오브젝트는 선택에서 빼고(파괴는 지연이라 남겨 두면 인스펙터 · 기즈모가 잡는다),
         *        되살린 오브젝트는 그것만 고릅니다. 이미 사라진 대상은 선택의 죽은 칸을 걷는다(`pruneInvalid`).
         */
        void applyTo( EditorSelection& selection );
        /** @brief 입힐 알림이 없으면 true 입니다. */
        bool isEmpty() const { return _listPending.empty(); }

    private:
        vector<CommandStack::ObjectEditEvent> _listPending; ///< 넣은 순서
    };
} // namespace sw::editor
