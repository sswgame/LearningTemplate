/**
 * @file EditorInputActions.cpp
 * @brief 에디터 입력 어댑터 — 포커스 패널 찾기 · 키보드 주인 · ImGui 키 넘기기 · 메뉴 명령 줄. 맵 · 판정은 `EditorShortcutMap`(Model)이다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorInputActions.h"

#include "Core/Container/vector.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/RawInputEvent.h"

#include <imgui.h>

namespace sw::editor
{
    EditorInputActions* EditorInputActions::findInEditorContext()
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorInputActions>() : nullptr;
    }

    EditorInputActions& EditorInputActions::getInEditorContext()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>();
    }

    namespace
    {
        /** @brief 에디터 UI 가 넘긴 키의 뗌 이벤트입니다(`_bEditorUI` — 배타 가상 입력이 OS 이벤트를 버려도 남는다). */
        RawInputEvent makeForwardedKeyUp( Key key )
        {
            RawInputEvent event = RawInputEvent::makeKeyUp( key );
            event._bEditorUI    = SW_TRUE;
            return event;
        }
    } // namespace

    EditorInputActions::EditorInputActions()
        : _shortcuts{}
        , _listQueuedCommand{}
        , _arrForwardedKey{}
        , _focusBeforeTool{ InputKeyboardFocus::Game }
        , _bToolOwnsKeyboard{ SW_FALSE }
        , _bCommandsSuspended{ SW_FALSE }
    {
    }

    EditorInputActions::~EditorInputActions() = default;

    span<const utf8* const> EditorInputActions::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorCommandRegistry::kSubsystemName, EditorPanelManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult EditorInputActions::initializeSubsystem()
    {
        // 엔진 입력 매니저는 클라이언트 호스트에 늘 있다(필수) — 에디터 키도 같은 장치 · 같은 맵 규칙으로 판정한다.
        return _shortcuts.connect( editor::getSubsystem<InputManager>() ) ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
    }

    void EditorInputActions::shutdownSubsystem()
    {
        // 입력 매니저는 에디터보다 오래 산다 — 키보드 주인과 넣은 키를 돌려 두고, 맵이 그것을 가리킨 채 남지 않게 끊는다(끊긴 맵의 `update` 는 아무것도 하지 않는다).
        InputManager& input = editor::getSubsystem<InputManager>();
        releaseForwardedKeys( input );
        if ( _bToolOwnsKeyboard && input.getKeyboardFocus() == InputKeyboardFocus::Tool )
            input.setKeyboardFocus( _focusBeforeTool );
        _bToolOwnsKeyboard = SW_FALSE;
        _shortcuts.disconnect();
    }

    void EditorInputActions::processFrame( float32 deltaSeconds )
    {
        _shortcuts.rebuildIfExtensionsChanged();
        // 지난 프레임 메뉴에서 고른 명령부터 — 패널을 그리기 전이라 그리는 중인 목록이 없다. 실행이 다시 넣은 것은 다음 프레임이다.
        vector<QueuedCommand> listRun;
        listRun.swap( _listQueuedCommand );
        EditorCommandRegistry& registry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        for ( const QueuedCommand& queued : listRun )
        {
            (void)registry.request( queued._commandID, queued._target ); // 그 사이 꺼졌으면 아무 일도 없다(메뉴의 회색과 같은 조건). 씬 편집은 게임 스레드가 실행한다
        }
        forwardEditorKeyboard( decideKeyboardOwner() );
        syncFocusedPanelLayer();
        _shortcuts.update( deltaSeconds );
        // ImGui 의 클릭 · 끌기 · 더블클릭 판정(항목 단추 · 맥락 메뉴 · 끌어 놓기)도 입력 맵의 값 하나를 쓴다 — 두 곳에 숫자를 두지 않는다.
        const InputMap& map        = _shortcuts.getInputMap();
        ImGuiIO&        io         = ImGui::GetIO();
        io.MouseDragThreshold      = map.getDragThreshold();
        io.MouseDoubleClickTime    = map.getDoubleClickTime();
        io.MouseDoubleClickMaxDist = map.getDoubleClickMaxDistance();
        // 글을 치는 중에는 Ctrl+S 같은 단축키도 그 글 칸의 것이다(예전 처리와 같다). 재배치 창이 키를 받는 중에도 실행하지 않는다.
        if ( areActionsBlocked() )
            return;
        // 발화 판정은 Model 이 한다. 실행은 판정이 끝난 뒤 — 명령이 등록부를 다시 만들어도(모듈 리로드) 순회 중인 목록을 읽지 않는다.
        vector<string> listCommandID;
        _shortcuts.collectTriggeredCommands( registry.getCommands(), listCommandID );
        for ( const string& commandID : listCommandID )
        {
            (void)registry.request( commandID, EditorCommandTarget::none() ); // 꺼진 명령이면 아무 일도 없다(메뉴의 회색과 같은 조건). 씬 편집은 게임 스레드가 실행한다
        }
    }

    void EditorInputActions::queueCommand( string_view commandID, const EditorCommandTarget& target )
    {
        _listQueuedCommand.push_back( QueuedCommand{ string( commandID ), target } );
    }

    bool EditorInputActions::areActionsBlocked() const
    {
        return _bCommandsSuspended || ImGui::GetIO().WantTextInput;
    }

    bool EditorInputActions::wasActionTriggered( string_view action ) const
    {
        return areActionsBlocked() == false && _shortcuts.wasActionTriggered( action );
    }

    bool EditorInputActions::isActionDown( string_view action ) const
    {
        return areActionsBlocked() == false && _shortcuts.isActionDown( action );
    }

    bool EditorInputActions::decideKeyboardOwner()
    {
        // 패널을 그리기 전이라 이번 프레임 포커스는 아직 없다 — 지난 프레임에 게임 뷰 · 게임 입력 면(한 프로세스 재생 뷰포트)이 포커스였는지를 본다.
        EditorRenderViews& views            = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>();
        const bool         bGameViewFocused = views.wasGameInputFocusedLastFrame();
        const bool         bEditorOwns      = EditorPlaySession::getUIState().isSimulating() || ( ImGui::GetIO().WantCaptureKeyboard && bGameViewFocused == false );
        views.setEditorOwnsKeyboard( bEditorOwns ); // 엔진 포커스는 게임 스레드가 넘긴 값으로 입힌다(`applyKeyboardOwner`)
        return bEditorOwns;
    }

    void EditorInputActions::applyKeyboardOwner( bool bEditorOwns )
    {
        InputManager&            input   = editor::getSubsystem<InputManager>();
        const InputKeyboardFocus current = input.getKeyboardFocus();
        if ( bEditorOwns && current != InputKeyboardFocus::Tool )
        {
            _focusBeforeTool   = current;
            _bToolOwnsKeyboard = SW_TRUE;
            input.setKeyboardFocus( InputKeyboardFocus::Tool );
            return;
        }
        if ( bEditorOwns == false && _bToolOwnsKeyboard )
        {
            // 그동안 다른 주인(콘솔)이 포커스를 바꿨으면 그쪽을 존중하고 돌리지 않는다.
            if ( current == InputKeyboardFocus::Tool )
                input.setKeyboardFocus( _focusBeforeTool );
            _bToolOwnsKeyboard = SW_FALSE;
        }
    }

    void EditorInputActions::forwardEditorKeyboard( bool bEditorOwnsKeyboard )
    {
        InputManager& input = editor::getSubsystem<InputManager>();
        if ( bEditorOwnsKeyboard == false )
        {
            releaseForwardedKeys( input );
            return;
        }
        vector<RawInputEvent> listEvent;
        ImGuiKeyForward::appendKeyChanges( listEvent );
        for ( RawInputEvent& event : listEvent )
        {
            event._bEditorUI = SW_TRUE; // 배타 가상 입력(시나리오)이 OS 이벤트를 버려도 이 키는 남는다
            if ( input.postRawEvent( event ) == false )
                continue; // 큐가 차면 이 사건만 잃는다
            const size_t keyIndex = static_cast<size_t>( event._payload._keyData._key );
            if ( event._type == RawInputEventType::KeyDown )
                _arrForwardedKey[keyIndex] = true;
            else if ( event._type == RawInputEventType::KeyUp )
                _arrForwardedKey[keyIndex] = false;
        }
    }

    void EditorInputActions::releaseForwardedKeys( InputManager& input )
    {
        for ( size_t keyIndex = 0; keyIndex < std::size( _arrForwardedKey ); ++keyIndex )
        {
            // 큐가 차서 못 넣은 뗌은 다음 프레임에 다시 시도한다.
            if ( _arrForwardedKey[keyIndex] && input.postRawEvent( makeForwardedKeyUp( static_cast<Key>( keyIndex ) ) ) )
                _arrForwardedKey[keyIndex] = false;
        }
    }

    void EditorInputActions::syncFocusedPanelLayer()
    {
        const EditorPanelManager& panelManager = editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>();
        string_view               focusedPanelID{};
        for ( const EditorPanelEntry& entry : panelManager.getPanels() )
        {
            if ( entry._pInstance != nullptr && entry._pInstance->isOpen() && entry._pInstance->isWindowFocused() )
            {
                focusedPanelID = entry._id;
                break;
            }
        }
        _shortcuts.setFocusedPanel( focusedPanelID );
    }
} // namespace sw::editor
