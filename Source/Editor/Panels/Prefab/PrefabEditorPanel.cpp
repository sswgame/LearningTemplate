#include "pch.h"

#include "Editor/Panels/Prefab/PrefabEditorPanel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PrefabEditorPanelInternal
        {
            /** @brief 문서를 열 때 카메라가 겨누는 반지름(미터)입니다 — 프리팹 크기를 모르므로 원점 주변 고정값이다. */
            static constexpr float32 kFrameRadius = 2.0f;
            /// @brief 뷰포트의 최소 한 변(96 DPI 기준 픽셀)입니다.
            static constexpr float32 kMinViewportSide = 160.0f;
            /// @brief 자식 목록의 들여쓰기 한 단계(픽셀)입니다.
            static constexpr float32 kChildIndent = 12.0f;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( PrefabEditorPanel, PrefabEditorPanel::kPanelID, EditorPanelCategory::Tool, 1410 );

    PrefabEditorPanel::PrefabEditorPanel()
        : EditorPreviewPanel{ PrefabEditorViewModel::getDocumentType(), false }
        , _viewModel{}
        , _viewState{}
        , _gizmo{}
    {
    }

    PrefabEditorPanel* PrefabEditorPanel::findWindow()
    {
        return EditorPanelManager::getInEditorContext().findPanel<PrefabEditorPanel>();
    }

    PrefabEditorPanel* PrefabEditorPanel::findIsolatingWindow()
    {
        PrefabEditorPanel* pWindow = findWindow();
        return ( pWindow != nullptr && pWindow->isOpen() && pWindow->isIsolating() ) ? pWindow : nullptr;
    }

    bool PrefabEditorPanel::openWindow( string_view prefabPath )
    {
        return findWindow() != nullptr && PrefabEditorViewModel::requestOpenWindow( prefabPath, kPanelID );
    }

    void PrefabEditorPanel::populatePreviewScene( Scene& /*scene*/ )
    {
        getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, PrefabEditorPanelInternal::kFrameRadius );
        _viewModel.resetForNewScene();
    }

    ToolAssetLoadResult PrefabEditorPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        _viewModel.requestRebuild(); // 다시 읽으면 루트도 다시 스폰한다(`syncDocumentRoot`)
        return ToolAssetLoadResult::Loaded;
    }

    bool PrefabEditorPanel::rebuildDocumentRoot()
    {
        if ( getPreviewScene() == nullptr || getLoadedAssetPath().empty() )
            return false;

        const bool bSpawned = _viewModel.rebuildRoot( getPreviewScene(), getPreviewSelection(), getLoadedAssetPath() );
        resetDocumentHistory(); // 옛 문서의 편집 기록은 새 루트와 맞지 않는다
        if ( bSpawned )
            getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, PrefabEditorPanelInternal::kFrameRadius );
        return bSpawned;
    }

    void PrefabEditorPanel::syncDocumentRoot()
    {
        const bool bDocumentReady = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false && isDocumentLoaded();
        if ( bDocumentReady == false || _viewModel.needsRebuild( getLoadedAssetPath() ) == false )
            return;
        (void)rebuildDocumentRoot(); // 실패는 상태 줄에 남는다 — 같은 경로로 프레임마다 다시 스폰하지 않게 모델이 스폰한 경로를 이미 채웠다
    }

    bool PrefabEditorPanel::enterNestedIsolation( string_view prefabPath )
    {
        return _viewModel.enterNested( getPreviewScene(), getPreviewSelection(), prefabPath );
    }

    bool PrefabEditorPanel::exitIsolation( bool bSaveToPrefab )
    {
        const PrefabFrameExit result = _viewModel.exitFrame( getPreviewScene(), getPreviewSelection(), bSaveToPrefab );
        if ( result == PrefabFrameExit::Failed )
            return false;
        if ( bSaveToPrefab )
        {
            clearDocumentDirty();
            acknowledgePreviewEdits();
        }
        if ( result == PrefabFrameExit::DocumentStep )
        {
            setOpen( false ); // 문서 단계 — 창을 닫는다(편집이 남았으면 확인을 거친다). 닫히면 기반이 미리보기 씬을 놓고, 다시 열면 문서 루트를 새로 스폰한다
            if ( isOpen() == false )
                PrefabEditorViewModel::notifyExited(); // 확인을 기다리는 중이면 아직 나가지 않았다
            return true;
        }
        PrefabEditorViewModel::notifyExited();
        return true;
    }

    bool PrefabEditorPanel::saveDocument()
    {
        if ( _viewModel.saveFrame( getPreviewScene() ) == false )
            return false;
        acknowledgePreviewEdits();
        return true;
    }

    void PrefabEditorPanel::revertDocument()
    {
        (void)rebuildDocumentRoot(); // 씬이 없으면 버릴 편집도 없다
    }

    void PrefabEditorPanel::drawChildRows()
    {
        for ( const PrefabEditorChildRow& row : _viewState._listChildRow )
        {
            const float32 indent = PrefabEditorPanelInternal::kChildIndent * static_cast<float32>( row._depth );
            ImGui::PushID( static_cast<int32>( row._objectID ) );
            ImGui::Indent( indent );
            if ( ImGui::Selectable( row._name.c_str(), row._bSelected != SW_FALSE ) )
                _viewModel.selectObject( getPreviewScene(), getPreviewSelection(), row._objectID );
            EditorSelfTestMarks::record( ( "prefabwindow.child." + row._name ).c_str() );
            ImGui::Unindent( indent );
            ImGui::PopID();
        }
    }

    void PrefabEditorPanel::drawIsolationBar()
    {
        const bool bNested = _viewState._bNested != SW_FALSE;
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 모두 이것을 거친다 — 실패는 상태 줄에 남는다
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( bNested ? "Save & Back" : "Save & Close" ) );
        if ( ImGui::Button( bNested ? "Save & Back" : "Save & Close" ) )
            (void)exitIsolation( true );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( bNested ? "Back" : "Close" ) );
        if ( ImGui::Button( bNested ? "Back" : "Close" ) )
            (void)exitIsolation( false );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
            reloadDocument();
    }

    void PrefabEditorPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "prefabwindow" );
        syncDocumentRoot();
        _viewModel.enterPendingNested( getPreviewScene(), getPreviewSelection() );
        _viewModel.gather( getPreviewScene(), getPreviewSelection(), _viewState );

        if ( getLoadedAssetPath().empty() || _viewState._bIsolating == SW_FALSE )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdLabel", "OpenAPrefabWithOpenAbove" ) );
            EditorWidgets::drawPanelStatus( _viewState._status.c_str() );
            return;
        }

        ImGui::TextDisabled( "%s%s", _viewState._prefabPath.c_str(), isDocumentDirty() ? " *" : "" );
        drawIsolationBar();

        const float32 side   = PrefabEditorPanelInternal::kMinViewportSide;
        const ImVec2  avail  = ImGui::GetContentRegionAvail();
        const float32 width  = MathUtil::max( avail.x, side );
        const float32 height = MathUtil::max( avail.y * 0.55f, side );
        drawPreviewViewport( float2{ width, height }, _gizmo.hasMouse() );
        _gizmo.draw( *this );

        if ( _viewState._listChildRow.empty() == false )
        {
            ImGui::Separator();
            ImGui::BeginChild( "##PrefabChildren", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_None );
            drawChildRows();
            ImGui::EndChild();
        }
        EditorWidgets::drawPanelStatus( _viewModel.getStatus().c_str() );
    }
} // namespace sw::editor
