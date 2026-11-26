// 씬 뷰 오버레이 바의 Model 쪽(ImGui 없음) — 바가 보일 편집 상태 값을 모으고, 바가 적은 보기 모드 요청을 렌더러에 쓴다. 그리기는 `ViewportToolbarSettings.cpp`(View)다.
#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/ViewportToolbarSettings.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"

namespace sw::editor
{
    void ViewportToolbarState::gather( ViewportToolbarState& outState )
    {
        const EditorWorkspace& workspace = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        for ( uint32 slot = 0; slot < kBookmarkSlotCount; ++slot )
        {
            BookmarkRow&          row       = outState._arrBookmark[slot];
            const CameraBookmark* pBookmark = workspace.hasCameraBookmark( slot ) ? workspace.getCameraBookmark( slot ) : nullptr;
            row._bValid                     = pBookmark != nullptr;
            row._name                       = pBookmark != nullptr ? pBookmark->_name : string{};
            row._position                   = pBookmark != nullptr ? pBookmark->_position : float3{};
        }
        // 렌더러는 호스트 서브시스템다(선택 서브시스템 칸) — 테스트 하네스처럼 렌더러가 없는 호스트에서는 비워 콤보를 막는다.
        const FrameRenderer* pRenderer = editor::findSubsystem<FrameRenderer>();
        outState._sceneViewMode.reset();
        if ( pRenderer != nullptr )
            outState._sceneViewMode = pRenderer->getSceneViewMode();
        outState._selectedObjectCount = static_cast<uint32>( editor::getMainSceneSelection().getSelectedObjectCount() );
    }

    void ViewportToolbarSettings::applyViewModeRequest()
    {
        if ( _requestedViewMode.has_value() == false )
            return;
        FrameRenderer* pRenderer = editor::findSubsystem<FrameRenderer>();
        if ( pRenderer != nullptr )
            pRenderer->setSceneViewMode( *_requestedViewMode );
        _requestedViewMode.reset();
    }
} // namespace sw::editor
