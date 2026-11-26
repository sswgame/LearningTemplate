/**
 * @file EditSnapshotHistory.h
 * @brief 편집 모델의 스냅샷 되돌리기입니다 — 편집 직전 상태를 명령 스택(`CommandStack`)의 명령 하나로 올리고, 같은 키의 연속 편집(슬라이더 끌기)은 한 명령으로 합칩니다.
 * @details 예전에는 모델마다 자기 되돌리기 · 다시 하기 벡터를 들어 Ctrl+Z · History 창 · 바이트 한도 · 열린 상호작용 거절이 닿지 않았다(명령 이력 네 벌).
 *          지금 기록은 늘 `CommandStack` 이다 — 문서 패널에 묶으면(`bindStack`) 그 문서의 스택에, 아니면 자기 스택에 쌓인다. 모델은 지금 상태를 만들고(`capture`) 넣는(`apply`) 법만 준다.
 *          명령은 편집 전 값만 들고, 처음 되돌릴 때 그때의 상태를 다시 하기 값으로 잡는다 — 스택 차례상 그때가 편집 직후 상태다.
 *          언리얼 트랜잭션 버퍼 · 유니티 Undo 는 프로세스에 하나, Godot `EditorUndoRedoManager` 는 문서(씬 · 리소스)마다 history 를 둔다 — 문서 스택에 묶는 Godot 쪽을 따른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/Undo/SnapshotUndoRecorder.h"

namespace sw::editor
{
    /**
     * @class EditSnapshotHistory
     * @brief 스냅샷 값 @p TSnapshot 의 되돌리기입니다. 쓰기 전에 `bind` 로 상태를 만들고 넣는 법을 준다(필수 — 없으면 단언).
     */
    template <typename TSnapshot>
    class EditSnapshotHistory
    {
    public:
        using CaptureDelegate = Delegate<TSnapshot()>;
        using ApplyDelegate   = Delegate<void( const TSnapshot& )>;

        EditSnapshotHistory()                                        = default;
        EditSnapshotHistory( const EditSnapshotHistory& )            = delete;
        EditSnapshotHistory& operator=( const EditSnapshotHistory& ) = delete;

        /** @brief 지금 상태를 만드는 법(@p capture)과 되돌린 상태를 넣는 법(@p apply — 넣은 뒤 모델이 할 일까지)입니다. 모델의 생성자에서 한 번 겁니다. */
        void bind( CaptureDelegate capture, ApplyDelegate apply )
        {
            _capture = std::move( capture );
            _apply   = std::move( apply );
        }
        /** @brief 명령을 @p stack(문서 패널의 스택)에 올립니다. 앞 기록은 버립니다. */
        void bindStack( CommandStack& stack ) { _recorder.bindStack( stack ); }
        /** @brief 명령이 올라가는 스택입니다(묶지 않았으면 자기 스택). 창이 Ctrl+Z 를 이리 보낸다(`EditorPanel::findPanelCommandStack`). */
        CommandStack& getStack() { return _recorder.getStack(); }

        /** @brief 이 기록의 명령을 모두 지웁니다(문서를 새로 읽을 때). */
        void clear() { _recorder.clear(); }

        /**
         * @brief 편집 직전 상태 @p before 를 명령으로 올립니다. 다시 하기 기록은 스택이 버린다.
         * @param coalesceKey 비어 있지 않고 방금 올린 명령의 키와 같으면(그 명령이 맨 위일 때) 새 명령을 만들지 않습니다(끌기 한 번이 한 단계).
         * @return 새 명령을 만들었으면 true, 직전 명령에 합쳤으면 false 입니다.
         */
        bool record( const TSnapshot& before, string_view coalesceKey = {}, string_view label = "Edit" )
        {
            SW_ASSERT( _capture.isBound() && _apply.isBound() );
            if ( _recorder.canCoalesce( coalesceKey ) )
                return false;
            const shared_ptr<Step> pStep = make_shared<Step>( Step{ before, TSnapshot{}, false } );
            const uint64           bytes = measureBytes( before );
            _recorder.push( label, coalesceKey, SW_DELEGATE_LAMBDA( Delegate<void()>, [this, pStep]()
            {
                if ( pStep->_bHasAfter == false )
                {
                    pStep->_after     = _capture(); // 처음 되돌릴 때의 상태가 편집 직후다(스택 차례)
                    pStep->_bHasAfter = true;
                }
                _apply( pStep->_before );
            } ),
                            SW_DELEGATE_LAMBDA( Delegate<void()>, [this, pStep]()
            { _apply( pStep->_after ); } ),
                            bytes * 2 );
            return true;
        }

        /** @brief 끌기가 끝났음을 알립니다. 다음 편집은 같은 키여도 새 명령입니다. */
        void endCoalescing() { _recorder.endCoalescing(); }
        /** @brief 한 단계 되돌립니다(넣기는 `apply` 가 한다). 기록이 없거나 스택이 거절하면 false 입니다. */
        [[nodiscard]] bool undo() { return _recorder.undo(); }
        /** @brief 되돌린 것을 다시 합니다. */
        [[nodiscard]] bool redo() { return _recorder.redo(); }
        /** @brief 방금 올린 편집을 되돌리고 다시 하기에서도 지웁니다 — 편집을 거절해 없던 일로 할 때 씁니다. */
        void cancelLast() { _recorder.cancelLast(); }

        bool canUndo() const { return _recorder.canUndo(); }
        bool canRedo() const { return _recorder.canRedo(); }

    private:
        struct Step
        {
            TSnapshot _before;
            TSnapshot _after;
            bool      _bHasAfter; // NOLINT(sw-bool-flag): 집합체다(중괄호로 만든다)
        };

        /** @brief 단계가 쥔 바이트입니다(되돌리기 바이트 한도가 센다). 글은 길이, 다른 값은 그 형식의 크기입니다. */
        static uint64 measureBytes( const TSnapshot& snapshot )
        {
            if constexpr ( std::is_same_v<TSnapshot, string> )
                return static_cast<uint64>( snapshot.size() );
            else
                return static_cast<uint64>( sizeof( TSnapshot ) );
        }

    private:
        SnapshotUndoRecorder _recorder;
        CaptureDelegate      _capture{};
        ApplyDelegate        _apply{};
    };
} // namespace sw::editor
