#include "pch.h"

#include "Editor/Viewport/Model/EditorViewportOverlaysViewModel.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Viewport/Model/SceneViewOverlayLayoutStore.h"

namespace sw::editor
{
    void EditorViewportOverlaysViewModel::gather( vector<EditorOverlayBarState>& outListBar )
    {
        outListBar.clear();
        // 저장 파일에는 아직 등록하지 않은 바도 남는다(`loadFromFile`) — 씬 뷰 바만 넘긴다.
        for ( const EditorOverlayBarState& state : editor::getSubsystem<SceneViewOverlayLayoutStore>().getLayout()._listBar )
        {
            if ( SceneViewOverlayLayoutStore::isKnownBarID( state._id ) )
                outListBar.push_back( state );
        }
    }

    bool EditorViewportOverlaysViewModel::findBar( string_view barID, EditorOverlayBarState& outState )
    {
        const SceneViewOverlayLayoutStore* pStore = editor::findSubsystem<SceneViewOverlayLayoutStore>(); // 감시 값은 에디터 범위 없이도 불린다
        const EditorOverlayBarState*       pState = pStore != nullptr ? pStore->getLayout().findBar( barID ) : nullptr;
        if ( pState == nullptr )
            return false;
        outState = *pState;
        return true;
    }

    void EditorViewportOverlaysViewModel::resetToDefault()
    {
        editor::getSubsystem<SceneViewOverlayLayoutStore>().resetToDefault();
    }

    void EditorViewportOverlaysViewModel::setBarVisible( string_view barID, bool bVisible )
    {
        (void)editor::getSubsystem<SceneViewOverlayLayoutStore>().setBarVisible( barID, bVisible ); // View 는 모은 바의 id 만 넘긴다
    }

    void EditorViewportOverlaysViewModel::setBarCollapsed( string_view barID, bool bCollapsed )
    {
        (void)editor::getSubsystem<SceneViewOverlayLayoutStore>().setBarCollapsed( barID, bCollapsed ); // View 는 모은 바의 id 만 넘긴다
    }

    void EditorViewportOverlaysViewModel::dropBar( string_view barID, const float2& position, const float2& barSize, const float2& viewSize, float32 snapDistance )
    {
        (void)editor::getSubsystem<SceneViewOverlayLayoutStore>().dropBar( barID, position, barSize, viewSize, snapDistance ); // View 는 모은 바의 id 만 넘긴다
    }
} // namespace sw::editor
