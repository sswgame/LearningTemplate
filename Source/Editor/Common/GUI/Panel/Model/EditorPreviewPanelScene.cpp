// 미리보기 창 바탕의 미리보기 씬 · 선택 · 창 스택 · 시간 배율입니다(ImGui 없음). 그리기는 `EditorPreviewPanel.cpp`(MVVM View)다.
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 가장 최근에 그린 미리보기 창의 재생 막대 상태 — 감시 값이 읽는다(창은 RTTI 없이 찾을 수 없다). */
        struct EditorPreviewBarState
        {
            float32 _timeScale{ 1.0f };
            bool    _bFloorShown{ SW_TRUE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool    _bAxesShown{ SW_TRUE };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        struct EditorPreviewPanelInternal
        {
            static constexpr uint32  kDefaultSpeedIndex = 3;
            static constexpr float32 kPausedTimeScale   = 0.0f; ///< 멈춘 미리보기 씬의 시간 배율
        };

        [[nodiscard]] bool readPreviewTimeScale( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( getEditorState<EditorPreviewBarState>()._timeScale );
            return true;
        }

        [[nodiscard]] bool readPreviewFloorShown( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = getEditorState<EditorPreviewBarState>()._bFloorShown ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readPreviewAxesShown( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = getEditorState<EditorPreviewBarState>()._bAxesShown ? 1.0 : 0.0;
            return true;
        }

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

        /** @brief 감시 값 `Editor.PreviewSceneCount` — 씬 매니저가 가진 미리보기 씬 수입니다. 미리보기 창을 닫으면 그 창의 씬이 빠진다(감사 E-14). */
        [[nodiscard]] bool readPreviewSceneCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SceneManager* pSceneManager = findSubsystem<SceneManager>();
            if ( pSceneManager == nullptr )
                return false;
            outValue = static_cast<float64>( pSceneManager->getPreviewScenes().size() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorPreviewTimeScale, "Editor.PreviewTimeScale", "Time scale the preview bar of the preview window drawn most recently gave its scene (0 while paused)",
                         &readPreviewTimeScale );
    SW_AUTOMATION_WATCH( editorPreviewFloorShown, "Editor.PreviewFloorShown", "1 when the preview window drawn most recently shows its floor", &readPreviewFloorShown );
    SW_AUTOMATION_WATCH( editorPreviewAxesShown, "Editor.PreviewAxesShown", "1 when the preview window drawn most recently draws its axis indicator", &readPreviewAxesShown );
    SW_AUTOMATION_WATCH( editorPreviewSceneCount, "Editor.PreviewSceneCount", "Preview scenes the scene manager holds (a closed preview window holds none)", &readPreviewSceneCount );
    SW_AUTOMATION_WATCH( editorPreviewTickCount, "Editor.PreviewTickCount", "Ticks the scene manager gave all preview scenes (a hidden preview window adds none)",
                         &readPreviewTickCount );

    EditorPreviewPanel::EditorPreviewPanel( const EditorAssetType& kind, bool bLoadOnOpen )
        : EditorDocumentPanel{ kind, bLoadOnOpen }
        , _viewport{}
        , _pSelection{ make_unique<EditorSelection>() }
        , _bFocusApplied{ SW_FALSE }
        , _bPreviewPaused{ SW_FALSE }
        , _bFloorShown{ SW_TRUE }
        , _bAxesShown{ SW_TRUE }
        , _bFloorChanged{ SW_FALSE }
        , _seenCommandCount{ 0 }
        , _previewBarSize{}
        , _speedIndex{ EditorPreviewPanelInternal::kDefaultSpeedIndex }
        , _stepFramesLeft{ 0 }
    {
    }

    EditorPreviewPanel::~EditorPreviewPanel()
    {
        applyWindowFocus( false );
        _pSelection->unbindPreviewScope();
        _viewport.shutdown();
    }

    void EditorPreviewPanel::shutdown()
    {
        releasePreview();
    }

    void EditorPreviewPanel::releasePreview()
    {
        applyWindowFocus( false );
        resetDocumentHistory(); // 창 스택의 명령은 이 씬의 키를 가리킨다 — 씬과 함께 버린다(되돌리기 어려운 결정 4)
        _pSelection->unbindPreviewScope();
        _viewport.shutdown();
    }

    void EditorPreviewPanel::acknowledgePreviewEdits()
    {
        _seenCommandCount = getDocumentCommandStack().getCommandCount();
    }

    void EditorPreviewPanel::resetDocumentHistory()
    {
        EditorDocumentPanel::resetDocumentHistory();
        _seenCommandCount = 0; // 비운 스택을 "편집이 줄었다" 로 읽어 dirty 로 만들지 않는다
    }

    void EditorPreviewPanel::syncDocumentDirty()
    {
        const size_t count = getDocumentCommandStack().getCommandCount();
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

    void EditorPreviewPanel::ensurePreviewScene()
    {
        if ( _viewport.isInitialized() || _viewport.initialize( getPanelTitle(), getPreviewPolicy(), getPreviewEnvironment() ) == false )
            return;
        Scene* pScene = _viewport.getScene();
        if ( pScene == nullptr )
            return;
        _pSelection->bindPreviewScope( *pScene, getDocumentCommandStack() );
        populatePreviewScene( *pScene );
        framePreviewSubject(); // 처음 카메라 = 맞춤(F) — 창마다 따로 두던 frameSphere 를 한 곳으로
    }

    void EditorPreviewPanel::publishPreviewBarState( float32 speed ) const
    {
        EditorPreviewBarState& state = getEditorState<EditorPreviewBarState>();
        state._timeScale             = _bPreviewPaused ? EditorPreviewPanelInternal::kPausedTimeScale : speed;
        state._bFloorShown           = _bFloorShown && _viewport.hasFloor();
        state._bAxesShown            = _bAxesShown;
    }

    void EditorPreviewPanel::preparePreviewFrame( bool bFocused )
    {
        ensurePreviewScene();
        applyWindowFocus( bFocused );
    }

    void EditorPreviewPanel::applyPreviewBar( float32 speed )
    {
        if ( _bFloorChanged )
        {
            _viewport.setFloorVisible( _bFloorShown );
            _bFloorChanged = SW_FALSE;
        }
        Scene*        pScene        = _viewport.getScene();
        SceneManager* pSceneManager = findSubsystem<SceneManager>();
        const bool    bOwnScene     = pScene != nullptr && pSceneManager != nullptr && pSceneManager->findPreviewPolicy( pScene ) != nullptr;
        if ( bOwnScene == false )
            return; // 붙인 씬(넷 재생)은 가진 쪽이 시간을 정한다
        const bool    bStep  = _bPreviewPaused && _stepFramesLeft > 0;
        const float32 wanted = _bPreviewPaused && bStep == false ? EditorPreviewPanelInternal::kPausedTimeScale : speed;
        if ( bStep )
            --_stepFramesLeft;
        const ScenePreviewPolicy* pPolicy = pSceneManager->findPreviewPolicy( pScene );
        if ( pPolicy->_timeScale == wanted )
            return;
        ScenePreviewPolicy policy = *pPolicy;
        policy._timeScale         = wanted;
        (void)pSceneManager->setPreviewPolicy( pScene, policy ); // 이 창이 만든 미리보기 씬이라 늘 성공한다
    }

    bool EditorPreviewPanel::recordPreviewModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label )
    {
        getDocumentCommandStack().push( ObjectSnapshot::makeModify( editor::getSubsystem<SceneManager>(), obj, before, after, label ) ); // 인스펙터 편집은 선택의 `recordModify` 로 같은 스택
        return true;
    }

    bool EditorPreviewPanel::readDocumentIsWatchAsset( float64& outValue ) const
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
            if ( _pSelection->isPreviewScope() )
                setFocusedSelection( _pSelection.get() ); // 포커스를 잃어도 남는다 — 인스펙터가 이 선택을 고친다(`getFocusedSelection`)
            _bFocusApplied = SW_TRUE;
            return;
        }
        if ( _bFocusApplied == false )
            return;
        // 소멸자 · 종료에서도 오는 길이라 씬 관리자가 먼저 내려갔을 수 있다 — 초점 씬을 풀 대상이 없을 뿐 오류가 아니다.
        SceneManager* pSceneManager    = findSubsystem<SceneManager>(); // 종료 순서에 따라 이미 없을 수 있다
        const bool    bOwnSceneFocused = pSceneManager != nullptr && pScene != nullptr && pSceneManager->getFocusedScene() == pScene;
        if ( bOwnSceneFocused )
            pSceneManager->clearSceneFocus();
        _bFocusApplied = SW_FALSE;
    }
} // namespace sw::editor
