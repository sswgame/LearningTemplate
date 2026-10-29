/**
 * @file EditorWorkspaceWatches.cpp
 * @brief 편집 작업 공간 전체(플레이 상태 · 선택 · 되돌리기 · 씬 변경)를 읽는 에디터 감시 값(`Editor.*`)입니다. 한 패널의 것이 아니라 패널을 넘는 값이라 자체 테스트 폴더에 둡니다(작업 공간 폴더는 ImGui 를 모르는 코어라 EditorTest 도 받는데, 이 감시 값들은 EditorModule 에서만 읽는다).
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/HexUtil.h"

#include "DevTools/DebugHUD/DebugHUD.h"
#include "DevTools/RenderDoc/RenderDocCapture.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWindowTitle.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw::editor
{
    namespace
    {
        [[nodiscard]] bool readPlayState( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( static_cast<uint32>( EditorPlaySession::getState() ) );
            return true;
        }

        [[nodiscard]] bool readSceneDirty( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            outValue = pContext->getSubsystem<EditorWorkspace>().isSceneDirty() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 창 제목에 미저장 표시(`*`)가 있으면 1 입니다. 셸이 실제로 창에 설정한 제목을 읽습니다. */
        [[nodiscard]] bool readWindowTitleDirty( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
            if ( pWindowHost == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] watch Editor.WindowTitleDirty: MainWindowSubsystem is missing" );
                return false;
            }
            const IWindow* pWindow = pWindowHost->getWindow();
            if ( pWindow == nullptr )
                return false; // 창 없는 실행(헤드리스)에서는 값이 없다
            // 창에는 엔진이 프레임 끝에 건 제목(실행 정보 포함)이 있어 한 프레임 늦다 — 에디터가 정한 평소 제목을 본다.
            outValue = EditorWindowTitleUtil::isDirtyTitle( pWindowHost->getBaseTitle() ) ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readRenderDocAvailable( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = RenderDocCapture::isAvailable() ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readObjectCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObjectManager* pActive = editor::getActiveObjectManager();
            if ( pActive == nullptr )
                return false;
            uint32 count = 0;
            pActive->forEachGameObject( [&count]( const GameObject* pObj )
            {
                if ( pObj != nullptr && pObj->isPendingDestroy() == false )
                    ++count;
            } );
            outValue = static_cast<float64>( count );
            return true;
        }

        [[nodiscard]] bool readSelectionCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            outValue = static_cast<float64>( editor::getMainSceneSelection().getSelectedObjectCount() );
            return true;
        }

        [[nodiscard]] bool readSelectedIsEditorCamera( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            const GameObject*      pPrimary = editor::getMainSceneSelection().getPrimaryObject();
            const CameraComponent* pCamera  = pPrimary != nullptr ? pPrimary->getComponent<CameraComponent>() : nullptr;
            outValue                        = ( pCamera != nullptr && pCamera->getRole() == CameraRole::Viewport ) ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readUndoCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const CommandStack* pStack = editor::findSubsystem<CommandStack>();
            if ( pStack == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] watch Editor.UndoCount: CommandStack is missing" );
                return false;
            }
            outValue = static_cast<float64>( pStack->getCommandCount() );
            return true;
        }

        [[nodiscard]] bool readUndoIndex( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const CommandStack* pStack = editor::findSubsystem<CommandStack>();
            if ( pStack == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] watch Editor.UndoIndex: CommandStack is missing" );
                return false;
            }
            outValue = static_cast<float64>( pStack->getCurrentIndex() );
            return true;
        }

        /** @brief 툴이 겨누는 씬과 Undo 스택이 메인 씬의 것이면 1 입니다 — 포커스를 쥔 미리보기 창이 닫힌 뒤에도 남아 있으면 0 입니다(선택은 포커스를 잃어도 남는 것이 정상이라 보지 않는다). */
        [[nodiscard]] bool readToolFocusIsMain( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const CommandStack* pMainStack = editor::findSubsystem<CommandStack>();
            if ( pMainStack == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] watch Editor.ToolFocusIsMain: CommandStack is missing" );
                return false;
            }
            if ( editor::getActiveScene() == nullptr )
                return false; // 열린 씬이 없으면 값이 없다
            outValue = editor::getFocusedScene() == editor::getActiveScene() && editor::getFocusedCommandStack() == pMainStack ? 1.0 : 0.0;
            return true;
        }

        /** @brief 64 비트 값을 16 자리 16 진 글로 씁니다(수 감시 값의 float 비교에서 뭉개지지 않게 글로 낸다). */
        string formatHash( uint64 hash )
        {
            string text;
            HexUtil::appendUint64( text, hash );
            return text;
        }

        /**
         * @brief 편집 중인 씬의 내용 해시입니다. 오브젝트마다 (이름, 부모 이름, 상태 XML — 다른 오브젝트 참조는 빼고)를 이름 순으로 접습니다.
         * @details 씬 뷰 카메라(에디터 카메라 오브젝트)는 뺀다 — 카메라를 움직이는 것은 편집이 아니다. 상태 XML 은 저장과 같은 직렬화기라 인스펙터로 고친 값이 다 들어간다.
         *          `-gv_automationHashDump=1` 이면 오브젝트마다 해시를 로그로 남긴다(두 시점의 로그를 비교해 바뀐 오브젝트를 찾는다).
         */
        bool computeSceneContentHash( uint64& outHash )
        {
            const GameObjectManager* pActive = editor::getActiveObjectManager();
            if ( pActive == nullptr )
                return false;
            vector<std::pair<string, uint64>> listEntry;
            ObjectSaveOptions                 options{ SerializePurpose::Authoring };
            options._bOmitExternalParent = true;
            pActive->forEachGameObject( [&listEntry, &options]( const GameObject* pObject )
            {
                if ( pObject == nullptr || pObject->isPendingDestroy() )
                    return;
                const CameraComponent* pCamera = pObject->getComponent<CameraComponent>();
                if ( pCamera != nullptr && pCamera->getRole() == CameraRole::Viewport )
                    return;
                const GameObject* pParent = pObject->getParent();
                string            key     = string( pObject->getName().c_str() ) + "|" + ( pParent != nullptr ? string( pParent->getName().c_str() ) : string() );
                const string      state   = ObjectStateSerializer::saveToXMLString( pObject, options );
                listEntry.emplace_back( std::move( key ), HashUtil::computeStringHash64( state, false ) );
            } );
            std::sort( listEntry.begin(), listEntry.end() );
            const GlobalVariableInfo* pDump = editor::getSubsystem<GlobalVariableManager>().findVariable( "gv_automationHashDump" );
            const bool                bDump = pDump != nullptr && pDump->getValueAsInt() != 0;
            uint64                    hash  = HashUtil::kFNVOffset64;
            for ( const std::pair<string, uint64>& entry : listEntry )
            {
                hash = HashUtil::combine( hash, HashUtil::computeStringHash64( entry.first, false ) );
                hash = HashUtil::combine( hash, entry.second );
                if ( bDump )
                    SW_LOG_INFO( "[Scenario] scene hash item %# %#", entry.first.c_str(), formatHash( entry.second ).c_str() );
            }
            outHash = hash;
            return true;
        }

        /** @brief 고른 오브젝트 이름(이름 순, 쉼표로 이음). 아무것도 고르지 않았으면 `(none)` 입니다. */
        bool makeSelectedNames( string& outText )
        {
            EditorContext* pContext = getWatchContext( __func__ );
            if ( pContext == nullptr )
                return false;
            vector<GameObject*> listSelected;
            editor::getMainSceneSelection().getSelectedObjects( listSelected );
            vector<string> listName;
            for ( const GameObject* pObject : listSelected )
            {
                if ( pObject != nullptr )
                    listName.emplace_back( pObject->getName().c_str() );
            }
            std::sort( listName.begin(), listName.end() );
            outText.clear();
            for ( const string& name : listName )
            {
                if ( outText.empty() == false )
                    outText += ",";
                outText += name;
            }
            if ( outText.empty() )
                outText = "(none)";
            return true;
        }

        /** @brief 되돌리기 스택을 `<위치>/<수>:<레이블>|<레이블>…` 로 씁니다(아래에서 위로). */
        bool makeUndoState( string& outText )
        {
            const CommandStack* pStack = editor::findSubsystem<CommandStack>();
            if ( pStack == nullptr )
                return false;
            outText = to_string( pStack->getCurrentIndex() ) + "/" + to_string( pStack->getCommandCount() ) + ":";
            for ( size_t index = 0; index < pStack->getCommandCount(); ++index )
            {
                if ( index > 0 )
                    outText += "|";
                outText += pStack->getCommand( index )._label;
            }
            return true;
        }

        [[nodiscard]] bool readSceneHash( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
        {
            uint64 hash = 0;
            if ( computeSceneContentHash( hash ) == false )
            {
                outValue.setText( "no edited scene" );
                return false;
            }
            outValue.setText( formatHash( hash ) );
            return true;
        }

        [[nodiscard]] bool readSelectedNames( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
        {
            string text;
            if ( makeSelectedNames( text ) == false )
            {
                outValue.setText( "no editor context" );
                return false;
            }
            outValue.setText( text );
            return true;
        }

        [[nodiscard]] bool readUndoState( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
        {
            string text;
            if ( makeUndoState( text ) == false )
            {
                outValue.setText( "no undo stack" );
                return false;
            }
            outValue.setText( text );
            return true;
        }

        /** @brief 씬 내용 · 선택 · 되돌리기 스택 · 변경 표시를 한 해시로 접습니다. 경계 하나를 넘은 뒤 "편집 상태가 그대로다" 를 한 줄로 단언합니다. */
        [[nodiscard]] bool readEditorStateHash( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
        {
            uint64         sceneHash = 0;
            string         selected;
            string         undoState;
            EditorContext* pContext  = editor::findSubsystem<EditorContext>();
            const bool     bReadable = pContext != nullptr && computeSceneContentHash( sceneHash ) && makeSelectedNames( selected ) && makeUndoState( undoState );
            if ( bReadable == false )
            {
                outValue.setText( "no editor state" );
                return false;
            }
            uint64 hash = HashUtil::combine( sceneHash, HashUtil::computeStringHash64( selected, false ) );
            hash        = HashUtil::combine( hash, HashUtil::computeStringHash64( undoState, false ) );
            hash        = HashUtil::combine( hash, pContext->getSubsystem<EditorWorkspace>().isSceneDirty() ? 1u : 0u );
            outValue.setText( formatHash( hash ) );
            return true;
        }

        [[nodiscard]] bool readLoadingScreenShown( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = editor::getSubsystem<UISystem>().isLoadingScreenShown() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 플레이 세션이 Simulate(조종 없이 월드만 실행)이면 1, 플레이어가 조종하면 0 입니다. 정지 상태이면 값을 내지 않습니다. */
        [[nodiscard]] bool readPlaySimulating( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( EditorPlaySession::isStopped() )
                return false;
            outValue = EditorPlaySession::isSimulating() ? 1.0 : 0.0;
            return true;
        }

        /** @brief `-gv_editorWatchObject` 오브젝트 자신의 활성 비트(1 또는 0)입니다. */
        [[nodiscard]] bool readObjectActive( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObject* pObject = findWatchedObject();
            if ( pObject == nullptr )
                return false;
            outValue = pObject->isActive() ? 1.0 : 0.0;
            return true;
        }

        /** @brief `-gv_editorWatchObject` 오브젝트를 에디터에서만 숨겼으면 1 입니다. */
        [[nodiscard]] bool readObjectHiddenInEditor( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const GameObject* pObject = findWatchedObject();
            if ( pObject == nullptr )
                return false;
            outValue = pObject->isRenderHidden() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 게임 디버그 HUD 의 켜짐(`debug.hud`) 상태입니다. 화면이 열렸는지(엔진 프로브 `DebugHUD.Shown`)와 달리 정지 상태에서도 켜짐 여부를 반환합니다. 조종자가 없으면 값을 내지 않습니다. */
        [[nodiscard]] bool readDebugHUDEnabled( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const DebugHUD* pHUD = editor::findSubsystem<DebugHUD>();
            if ( pHUD == nullptr )
                return false; // HUD 를 아직 만들지 않았다
            outValue = pHUD->isShown() ? 1.0 : 0.0;
            return true;
        }

        /** @brief 켜진 디버그 HUD 섹션 수입니다. HUD 가 꺼져 있어도 셉니다. */
        [[nodiscard]] bool readDebugHUDShownSections( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( DebugHUD::getShownSectionCount() );
            return true;
        }

        /** @brief 렌더 스레드에 살아 있는 미리보기 씬 렌더러 수입니다(창 하나에 하나). 창을 닫은 다음 렌더 프레임에 줄어듭니다. 렌더러가 없으면 false 입니다. */
        [[nodiscard]] bool readScenePreviewRenderers( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const FrameRenderer* pRenderer = editor::findSubsystem<FrameRenderer>();
            if ( pRenderer == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] watch ScenePreviewRenderers: FrameRenderer is missing" );
                return false;
            }
            outValue = static_cast<float64>( pRenderer->getScenePreviewRendererCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorPlayState, "Editor.PlayState", "Play session state: 0 stopped, 1 playing, 2 paused", &readPlayState );
    SW_AUTOMATION_WATCH( editorPlaySimulating, "Editor.PlaySimulating", "While a session runs: 1 simulating (ejected or Simulate), 0 the player is possessed",
                         &readPlaySimulating );
    SW_AUTOMATION_WATCH( editorSceneDirty, "Editor.SceneDirty", "1 while the edited scene has unsaved changes", &readSceneDirty );
    SW_AUTOMATION_WATCH( editorWindowTitleDirty, "Editor.WindowTitleDirty", "1 when the editor window title carries the unsaved mark (*)",
                         &readWindowTitleDirty );
    SW_AUTOMATION_WATCH( editorRenderDocAvailable, "Editor.RenderDocAvailable", "1 when RenderDoc is attached (-renderdoc or launched from RenderDoc)",
                         &readRenderDocAvailable );
    SW_AUTOMATION_WATCH( editorObjectCount, "Editor.ObjectCount", "Objects in the active scene", &readObjectCount );
    SW_AUTOMATION_WATCH( editorSelectionCount, "Editor.SelectionCount", "Selected objects", &readSelectionCount );
    SW_AUTOMATION_WATCH( editorSelectedIsEditorCamera, "Editor.SelectedIsEditorCamera", "1 when the primary selection is the viewport's editor camera",
                         &readSelectedIsEditorCamera );
    SW_AUTOMATION_WATCH( editorUndoCount, "Editor.UndoCount", "Commands on the undo stack", &readUndoCount );
    SW_AUTOMATION_WATCH( editorUndoIndex, "Editor.UndoIndex", "Position on the undo stack (commands not undone)", &readUndoIndex );
    SW_AUTOMATION_WATCH( editorToolFocusIsMain, "Editor.ToolFocusIsMain", "1 when the scene and undo stack tools aim at are the main scene's (0 while a preview window holds the focus)",
                         &readToolFocusIsMain );
    SW_AUTOMATION_QUERY_WATCH( editorSceneHash, "Editor.SceneHash", "Hash of the edited scene content (every object's name, parent and saved state; the scene view camera is left out)",
                               &readSceneHash, false, false );
    SW_AUTOMATION_QUERY_WATCH( editorSelectedNames, "Editor.SelectedNames", "Selected object names, sorted and comma separated ('(none)' when nothing is selected)",
                               &readSelectedNames, false, false );
    SW_AUTOMATION_QUERY_WATCH( editorUndoState, "Editor.UndoState", "Undo stack as '<index>/<count>:<label>|<label>...' from the oldest command",
                               &readUndoState, false, false );
    SW_AUTOMATION_QUERY_WATCH( editorStateHash, "Editor.StateHash", "Hash of the edited scene content, the selection, the undo stack and the unsaved flag",
                               &readEditorStateHash, false, false );
    SW_AUTOMATION_WATCH( editorLoadingScreenShown, "Editor.LoadingScreenShown", "1 while the runtime UI shows its loading screen",
                         &readLoadingScreenShown );
    SW_AUTOMATION_WATCH( editorObjectActive, "Editor.ObjectActive", "Own active bit of the object named by gv_editorWatchObject", &readObjectActive );
    SW_AUTOMATION_WATCH( editorObjectHiddenInEditor, "Editor.ObjectHiddenInEditor", "1 when the object named by gv_editorWatchObject is hidden in the editor only",
                         &readObjectHiddenInEditor );
    SW_AUTOMATION_WATCH( editorDebugHUDEnabled, "Editor.DebugHUDEnabled", "1 when the game debug HUD is switched on (debug.hud), even while stopped",
                         &readDebugHUDEnabled );
    SW_AUTOMATION_WATCH( editorDebugHUDShownSections, "Editor.DebugHUDShownSections", "Debug HUD sections switched on (debug.hudSections plus defaults)",
                         &readDebugHUDShownSections );
    SW_AUTOMATION_WATCH( editorScenePreviewRenderers, "Editor.ScenePreviewRenderers",
                         "Scene preview renderers alive on the render thread (one per open preview window, retired the render frame after it closes)",
                         &readScenePreviewRenderers );
} // namespace sw::editor
