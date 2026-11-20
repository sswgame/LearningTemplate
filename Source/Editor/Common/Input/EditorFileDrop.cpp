#include "pch.h"

#include "Editor/Common/Input/EditorFileDrop.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorFileDropInternal
        {
            /** @brief 끌어 놓기 상태입니다(에디터 범위 상태 저장소가 소유). */
            struct State
            {
                vector<string> _listDroppedPath{};
                float2         _hoverPosition{};
                float2         _dropPosition{};
                uint8          _bHovering    : 1;
                uint8          _bDropPending : 1;

                State()
                    : _listDroppedPath{}
                    , _hoverPosition{}
                    , _dropPosition{}
                    , _bHovering{ SW_FALSE }
                    , _bDropPending{ SW_FALSE }
                {
                }
            };
        };
    } // namespace

    void EditorFileDrop::post( const WindowFileDropEvent& event )
    {
        // 창 좌표 → ImGui 화면 좌표. 여러 뷰포트를 켜면 ImGui 좌표는 바탕 화면 기준이라 주 뷰포트 자리를 더한다(끌어 놓기는 주 창만 받는다).
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        postAtScreenPosition( event._phase, event._listPath, float2{ origin.x + static_cast<float32>( event._x ), origin.y + static_cast<float32>( event._y ) } );
    }

    void EditorFileDrop::postAtScreenPosition( WindowFileDropPhase phase, const vector<string>& listPath, const float2& screenPosition )
    {
        EditorFileDropInternal::State& state = getEditorState<EditorFileDropInternal::State>();
        switch ( phase )
        {
            case WindowFileDropPhase::Enter:
            case WindowFileDropPhase::Over:
            {
                state._bHovering     = SW_TRUE;
                state._hoverPosition = screenPosition;
                break;
            }
            case WindowFileDropPhase::Leave:
            {
                state._bHovering = SW_FALSE;
                break;
            }
            case WindowFileDropPhase::Drop:
            {
                if ( state._bDropPending != SW_FALSE )
                    SW_LOG_WARNING_ID( "EditorFileDrop.UntakenDropReplaced", "A file drop was not taken by any panel (the Content Browser was closed or the drop missed it) - %# file(s) ignored",
                                       static_cast<uint64>( state._listDroppedPath.size() ) );
                state._bHovering       = SW_FALSE;
                state._bDropPending    = SW_TRUE;
                state._listDroppedPath = listPath;
                state._dropPosition    = screenPosition;
                break;
            }
        }
    }

    bool EditorFileDrop::findHover( float2& outPosition )
    {
        const EditorFileDropInternal::State& state = getEditorState<EditorFileDropInternal::State>();
        outPosition                                = state._hoverPosition;
        return state._bHovering != SW_FALSE;
    }

    bool EditorFileDrop::peekDrop( float2& outPosition )
    {
        const EditorFileDropInternal::State& state = getEditorState<EditorFileDropInternal::State>();
        outPosition                                = state._dropPosition;
        return state._bDropPending != SW_FALSE;
    }

    bool EditorFileDrop::takeDrop( vector<string>& outListPath, float2& outPosition )
    {
        EditorFileDropInternal::State& state = getEditorState<EditorFileDropInternal::State>();
        outListPath.clear();
        if ( state._bDropPending == SW_FALSE )
            return false;
        state._bDropPending = SW_FALSE;
        outListPath.swap( state._listDroppedPath );
        outPosition = state._dropPosition;
        return true;
    }
} // namespace sw::editor
