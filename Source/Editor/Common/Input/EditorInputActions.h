/**
 * @file EditorInputActions.h
 * @brief 에디터 입력 맵(`editor.input.xml` + 확장의 맵)을 소유하고, 포커스 창의 레이어를 켜고, 발화한 액션의 명령을 실행하고, 사용자 재배치를 저장합니다.
 * @details 에디터 단축키는 엔진 입력 맵(`InputMap`) 하나가 판정한다(EditorUX 10-4). 키 · 수정 키 · 트리거 · 사용자 재배치 파일 형식을 에디터가 따로 두지 않는다.
 *          이 서브시스템은 입력 맵에 없는 에디터 고유의 것만 맡는다: 어느 패널 창이 포커스인지(레이어 켜기), 키보드 주인(도구 포커스), 액션 → 명령 연결.
 *          유니티 단축키 관리자의 컨텍스트, 언리얼 위젯별 `FUICommandList` 와 같은 선이다(기억).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw::editor
{
    /**
     * @struct EditorInputMapRegistration
     * @brief 확장 모듈이 에디터 입력 맵에 자기 액션 · 레이어를 더하는 등록 줄입니다(`SW_EDITOR_INPUT_MAP`).
     * @details 파일은 `InputMap::appendFromResource` 규칙을 따른다 — 맵 전체 속성을 적지 않고, 있는 액션 · 레이어를 다시 정의하지 않고,
     *          같은 레이어의 같은 입력을 다른 액션과 나누지 않는다. 확장 명령의 `SW_EDITOR_COMMAND` 입력 액션 칸이 이 파일의 액션을 가리킨다.
     */
    struct EditorInputMapRegistration : EditorExtensionRegistration
    {
        static constexpr const utf8* kKindName = "inputMap";

        const utf8* _pResourcePath; ///< 리소스 경로(`editor/input/…` 또는 확장 팩의 경로)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorInputActions
     * @brief 에디터 입력 맵의 주인입니다. `EditorContext` 가 명령 등록부 · 패널 매니저 뒤에 만들고 앞에 지웁니다.
     * @details 레이어 규칙: `Editor.Global` 은 늘 켜져 있고, `Editor.Panel.<패널 id>` 는 그 패널 창이 포커스일 때만 켜진다.
     *          같은 누름으로 포커스 패널 레이어의 액션과 전역 액션이 함께 발화하면 패널 쪽만 실행한다(포커스 창 → 바깥 → 전역).
     *          텍스트 입력 중(ImGui `WantTextInput`)이거나 재배치 창이 키를 받는 중이면 맵은 갱신하되 명령은 실행하지 않는다.
     */
    class SW_EDITOR_API EditorInputActions final : public EditorSubsystem
    {
    public:
        /** @brief 늘 켜진 전역 레이어 이름입니다. */
        static constexpr const utf8* kGlobalLayer = "Editor.Global";
        /** @brief 패널 레이어 이름의 앞부분입니다. 레이어 이름은 이 뒤에 패널 id(`EditorPanelEntry::_id`)를 붙인 것입니다. */
        static constexpr const utf8* kPanelLayerPrefix = "Editor.Panel.";
        /** @brief 사용자 재배치 파일 이름입니다(에디터 상태 폴더 — `Saved/Editor`). 형식은 셸 맵과 같다(`ShellInputActions::saveUserBindings`). */
        static constexpr const utf8* kUserBindingsFileName = "editorbindings.xml";

        EditorInputActions();
        ~EditorInputActions() override;
        EditorInputActions( const EditorInputActions& )            = delete;
        EditorInputActions& operator=( const EditorInputActions& ) = delete;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        static constexpr const utf8* kSubsystemName = "EditorInputActions";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        /** @brief 입력 맵을 엔진 입력 매니저에 붙이고 `editor.input.xml` · 확장 맵 · 사용자 재배치를 읽습니다. 못 읽으면 실패입니다(단축키 없는 에디터로 서지 않는다). */
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;

        /**
         * @brief 한 프레임: 확장 맵이 바뀌었으면 맵을 다시 만들고, 키보드 주인을 맞추고, 에디터 창의 키를 넘기고, 포커스 창의 레이어를 켜고,
         *        맵을 갱신하고, 발화한 액션의 명령을 실행합니다. ImGui 프레임 안에서, 엔진 입력 매니저의 `beginFrame` 뒤에 부릅니다.
         */
        void processFrame( float32 deltaSeconds );
        /** @brief 에디터 입력 맵입니다(값으로 소유 — 늘 있다. 초기화 전 · 종료 뒤에는 입력 매니저가 끊겨 갱신해도 아무것도 하지 않는다). */
        InputMap&       getInputMap() { return _map; }
        const InputMap& getInputMap() const { return _map; }
        /** @brief 지금 켜진 패널 레이어 이름입니다. 포커스 패널에 레이어가 없으면 빈 이름입니다. */
        const hashed_string& getFocusedPanelLayer() const { return _focusedPanelLayer; }
        /** @brief 단축키 재배치 창이 키를 받는 동안 명령 실행을 멈춥니다. 맵은 계속 갱신합니다. */
        void setCommandsSuspended( bool bSuspended ) { _bCommandsSuspended = bSuspended; }
        /**
         * @brief 패널 · 뷰포트가 읽는 액션(명령이 아닌 것 — 화살표 행 이동, 비행 키, 이름 바꾸기 시작)이 이번 프레임에 발화했으면 true 입니다.
         * @details 글을 치는 중이거나 재배치 창이 키를 받는 중이면 false 다(명령 실행과 같은 조건). 범위는 액션의 레이어가 정한다 — 그 패널이 포커스일 때만 켜진다.
         */
        bool wasActionTriggered( string_view action ) const;
        /** @brief 액션이 눌려 있으면 true 입니다(비행처럼 누르는 동안 하는 일). 조건은 `wasActionTriggered` 와 같습니다. */
        bool isActionDown( string_view action ) const;
        /** @brief 몸짓 동안만 켜는 모드 레이어를 켜고 끕니다(뷰포트 비행). 맵에 없는 레이어면 부른 쪽의 잘못이라 멈춥니다. 바뀐 것은 다음 `processFrame` 의 갱신부터 보입니다. */
        void setModeLayerEnabled( string_view layer, bool bEnabled );
        /**
         * @brief 메뉴에서 고른 명령을 다음 `processFrame` 첫머리에 실행하도록 넣습니다(패널을 그리기 전).
         * @details 맥락 메뉴는 패널이 목록을 그리는 중에 닫힌다. 그 자리에서 실행하면 지운 컴포넌트 · 오브젝트를 그리던 루프가 계속 읽는다 —
         *          실행을 프레임 경계로 미뤄 그리는 중인 목록이 명령 뒤에 남지 않게 한다. 글을 치는 중이어도 실행한다(고른 것이다).
         */
        void queueCommand( string_view commandID, const EditorCommandTarget& target );

        // ------------------------------------------------------------------------------
        // 단축키 글자 · 재배치(단축키 창이 쓴다). 바꾼 것은 곧바로 사용자 재배치 파일에 저장한다
        // ------------------------------------------------------------------------------
        /** @brief 액션의 키보드 바인딩을 메뉴 글자(`Ctrl+Shift+P / Ctrl+Space`)로 만듭니다. 액션이 없거나 빈 이름이면 빈 글입니다. */
        void formatShortcutLabel( string_view action, string& outLabel ) const;
        /**
         * @brief 액션의 첫 키보드 바인딩을 @p key + @p modifierMask 로 바꿉니다. 같은 레이어의 다른 액션과 겹치면 바꾸지 않고 그 이름을 @p outConflictAction 에 담고 false 입니다.
         * @details 겹침은 엔진 맵의 규칙(`InputMap::findSameLayerInputConflict`)으로 본다. 바꿨으면 저장한다.
         */
        [[nodiscard]] bool rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction );
        /** @brief @p action 을 @p key + @p modifierMask 로 바꾸고, 겹치던 @p otherAction 에는 @p action 의 옛 조합을 줍니다(바꾸기). 저장합니다. */
        [[nodiscard]] bool swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask );
        /** @brief 액션의 바인딩을 기본으로 되돌리고 저장합니다. */
        void resetShortcut( string_view action );
        /** @brief 모든 바인딩을 기본으로 되돌리고 저장합니다(재배치 파일은 지워진다). */
        void resetAllShortcuts();
        /** @brief 액션의 첫 바인딩이 기본과 다르면 true 입니다. */
        bool isShortcutModified( string_view action ) const;
        /** @brief 기본과 다른 액션 수입니다(탐침 `Editor.ShortcutOverrideCount`). */
        uint32 countModifiedShortcuts() const;
        /** @brief 사용자 재배치 파일 경로입니다(`Saved/Editor/editorbindings.xml`). */
        static string getUserBindingsPath();

    private:
        /** @brief 기본 맵을 다시 읽고, 확장 맵을 더하고, 같은 레이어 겹침을 검사하고, 사용자 재배치를 입힙니다. 실패하면 false 입니다. */
        [[nodiscard]] bool rebuildMap();
        /** @brief 포커스 패널을 찾아 그 레이어만 켭니다. 바뀌었을 때만 레이어를 건드립니다. */
        void syncFocusedPanelLayer();
        /**
         * @brief 에디터 창이 키보드를 가진 동안 엔진 키보드 포커스를 `Tool` 로 두고, 놓으면 앞의 주인으로 돌립니다. 에디터 창이 키보드를 가졌으면 true 입니다.
         * @details 에디터 창의 키는 아래 `forwardEditorKeyboard` 가 입력 매니저에 넣는다. 그 키를 게임 · 셸 · UI 맵이 보지 않게 막는 것이 이 포커스다.
         *          Simulate 중에는 게임 뷰가 포커스여도 게임에 키를 주지 않는다(월드만 돈다).
         */
        bool syncKeyboardOwner();
        /**
         * @brief 에디터 창(주 창 · 떼어 낸 창, 게임 뷰 밖)이 키보드를 가진 동안 ImGui 가 받은 키 변화를 엔진 입력 매니저에 넣습니다. 놓으면 누른 채 남은 키를 놓습니다.
         * @details 그동안 `ImGuiEditor::processEvent` 는 OS 키 누름을 넘기지 않는다 — 키가 두 길로 들어가지 않고, 떼어 낸 창(보조 뷰포트, OS 메시지가 입력 매니저에 닿지 않는다)과
         *          자동화가 ImGui 에 넣은 키(`EditorKey`)도 같은 한 길로 에디터 입력 맵에 닿는다. 게임 뷰가 포커스면 OS 키가 그대로 간다(떼어 낸 게임 뷰는 `GameViewPanel` 이 넘긴다).
         */
        void forwardEditorKeyboard( bool bEditorOwnsKeyboard );
        /** @brief 넣은 눌린 키를 모두 놓습니다. 큐가 차서 못 넣은 키는 다음에 다시 시도합니다. */
        void releaseForwardedKeys( InputManager& input );
        /** @brief 글을 치는 중이거나 재배치 창이 키를 받는 중이면 true 입니다 — 명령 · 패널 액션이 모두 멈춘다. */
        bool areActionsBlocked() const;
        /** @brief 이번 프레임에 발화한 액션의 명령을 실행합니다. 포커스 패널 레이어의 명령이 있으면 그것만 실행합니다. */
        void executeTriggeredCommands();
        /** @brief 액션의 첫 키보드 바인딩 번호를 찾습니다. 없으면 false 입니다. */
        [[nodiscard]] bool findKeyboardBinding( const hashed_string& action, uint32& outBindIndex ) const;
        /** @brief 사용자 재배치 파일을 씁니다. 실패는 경고로 남깁니다. */
        void saveUserBindings() const;

    private:
        /** @brief 메뉴에서 고른, 다음 프레임 첫머리에 실행할 명령입니다. */
        struct QueuedCommand
        {
            string              _commandID;
            EditorCommandTarget _target;
        };

        InputMap              _map;
        vector<QueuedCommand> _listQueuedCommand;                                  ///< `queueCommand` 가 넣은 것(다음 `processFrame` 이 비운다)
        hashed_string         _focusedPanelLayer;                                  ///< 지금 켠 패널 레이어(없으면 빈 이름)
        bool                  _arrForwardedKey[static_cast<size_t>( Key::Count )]; ///< 에디터 창에서 넣은 눌린 키(키보드를 놓으면 놓는다)
        InputKeyboardFocus    _focusBeforeTool;                                    ///< `Tool` 로 바꾸기 전의 키보드 주인
        uint32                _syncedMapGeneration;                                ///< 마지막으로 더한 확장 맵 등록 세대
        bool                  _bToolOwnsKeyboard;                                  ///< 이 서브시스템이 키보드 포커스를 `Tool` 로 둔 중이면 true
        bool                  _bCommandsSuspended;                                 ///< 재배치 창이 키를 받는 중이면 true
    };
} // namespace sw::editor

/**
 * @brief 확장 모듈이 에디터 입력 맵에 자기 액션 파일을 더합니다. @p pID 는 등록 id, @p pResourcePath 는 `*.input.xml` 리소스 경로입니다.
 * @code SW_EDITOR_INPUT_MAP( ParkInput, "themepark.input", "editor/input/themepark.input.xml" ); @endcode
 */
#define SW_EDITOR_INPUT_MAP( name, pID, pResourcePath ) SW_EDITOR_REGISTER( ::sw::editor::EditorInputMapRegistration, InputMap_##name, { pID, 0 }, pResourcePath )
