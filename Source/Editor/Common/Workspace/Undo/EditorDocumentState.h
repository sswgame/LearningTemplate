/**
 * @file EditorDocumentState.h
 * @brief 편집 모델 하나의 문서 상태 한 벌입니다: 저장한 값(더러움 판정 · 저장한 값으로 되돌리기)과 창 안 되돌리기 기록.
 * @details 모델마다 `_savedText` 칸 · 되돌리기 스택 · "읽을 때 둘을 함께 비우기" 를 손으로 적었다. 한쪽을 빠뜨리면 읽은 뒤에도 옛 문서로 되돌아간다. 모델은 "어떻게 읽고 쓰나" 만 답한다.
 *          창별 기록과 전역 `CommandStack` 은 합치지 않는다(`EditSnapshotHistory.h` 머리말의 "창 안 Undo" — 언리얼은 전역 트랜잭션, Godot 은 문서별 기록이다. 기억에 의존).
 *          편집 알림을 깃발로만 받는 모델(`AudioEventEditModel` · `InputMapModel` · `DataTableModel` · `UIPreviewModel`)은 이 형식을 쓰지 않는다 — 글로 대조하려면 프레임마다 표를 직렬화해야 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Undo/EditSnapshotHistory.h"

namespace sw::editor
{
    /**
     * @class EditorDocumentState
     * @brief 저장한 값과 되돌리기 기록을 함께 듭니다. 새 문서를 읽으면(`reset`) 둘이 같이 바뀌어 옛 문서의 기록이 남지 않습니다.
     * @tparam TSnapshot 되돌리기 한 단계의 값입니다(`EditSnapshotHistory`).
     * @tparam TSaved 저장한 값의 형식입니다. 글이 정본인 문서는 `string`(기본)입니다. `isDirty` 를 쓰면 `!=` 가 있어야 합니다.
     */
    template <typename TSnapshot, typename TSaved = string>
    class EditorDocumentState
    {
    public:
        EditorDocumentState() = default;
        explicit EditorDocumentState( TSaved saved )
            : _saved{ std::move( saved ) }
            , _history{}
        {
        }

        /** @brief 문서를 새로 읽었습니다. 저장한 값이 @p saved 이고 되돌리기 기록을 비웁니다. */
        void reset( TSaved saved )
        {
            _saved = std::move( saved );
            _history.clear();
        }
        /** @brief 저장에 성공했습니다. 되돌리기 기록은 남습니다(저장 뒤에도 되돌릴 수 있다). */
        void               markSaved( TSaved saved ) { _saved = std::move( saved ); }
        const TSaved&      getSaved() const { return _saved; }
        [[nodiscard]] bool isDirty( const TSaved& current ) const { return current != _saved; }

        /** @brief 편집 직전 상태를 기록합니다(`EditSnapshotHistory::record`). */
        void               record( const TSnapshot& before, string_view coalesceKey = {} ) { _history.record( before, coalesceKey ); }
        void               endCoalescing() { _history.endCoalescing(); }
        [[nodiscard]] bool undo( TSnapshot& inoutCurrent ) { return _history.undo( inoutCurrent ); }
        [[nodiscard]] bool redo( TSnapshot& inoutCurrent ) { return _history.redo( inoutCurrent ); }
        bool               canUndo() const { return _history.canUndo(); }
        bool               canRedo() const { return _history.canRedo(); }

    private:
        TSaved                         _saved{};
        EditSnapshotHistory<TSnapshot> _history{};
    };
} // namespace sw::editor
