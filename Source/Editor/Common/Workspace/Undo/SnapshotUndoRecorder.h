/**
 * @file SnapshotUndoRecorder.h
 * @brief 편집 모델의 스냅샷 편집을 명령 스택(`CommandStack`)에 올리는 기록기입니다. `EditorDocumentState` 가 든다(형식 없는 절반 — 헤더가 스택 형식을 끌지 않는다).
 * @details 예전에는 모델마다 자기 스냅샷 스택(`EditSnapshotHistory`)을 들어 Ctrl+Z · History 창 · 바이트 한도 · 열린 상호작용 거절이 닿지 않았고, 창의 Undo 단추만 그 기록을 움직였다.
 *          지금은 모든 편집이 명령 스택의 명령이다 — 문서 패널의 모델은 그 문서의 스택(`EditorDocumentPanel::getDocumentCommandStack`)에, 묶지 않은 모델(테스트 · 도구)은 자기 스택에 올린다.
 *          언리얼 트랜잭션 버퍼 · 유니티 Undo 는 하나이고 Godot 은 문서마다 history 를 둔다 — 문서 스택 하나인 Godot 쪽과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class CommandStack;
} // namespace sw

namespace sw::editor
{
    class UndoStepOwner;

    /**
     * @class SnapshotUndoRecorder
     * @brief 명령 하나(되돌리기 · 다시 하기)를 스택에 올리고, 같은 키의 연속 편집(끌기)을 한 단계로 합칩니다. 사라지거나 비우면 자기 명령을 스택에서 뺍니다.
     */
    class SW_EDITOR_API SnapshotUndoRecorder final
    {
    public:
        SnapshotUndoRecorder();
        ~SnapshotUndoRecorder();
        SnapshotUndoRecorder( const SnapshotUndoRecorder& )            = delete;
        SnapshotUndoRecorder& operator=( const SnapshotUndoRecorder& ) = delete;

        /** @brief 이 뒤의 명령을 @p stack 에 올립니다(문서 패널의 스택). 기록이 없을 때 한 번 겁니다 — 스택은 이 기록기보다 오래 산다. */
        void bindStack( CommandStack& stack );
        /** @brief 명령이 올라가는 스택입니다. 걸지 않았으면 자기 스택을 처음 부를 때 만듭니다. */
        CommandStack& getStack();

        /**
         * @brief @p coalesceKey 가 비어 있지 않고 방금 올린 명령의 키와 같으며 그 명령이 스택의 맨 위면 true 입니다 — 새 명령 없이 그 명령이 이 편집까지 되돌린다.
         * @details 그 사이 다른 모델이 같은 스택에 올렸거나 되돌리기가 지나갔으면 합치지 않는다.
         */
        [[nodiscard]] bool canCoalesce( string_view coalesceKey );
        /** @brief 명령을 올립니다. @p heldBytes 는 단계가 쥔 자료의 바이트입니다(되돌리기 바이트 한도가 센다). */
        void push( string_view label, string_view coalesceKey, const Delegate<void()>& undo, const Delegate<void()>& redo, uint64 heldBytes );
        /** @brief 끌기가 끝났음을 알립니다. 다음 편집은 같은 키여도 새 명령입니다. */
        void endCoalescing();
        /** @brief 방금 올린 명령이 맨 위면 되돌리고 다시 하기에서도 지웁니다 — 편집을 거절해 없던 일로 할 때 씁니다. */
        void cancelLast();
        /** @brief 이 기록기가 올린 명령을 스택에서 모두 뺍니다(다른 모델의 명령은 남는다). */
        void clear();

        /** @brief 스택을 한 단계 움직입니다 — Ctrl+Z · Ctrl+Y 와 같은 길입니다. */
        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        [[nodiscard]] bool canUndo() const;
        [[nodiscard]] bool canRedo() const;

    private:
        unique_ptr<CommandStack>  _pOwnStack; ///< 걸지 않은 모델(테스트 · 도구)의 스택
        CommandStack*             _pStack;    ///< 명령을 올릴 스택. 걸거나 처음 쓸 때 정한다
        unique_ptr<UndoStepOwner> _pOwner;    ///< 올린 명령의 주인 — 바꾸면 앞 주인의 명령이 스택에서 빠진다
        string                    _lastCoalesceKey;
        size_t                    _lastIndex; ///< 방금 올린 명령 뒤의 스택 위치
    };
} // namespace sw::editor
