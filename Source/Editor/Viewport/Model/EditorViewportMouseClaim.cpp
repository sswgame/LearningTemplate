#include "pch.h"

#include "Editor/Viewport/Model/EditorViewportMouseClaim.h"

namespace sw::editor
{
    namespace
    {
        const void* s_pMouseOwner  = nullptr;
        uint64      s_currentFrame = 0; ///< 마지막으로 `beginFrame` 에 들어온 프레임 번호
        uint64      s_renewedFrame = 0; ///< 소유자가 마지막으로 `acquire` 를 부른 프레임 번호
    } // namespace

    void EditorViewportMouseClaim::beginFrame( uint64 frameIndex )
    {
        if ( frameIndex == s_currentFrame )
            return;
        // 지난 프레임에 아무도 다시 잡지 않았으면 소유자의 그리기 함수가 돌지 않은 것이다 — 풀 기회를 놓쳤으니 회수한다
        if ( s_pMouseOwner != nullptr && s_renewedFrame != s_currentFrame )
            s_pMouseOwner = nullptr;
        s_currentFrame = frameIndex;
    }

    void EditorViewportMouseClaim::acquire( const void* pOwner )
    {
        s_pMouseOwner  = pOwner;
        s_renewedFrame = s_currentFrame;
    }

    void EditorViewportMouseClaim::release( const void* pOwner )
    {
        if ( s_pMouseOwner == pOwner )
            s_pMouseOwner = nullptr;
    }

    bool EditorViewportMouseClaim::isClaimed()
    {
        return s_pMouseOwner != nullptr;
    }

    bool EditorViewportMouseClaim::isClaimedBy( const void* pOwner )
    {
        return pOwner != nullptr && s_pMouseOwner == pOwner;
    }
} // namespace sw::editor
