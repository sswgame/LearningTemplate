#include "pch.h"

#include "Editor/Panels/SceneViewPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Utility/GameTimeScale.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SceneViewPanel, "scene_view", EditorPanelCategory::Core, 300 );

    SceneViewPanel::SceneViewPanel()
        : _viewportClient{}
        , _listDebugCategory{}
    {
    }

    void SceneViewPanel::drawContent()
    {
        EditorContext* pEditorContext = EditorContext::get();
        if ( pEditorContext == nullptr )
            return;

        const bool bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        // 엔진 프레임의 실제 경과(시간 배율 · 정지와 무관) — 고정 프레임 시간(`-gv_fixedFrameDelta`)으로 도는 시나리오에서 카메라 비행이 결정적이다.
        const float32 dt = GameTimeScale::getUnscaledDeltaTime( ImGui::GetIO().DeltaTime );
        _viewportClient.update( dt, bFocused, bHovered );

        if ( EditorChrome::beginToolbar( "##SceneViewToolbar" ) )
        {
            if ( ImGui::Button( "Dbg Cat" ) )
                ImGui::OpenPopup( "##DebugDrawCategories" );
            EditorWidgets::drawTooltip( "DebugDrawQueue 카테고리를 켜고 끕니다" );
            drawDebugCategoryPopup();
            EditorWidgets::drawToolbarSeparator();
            _viewportClient.drawViewportToolbar( ImGui::GetContentRegionAvail().x );
        }
        EditorChrome::endToolbar();

        // 크기 규칙은 게임 뷰와 같다(정수 픽셀로 내림) — 같은 도크 영역의 두 탭이 같은 크기의 RT 를 갖는다.
        const ImVec2         available = ImGui::GetContentRegionAvail();
        const EditorViewRect rect      = EditorViewTargetUtil::fitViewImage( float2{ available.x, available.y }, EditorGameViewAspect::Free );
        const ImVec2         size{ rect._size._x, rect._size._y };
        if ( size.x > 1.0f && size.y > 1.0f )
        {
            const uint32            targetWidth  = static_cast<uint32>( size.x );
            const uint32            targetHeight = static_cast<uint32>( size.y );
            const EditorViewTarget& view         = pEditorContext->getViewTarget( EditorViewKind::Scene );
            if ( EditorViewTargetUtil::needsResize( view._width, view._height, targetWidth, targetHeight ) )
                pEditorContext->ensureViewTargetSize( EditorViewKind::Scene, targetWidth, targetHeight );
        }
        // 이 패널이 보이는 프레임만 씬 뷰를 그린다(접힘 · 닫힘 · 다른 탭이면 drawContent 가 불리지 않는다).
        pEditorContext->markViewDrawn( EditorViewKind::Scene );

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        _viewportClient.draw( pEditorContext->getViewTarget( EditorViewKind::Scene )._pTextureId, float2{ size.x, size.y } );

        if ( size.x > 1.0f && size.y > 1.0f )
        {
            const float2  barAnchor{ imagePos.x + size.x * 0.5f, imagePos.y + 8.0f };
            const float32 barMaxWidth = size.x - 16.0f; // 씬 뷰 양쪽에 8 px 씩 남긴다
            _viewportClient.drawTransformBar( barAnchor, barMaxWidth );
        }
    }

    void SceneViewPanel::drawDebugCategoryPopup()
    {
        if ( ImGui::BeginPopup( "##DebugDrawCategories" ) == false )
            return;
        DebugDrawQueue* pQueue = getService<DebugDrawQueue>();
        _listDebugCategory.clear();
        if ( pQueue != nullptr )
            pQueue->collectCategories( _listDebugCategory );
        if ( _listDebugCategory.empty() )
            ImGui::TextDisabled( "No debug draw yet." );
        for ( const hashed_string& category : _listDebugCategory )
        {
            bool bEnabled = pQueue->isCategoryEnabled( category );
            if ( ImGui::Checkbox( category.c_str(), &bEnabled ) )
                pQueue->setCategoryEnabled( category, bEnabled );
        }
        ImGui::EndPopup();
    }
} // namespace sw::editor
