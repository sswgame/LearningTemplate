#include "pch.h"

#include "Editor/Panels/Prefab/PrefabEditorPanel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

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

            /** @brief 격리 단계를 나갔다고 알립니다. */
            static void notifyExited()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabIsolationExited" ),
                                                                              NotificationType::Info );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "PrefabEditorPanel" );
    SW_EDITOR_PANEL( PrefabEditorPanel, PrefabEditorPanel::kPanelID, EditorPanelCategory::Tool, 1410 );

    PrefabEditorPanel::PrefabEditorPanel()
        : EditorPreviewPanel{ EditorAssetType::Prefab, false }
        , _isolation{}
        , _gizmo{}
        , _pendingNestedPath{}
        , _spawnedDocumentPath{}
        , _status{}
        , _bConfirmClose{ false }
        , _seenCommandCount{ 0 }
    {
    }

    PrefabEditorPanel* PrefabEditorPanel::findWindow()
    {
        EditorContext* pContext = EditorContext::get();
        return pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<PrefabEditorPanel>() : nullptr;
    }

    PrefabEditorPanel* PrefabEditorPanel::findIsolatingWindow()
    {
        PrefabEditorPanel* pWindow = findWindow();
        return ( pWindow != nullptr && pWindow->isOpen() && pWindow->isIsolating() ) ? pWindow : nullptr;
    }

    bool PrefabEditorPanel::openWindow( string_view prefabPath )
    {
        EditorContext*     pContext    = EditorContext::get();
        PrefabEditorPanel* pWindow     = findWindow();
        const bool         bCannotOpen = prefabPath.empty() || pContext == nullptr || pWindow == nullptr || EditorPlaySession::isStopped() == false;
        if ( bCannotOpen )
            return false;

        EditorWorkspace& workspace = pContext->getSubsystem<EditorWorkspace>();
        workspace.setFocusedAssetPath( string{ prefabPath }.c_str() ); // 문서 패널이 다음 그리기에서 문서를 바꾼다
        workspace.requestOpenPanel( pWindow->getPanelTitle() );
        pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPrefabTitle" ), SW_LOCKEY( "EdCommon", "NotifyPrefabIsolated" ),
                                                                  NotificationType::Info );
        return true;
    }

    GameObjectManager* PrefabEditorPanel::findPreviewManager() const
    {
        Scene* pScene = getPreviewScene();
        return pScene != nullptr ? pScene->getObjectManager() : nullptr;
    }

    void PrefabEditorPanel::populatePreviewScene( Scene& /*scene*/ )
    {
        getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, PrefabEditorPanelInternal::kFrameRadius );
        _spawnedDocumentPath.clear(); // 새 씬에는 문서 루트가 아직 없다
        _isolation.clear();
    }

    ToolAssetLoadResult PrefabEditorPanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();
        _spawnedDocumentPath.clear(); // 다시 읽으면 루트도 다시 스폰한다(`syncDocumentRoot`)
        return ToolAssetLoadResult::Loaded;
    }

    bool PrefabEditorPanel::rebuildDocumentRoot()
    {
        GameObjectManager* pManager = findPreviewManager();
        if ( pManager == nullptr || getLoadedAssetPath().empty() )
            return false;

        // 옛 문서 — 루트를 지우면 중첩 단계의 루트 · 숨긴 오브젝트도 함께 사라진다(모두 문서 루트의 자손이다).
        GameObject* pOldRoot = _isolation.isActive() ? pManager->findGameObjectByID( _isolation.getDocumentRootObjectID() ) : nullptr;
        getPreviewSelection().clearObjectSelection();
        _isolation.clear();
        if ( pOldRoot != nullptr )
            pManager->destroyObject( pOldRoot );
        getPreviewCommandStack().clear();
        _seenCommandCount    = 0;
        _spawnedDocumentPath = getLoadedAssetPath();

        GameObject* pRoot = EditorAssetCommands::spawnPrefabWithoutUndo( pManager, getLoadedAssetPath().c_str(), nullptr );
        if ( pRoot == nullptr )
        {
            _status = "Could not spawn the prefab";
            return false;
        }
        _status.clear();
        _isolation.push( *pManager, getLoadedAssetPath(), *pRoot, true );
        getPreviewSelection().selectObject( pRoot );
        getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, PrefabEditorPanelInternal::kFrameRadius );
        return true;
    }

    void PrefabEditorPanel::syncDocumentRoot()
    {
        const bool bDocumentReady = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false && isDocumentLoaded();
        if ( bDocumentReady == false || _spawnedDocumentPath == getLoadedAssetPath() )
            return;
        (void)rebuildDocumentRoot(); // 실패는 상태 줄에 남는다 — 같은 경로로 프레임마다 다시 스폰하지 않게 `_spawnedDocumentPath` 는 이미 채웠다
    }

    bool PrefabEditorPanel::enterNestedIsolation( string_view prefabPath )
    {
        GameObjectManager* pManager     = findPreviewManager();
        EditorContext*     pContext     = EditorContext::get();
        const bool         bCannotEnter = pManager == nullptr || pContext == nullptr || prefabPath.empty() || _isolation.isActive() == false;
        if ( bCannotEnter )
            return false;

        GameObject* pUnderRoot = pManager->findGameObjectByID( _isolation.getRootObjectID() );
        GameObject* pRoot      = EditorPrefabIsolation::findInstance( *pManager, pContext->getSubsystem<EditorWorkspace>(), prefabPath,
                                                                      getPreviewSelection().getPrimaryObject(), pUnderRoot );
        bool        bSpawned{ false };
        if ( pRoot == nullptr )
        {
            const string path{ prefabPath };
            pRoot = EditorAssetCommands::spawnPrefabWithoutUndo( pManager, path.c_str(), pUnderRoot );
            if ( pRoot == nullptr )
                return false;
            bSpawned = true;
        }
        _isolation.push( *pManager, prefabPath, *pRoot, bSpawned );
        getPreviewSelection().selectObject( pRoot );
        return true;
    }

    bool PrefabEditorPanel::exitIsolation( bool bSaveToPrefab )
    {
        GameObjectManager* pManager = findPreviewManager();
        const auto*        pFrame   = _isolation.getFrame();
        if ( pManager == nullptr || pFrame == nullptr )
            return false;

        const PrefabIsolationFrame frame = *pFrame;
        GameObject*                pRoot = pManager->findGameObjectByID( frame._rootObjectID );

        // 저장하지 못하면 나가지 않는다. 나가면 편집한 프리팹 내용이 저장 없이 사라진다.
        const bool bSaveFailed = bSaveToPrefab && pRoot != nullptr && EditorInspectorCommands::applyToPrefab( pRoot, frame._prefabPath ) == false;
        if ( bSaveFailed )
        {
            SW_LOG_ERROR( "Prefab window could not save '%#' - still editing it", frame._prefabPath.c_str() );
            _status = "Could not save the prefab";
            return false;
        }
        if ( bSaveToPrefab )
        {
            clearDocumentDirty();
            _seenCommandCount = getPreviewCommandStack().getCommandCount();
        }

        if ( _isolation.getDepth() <= 1 )
        {
            setOpen( false ); // 문서 단계 — 창을 닫는다. 문서 루트는 씬에 남는다
            PrefabEditorPanelInternal::notifyExited();
            return true;
        }

        PrefabIsolationFrame popped;
        (void)_isolation.pop( pManager, popped );
        const bool bDiscardSpawnedRoot = popped._bSpawnedRoot == SW_TRUE && bSaveToPrefab == false && pRoot != nullptr;
        if ( bDiscardSpawnedRoot )
        {
            getPreviewSelection().selectObject( pRoot, SelectionMode::Remove );
            pManager->destroyObject( pRoot );
        }
        getPreviewSelection().selectObject( pManager->findGameObjectByID( _isolation.getRootObjectID() ) );
        PrefabEditorPanelInternal::notifyExited();
        return true;
    }

    bool PrefabEditorPanel::saveDocument()
    {
        GameObjectManager* pManager = findPreviewManager();
        const auto*        pFrame   = _isolation.getFrame();
        GameObject*        pRoot    = ( pManager != nullptr && pFrame != nullptr ) ? pManager->findGameObjectByID( pFrame->_rootObjectID ) : nullptr;
        if ( pRoot == nullptr || EditorInspectorCommands::applyToPrefab( pRoot, pFrame->_prefabPath ) == false )
        {
            _status = "Could not save the prefab";
            return false;
        }
        _status           = "Saved";
        _seenCommandCount = getPreviewCommandStack().getCommandCount();
        return true;
    }

    void PrefabEditorPanel::revertDocument()
    {
        (void)rebuildDocumentRoot(); // 씬이 없으면 버릴 편집도 없다
    }

    void PrefabEditorPanel::drawCloseConfirm()
    {
        constexpr const utf8* kPopupID = "##UnsavedPrefabWindowClose";
        if ( ImGui::IsPopupOpen( kPopupID ) == false )
            ImGui::OpenPopup( kPopupID );
        const EditorUnsavedChoice choice = EditorWidgets::drawUnsavedChangesModal( kPopupID, "This prefab has unsaved changes. Close anyway?" );
        if ( choice == EditorUnsavedChoice::None )
            return;

        _bConfirmClose = false;
        if ( EditorSessionPolicy::shouldSaveBeforeAction( choice ) && saveDocumentAndClearDirty() == false )
            return; // 저장하지 못하면 닫지 않는다 — 닫으면 편집이 사라진다. 이유는 상태 줄에 남았다
        if ( EditorSessionPolicy::shouldClearDirtyWithoutSave( choice ) )
            discardDirtyDocument();
        if ( EditorSessionPolicy::shouldProceedWithAction( choice ) )
            setOpen( false );
    }

    void PrefabEditorPanel::trackEdits()
    {
        const size_t count = getPreviewCommandStack().getCommandCount();
        if ( count == _seenCommandCount )
            return;
        _seenCommandCount = count;
        markDocumentDirty();
    }

    void PrefabEditorPanel::drawChildTree( GameObject& object, int32 depth )
    {
        ImGui::PushID( static_cast<int32>( object.getObjectID() ) );
        ImGui::Indent( PrefabEditorPanelInternal::kChildIndent * static_cast<float32>( depth ) );
        if ( ImGui::Selectable( object.getName().c_str(), getPreviewSelection().hasObject( &object ) ) )
            getPreviewSelection().selectObject( &object );
        EditorSelfTestMarks::note( ( "prefabwindow.child." + string{ object.getName().c_str() } ).c_str() );
        ImGui::Unindent( PrefabEditorPanelInternal::kChildIndent * static_cast<float32>( depth ) );
        ImGui::PopID();
        vector<GameObject*> listChild;
        object.getChildren( listChild );
        for ( GameObject* pChild : listChild )
        {
            if ( pChild != nullptr && pChild->isPendingDestroy() == false )
                drawChildTree( *pChild, depth + 1 );
        }
    }

    void PrefabEditorPanel::drawIsolationBar()
    {
        const bool bNested = _isolation.getDepth() > 1;
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 모두 이것을 거친다 — 실패는 상태 줄에 남는다
        ImGui::SameLine();
        if ( ImGui::Button( bNested ? "Save & Back" : "Save & Close" ) )
            (void)exitIsolation( true );
        ImGui::SameLine();
        if ( ImGui::Button( bNested ? "Back" : "Close" ) )
            (void)exitIsolation( false );
        ImGui::SameLine();
        if ( ImGui::Button( "Reload" ) )
            reloadDocument();
    }

    void PrefabEditorPanel::drawContent()
    {
        if ( shouldAskBeforeClosing( isOpen(), isDocumentDirty(), _bConfirmClose ) )
        {
            setOpen( true ); // X 가 내린 열림을 확인이 끝날 때까지 되돌린다
            _bConfirmClose = true;
            drawCloseConfirm();
        }
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( "prefabwindow" );
        syncDocumentRoot();
        if ( _pendingNestedPath.empty() == false )
        {
            const string path = std::move( _pendingNestedPath );
            _pendingNestedPath.clear();
            if ( enterNestedIsolation( path ) == false )
                _status = "Could not open the nested prefab";
        }

        if ( getLoadedAssetPath().empty() || _isolation.isActive() == false )
        {
            EditorWidgets::drawEmptyHint( "Open a prefab with Open... above" );
            EditorWidgets::drawPanelStatus( _status.c_str() );
            return;
        }

        trackEdits();
        ImGui::TextDisabled( "%s%s", _isolation.getPrefabPath().data(), isDocumentDirty() ? " *" : "" );
        drawIsolationBar();

        const float32 side   = PrefabEditorPanelInternal::kMinViewportSide;
        const ImVec2  avail  = ImGui::GetContentRegionAvail();
        const float32 width  = MathUtil::max( avail.x, side );
        const float32 height = MathUtil::max( avail.y * 0.55f, side );
        drawPreviewViewport( float2{ width, height }, _gizmo.hasMouse() );
        _gizmo.draw( *this );

        GameObjectManager* pManager = findPreviewManager();
        GameObject*        pTop     = pManager != nullptr ? pManager->findGameObjectByID( _isolation.getRootObjectID() ) : nullptr;
        if ( pTop != nullptr )
        {
            ImGui::Separator();
            ImGui::BeginChild( "##PrefabChildren", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_None );
            drawChildTree( *pTop, 0 );
            ImGui::EndChild();
        }
        EditorWidgets::drawPanelStatus( _status.c_str() );
    }
} // namespace sw::editor
