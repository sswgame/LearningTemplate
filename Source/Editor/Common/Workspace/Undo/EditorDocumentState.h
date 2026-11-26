/**
 * @file EditorDocumentState.h
 * @brief 편집 모델 하나의 문서 상태 한 벌입니다: 저장한 값(더러움 판정 · 저장한 값으로 되돌리기)과 창 안 되돌리기 기록.
 * @details 모델마다 `_savedText` 칸 · 되돌리기 스택 · "읽을 때 둘을 함께 비우기" 를 손으로 적었다. 한쪽을 빠뜨리면 읽은 뒤에도 옛 문서로 되돌아간다. 모델은 "어떻게 읽고 쓰나" 만 답한다.
 *          되돌리기 기록은 명령 스택(`CommandStack`)이다(`EditSnapshotHistory`) — 모델은 생성자에서 `bind` 로 지금 값을 만들고 넣는 법을 준다.
 *          편집을 알림으로만 내는 모델(`AudioEventEditModel` · `InputMapViewModel` · `DataTableViewModel` — 문서 패널의 편집 알림 창구 `DocumentEditRecorder`, `UIPreviewViewModel`)은 이 형식을 쓰지 않는다 —
 *          되돌리기와 미저장은 문서 패널이 문서 글로 든다(글로 대조하려고 프레임마다 표를 직렬화하지 않는다).
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
        using CaptureDelegate = typename EditSnapshotHistory<TSnapshot>::CaptureDelegate;
        using ApplyDelegate   = typename EditSnapshotHistory<TSnapshot>::ApplyDelegate;

        EditorDocumentState() = default;
        explicit EditorDocumentState( TSaved saved )
            : _saved{ std::move( saved ) }
            , _history{}
        {
        }
        EditorDocumentState( const EditorDocumentState& )            = delete;
        EditorDocumentState& operator=( const EditorDocumentState& ) = delete;

        /** @brief 지금 값을 만드는 법과 되돌린 값을 넣는 법입니다(`EditSnapshotHistory::bind`). 모델의 생성자에서 한 번 겁니다. */
        void bind( CaptureDelegate capture, ApplyDelegate apply ) { _history.bind( std::move( capture ), std::move( apply ) ); }
        /** @brief 명령을 @p stack(문서 패널의 스택)에 올립니다(`EditSnapshotHistory::bindStack`). 앞 기록은 버립니다. */
        void bindStack( CommandStack& stack ) { _history.bindStack( stack ); }
        /** @brief 되돌리기 명령이 쌓이는 스택입니다(창이 Ctrl+Z 를 이리 보낼 수 있다). */
        CommandStack& getStack() { return _history.getStack(); }

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
        bool record( const TSnapshot& before, string_view coalesceKey = {} ) { return _history.record( before, coalesceKey ); }
        void endCoalescing() { _history.endCoalescing(); }
        /** @brief 한 단계 되돌립니다 — 넣기는 `bind` 의 apply 가 한다. */
        [[nodiscard]] bool undo() { return _history.undo(); }
        [[nodiscard]] bool redo() { return _history.redo(); }
        /** @brief 방금 기록한 편집을 되돌리고 다시 하기에서도 지웁니다(편집 거절). */
        void cancelLast() { _history.cancelLast(); }
        bool canUndo() const { return _history.canUndo(); }
        bool canRedo() const { return _history.canRedo(); }

    private:
        TSaved                         _saved{};
        EditSnapshotHistory<TSnapshot> _history{};
    };
} // namespace sw::editor
