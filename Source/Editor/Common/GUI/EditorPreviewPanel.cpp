#include "pch.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/ObjectUndoUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    EditorPreviewPanel::EditorPreviewPanel( EditorAssetType kind, bool bLoadOnOpen )
        : EditorDocumentPanel{ kind, bLoadOnOpen }
        , _viewport{}
        , _commandStack{}
        , _selection{}
        , _bFocusApplied{ false }
        , _seenCommandCount{ 0 }
    {
    }

    EditorPreviewPanel::~EditorPreviewPanel()
    {
        applyWindowFocus( false );
        _selection.unbindPreviewScope();
        _viewport.shutdown();
    }

    void EditorPreviewPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        applyWindowFocus( false );
        resetPreviewEdits();
        _selection.unbindPreviewScope();
        _viewport.shutdown();
    }

    void EditorPreviewPanel::resetPreviewEdits()
    {
        _commandStack.clear();
        _seenCommandCount = 0;
    }

    void EditorPreviewPanel::syncDocumentDirty()
    {
        const size_t count = _commandStack.getCommandCount();
        if ( count == _seenCommandCount )
            return;
        _seenCommandCount = count;
        markDocumentDirty();
    }

    ScenePreviewPolicy EditorPreviewPanel::getPreviewPolicy() const
    {
        ScenePreviewPolicy policy;
        policy._bTick    = true;
        policy._bPhysics = false;
        policy._bAudio   = false;
        policy._bPlay    = false;
        return policy;
    }

    void EditorPreviewPanel::beginPreviewFrame()
    {
        if ( _viewport.isInitialized() == false && _viewport.initialize( getPanelTitle(), getPreviewPolicy(), getPreviewEnvironment() ) )
        {
            Scene* pScene = _viewport.getScene();
            if ( pScene != nullptr )
            {
                _selection.bindPreviewScope( *pScene, _commandStack );
                populatePreviewScene( *pScene );
            }
        }
        applyWindowFocus( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
    }

    void EditorPreviewPanel::drawPreviewViewport( const float2& size, bool bOrbitBlocked )
    {
        const bool bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        _viewport.draw( size, ( bFocused || bHovered ) && bOrbitBlocked == false );
    }

    bool EditorPreviewPanel::recordPreviewModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label )
    {
        SceneManager* pSceneManager = findSubsystem<SceneManager>(); // 필수 서비스: 없으면 버그
        if ( pSceneManager == nullptr )
        {
            SW_LOG_ERROR( "[PreviewPanel] SceneManager is missing - the edit of '%#' is not recorded", obj.getName().c_str() );
            return false;
        }
        _commandStack.push( ObjectUndoUtil::makeModify( _commandStack, *pSceneManager, obj, before, after, label ) ); // 인스펙터 편집은 `_selection.recordModify` 로 같은 스택
        return true;
    }

    bool EditorPreviewPanel::readDocumentIsProbeAsset( float64& outValue ) const
    {
        GlobalVariableManager* pGvm = editor::findSubsystem<GlobalVariableManager>(); // 필수 서비스: 없으면 버그
        if ( pGvm == nullptr )
        {
            SW_LOG_ERROR( "[PreviewPanel] GlobalVariableManager is missing - the probe asset cannot be read" );
            return false;
        }
        const GlobalVariableInfo* pInfo = pGvm->findVariable( "gv_editorProbeAsset" );
        if ( pInfo == nullptr )
            return false;
        const string wanted = pInfo->getValueAsString();
        outValue            = wanted.empty() == false && hasDocument( wanted ) ? 1.0 : 0.0;
        return true;
    }

    void EditorPreviewPanel::applyWindowFocus( bool bFocused )
    {
        SceneManager* pSceneManager = findSubsystem<SceneManager>(); // 필수 서비스: 없으면 버그
        Scene*        pScene        = _viewport.getScene();
        if ( bFocused && pScene != nullptr )
        {
            if ( _bFocusApplied )
                return;
            if ( pSceneManager != nullptr )
                (void)pSceneManager->focusScene( pScene ); // 이 창의 씬이라 늘 알려진 씬이다
            else
                SW_LOG_ERROR( "[PreviewPanel] SceneManager is missing - the preview scene is not focused" );
            setFocusedCommandStack( &_commandStack );
            if ( _selection.isPreviewScope() )
                setFocusedSelection( &_selection ); // 포커스를 잃어도 남는다 — 인스펙터가 이 선택을 고친다(`getFocusedSelection`)
            _bFocusApplied = true;
            return;
        }
        if ( _bFocusApplied == false )
            return;
        // 이 창이 건 것만 푼다 — 그 사이 다른 창이 자기 것을 걸었으면 그대로 둔다.
        if ( getFocusedCommandStack() == &_commandStack )
            setFocusedCommandStack( nullptr );
        const bool bOwnSceneFocused = pSceneManager != nullptr && pScene != nullptr && pSceneManager->getFocusedScene() == pScene;
        if ( bOwnSceneFocused )
            pSceneManager->clearSceneFocus();
        _bFocusApplied = false;
    }
} // namespace sw::editor
