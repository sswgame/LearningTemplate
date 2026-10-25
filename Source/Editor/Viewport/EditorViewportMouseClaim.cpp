#include "pch.h"

#include "Editor/Viewport/EditorViewportMouseClaim.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

namespace sw::editor
{
    namespace
    {
        /// @brief 소유자와 프레임 번호입니다. 에디터 범위의 상태 저장소가 가진다(프로세스 정적이 아니라 에디터마다 따로이고 컨텍스트와 함께 사라진다).
        struct EditorViewportMouseClaimInternal
        {
            struct State
            {
                const void* _pMouseOwner{ nullptr };
                uint64      _currentFrame{ 0 }; ///< 마지막으로 `beginFrame` 에 들어온 프레임 번호
                uint64      _renewedFrame{ 0 }; ///< 소유자가 마지막으로 `acquire` 를 부른 프레임 번호
            };

            static State& get() { return getEditorState<State>(); }
        };
    } // namespace

    void EditorViewportMouseClaim::beginFrame( uint64 frameIndex )
    {
        auto& state = EditorViewportMouseClaimInternal::get();
        if ( frameIndex == state._currentFrame )
            return;
        // 지난 프레임에 아무도 다시 잡지 않았으면 소유자의 그리기 함수가 돌지 않은 것이다 — 풀 기회를 놓쳤으니 회수한다
        if ( state._pMouseOwner != nullptr && state._renewedFrame != state._currentFrame )
            state._pMouseOwner = nullptr;
        state._currentFrame = frameIndex;
    }

    void EditorViewportMouseClaim::acquire( const void* pOwner )
    {
        auto& state         = EditorViewportMouseClaimInternal::get();
        state._pMouseOwner  = pOwner;
        state._renewedFrame = state._currentFrame;
    }

    void EditorViewportMouseClaim::release( const void* pOwner )
    {
        auto& state = EditorViewportMouseClaimInternal::get();
        if ( state._pMouseOwner == pOwner )
            state._pMouseOwner = nullptr;
    }

    bool EditorViewportMouseClaim::isClaimed()
    {
        return EditorViewportMouseClaimInternal::get()._pMouseOwner != nullptr;
    }

    bool EditorViewportMouseClaim::isClaimedBy( const void* pOwner )
    {
        return pOwner != nullptr && EditorViewportMouseClaimInternal::get()._pMouseOwner == pOwner;
    }
} // namespace sw::editor
