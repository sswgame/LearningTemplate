#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/ObjectUndoUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 감시 값 `Editor.PreviewTickCount` — 씬 매니저가 미리보기 씬들을 틱한 횟수의 합입니다. 숨은 창이면 늘지 않아야 한다. */
        [[nodiscard]] bool readPreviewTickCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SceneManager* pSceneManager = findSubsystem<SceneManager>();
            if ( pSceneManager == nullptr )
                return false;
            uint64 tickCount = 0;
            for ( const PreviewSceneEntry& entry : pSceneManager->getPreviewScenes() )
            {
                tickCount += entry._tickCount;
            }
            outValue = static_cast<float64>( tickCount );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorPreviewTickCount, "Editor.PreviewTickCount", "Ticks the scene manager gave all preview scenes (a hidden preview window adds none)",
                         &readPreviewTickCount );

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
        // 틱은 창이 이번 프레임에 그려질 때만이다(`ScenePreviewTick::WhenDrawn`) — 뒤 탭 · 접힌 창의 씬은 시간이 멈춘다.
        ScenePreviewPolicy policy;
        policy._tick     = ScenePreviewTick::WhenDrawn;
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
        _commandStack.push( ObjectUndoUtil::makeModify( editor::getSubsystem<SceneManager>(), obj, before, after, label ) ); // 인스펙터 편집은 `_selection.recordModify` 로 같은 스택
        return true;
    }

    bool EditorPreviewPanel::readDocumentIsProbeAsset( float64& outValue ) const
    {
        const GlobalVariableInfo* pInfo = editor::getSubsystem<GlobalVariableManager>().findVariable( "gv_editorWatchAsset" );
        if ( pInfo == nullptr )
            return false;
        const string wanted = pInfo->getValueAsString();
        outValue            = wanted.empty() == false && hasDocument( wanted ) ? 1.0 : 0.0;
        return true;
    }

    void EditorPreviewPanel::applyWindowFocus( bool bFocused )
    {
        Scene* pScene = _viewport.getScene();
        if ( bFocused && pScene != nullptr )
        {
            if ( _bFocusApplied )
                return;
            (void)editor::getSubsystem<SceneManager>().focusScene( pScene ); // 이 창의 씬이라 늘 알려진 씬이다
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
        // 소멸자 · 종료에서도 오는 길이라 씬 관리자가 먼저 내려갔을 수 있다 — 초점 씬을 풀 대상이 없을 뿐 오류가 아니다.
        SceneManager* pSceneManager    = findSubsystem<SceneManager>(); // 종료 순서에 따라 이미 없을 수 있다
        const bool    bOwnSceneFocused = pSceneManager != nullptr && pScene != nullptr && pSceneManager->getFocusedScene() == pScene;
        if ( bOwnSceneFocused )
            pSceneManager->clearSceneFocus();
        _bFocusApplied = false;
    }
} // namespace sw::editor
