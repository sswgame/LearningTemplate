/**
 * @file EditorInputActions.h
 * @brief 에디터 입력 어댑터 — ImGui 키를 엔진 입력에 넘기고, 포커스 패널을 단축키 Model(`EditorShortcutMap`)에 알리고, Model 이 판정한 명령을 요청합니다.
 * @details 에디터 단축키는 엔진 입력 맵(`InputMap`) 하나가 판정한다(`docs/agents/EditorUI.md` 입력 절). 키 · 수정 키 · 트리거 · 사용자 재배치 파일 형식을 에디터가 따로 두지 않는다.
 *          맵 소유 · 갱신 · 발화 판정 · 재배치는 Model(`EditorShortcutMap`, ImGui 없음)이고, 이 어댑터는 ImGui 와 맞닿는 일(포커스 패널 찾기 · 키보드 주인 · 키 넘기기 · 글 입력 중 막기)만 한다.
 *          유니티 단축키 관리자의 컨텍스트, 언리얼 위젯별 `FUICommandList` 와 같은 선이다(기억).
 */
#pragma once
#include "Core/Container/EnumArray.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorExtensionRegistry.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Input/Model/EditorShortcutMap.h"

#include "Engine/Input/KeyCodeUtil.h"

namespace sw::editor
{
    /**
     * @class EditorInputActions
     * @brief 에디터 입력 어댑터입니다 — ImGui 와 엔진 입력 사이의 일만 맡고, 맵 · 판정은 Model(`EditorShortcutMap`)이 한다. `EditorContext` 가 명령 등록부 · 패널 매니저 뒤에 만들고 앞에 지웁니다.
     * @details 이 어댑터의 몫: 어느 패널 창이 포커스인지 찾아 Model 에 알리기, 키보드 주인(도구 포커스), 에디터 창의 ImGui 키를 엔진 입력에 넘기기, 클릭 · 끌기 문턱을 ImGui 에 넣기,
     *          텍스트 입력 중(ImGui `WantTextInput`)이거나 재배치 창이 키를 받는 중이면 명령 · 패널 액션 막기, 메뉴에서 고른 명령 줄.
     *          유니티 단축키 관리자의 컨텍스트, 언리얼 위젯별 `FUICommandList` 와 같은 선이다.
     */
    class SW_EDITOR_API EditorInputActions final : public EditorSubsystem
    {
    public:
        EditorInputActions();
        ~EditorInputActions() override;
        EditorInputActions( const EditorInputActions& )            = delete;
        EditorInputActions& operator=( const EditorInputActions& ) = delete;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        static constexpr const utf8* kSubsystemName = "EditorInputActions";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        void                         resetToFreshEditor() override
        {
            _listQueuedCommand.clear();
            _bCommandsSuspended = SW_FALSE;
        }

        /**
         * @brief 지금 에디터 컨텍스트의 입력 어댑터입니다. 컨텍스트가 없으면(에디터 밖 테스트 · 종료 뒤) nullptr 입니다.
         * @details 패널 View 가 단축키 표를 찾는 길이다 — View 는 작업 공간 헤더(`EditorContext`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약, `EditorPanelManager::findInEditorContext` 와 같은 모양).
         */
        [[nodiscard]] static EditorInputActions* findInEditorContext();
        /** @brief 지금 에디터 컨텍스트의 입력 어댑터입니다. 에디터가 서 있을 때만 부른다 — 없으면 멈춘다. */
        [[nodiscard]] static EditorInputActions& getInEditorContext();
        span<const utf8* const>                  getSubsystemDependencies() const override;
        /** @brief Model 맵을 엔진 입력 매니저에 붙이고 `editor.input.xml` · 확장 맵 · 사용자 재배치를 읽습니다. 못 읽으면 실패입니다(단축키 없는 에디터로 서지 않는다). */
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;

        /**
         * @brief 한 프레임: 메뉴가 미룬 명령을 요청하고, 키보드 주인을 맞추고, 에디터 창의 키를 넘기고, 포커스 패널을 Model 에 알리고, Model 이 맵을 갱신 · 판정한 명령을 요청합니다.
         *        ImGui 프레임 안에서, 엔진 입력 매니저의 `beginFrame` 뒤에 부릅니다.
         */
        void processFrame( float32 deltaSeconds );
        /** @brief 단축키 Model(맵 · 판정 · 재배치)입니다. */
        EditorShortcutMap&       getShortcuts() { return _shortcuts; }
        const EditorShortcutMap& getShortcuts() const { return _shortcuts; }
        /** @brief 단축키 재배치 창이 키를 받는 동안 명령 실행을 멈춥니다. 맵은 계속 갱신합니다. */
        void setCommandsSuspended( bool bSuspended ) { _bCommandsSuspended = bSuspended; }
        /**
         * @brief 패널 · 뷰포트가 읽는 액션(명령이 아닌 것 — 화살표 행 이동, 비행 키, 이름 바꾸기 시작)이 이번 프레임에 발화했으면 true 입니다.
         * @details 글을 치는 중이거나 재배치 창이 키를 받는 중이면 false 다(명령 실행과 같은 조건). 범위는 액션의 레이어가 정한다 — 그 패널이 포커스일 때만 켜진다.
         */
        bool wasActionTriggered( string_view action ) const;
        /** @brief 액션이 눌려 있으면 true 입니다(비행처럼 누르는 동안 하는 일). 조건은 `wasActionTriggered` 와 같습니다. */
        bool isActionDown( string_view action ) const;
        /**
         * @brief 메뉴에서 고른 명령을 다음 `processFrame` 첫머리에 실행하도록 넣습니다(패널을 그리기 전).
         * @details 맥락 메뉴는 패널이 목록을 그리는 중에 닫힌다. 그 자리에서 실행하면 지운 컴포넌트 · 오브젝트를 그리던 루프가 계속 읽는다 —
         *          실행을 프레임 경계로 미뤄 그리는 중인 목록이 명령 뒤에 남지 않게 한다. 글을 치는 중이어도 실행한다(고른 것이다).
         */
        void queueCommand( string_view commandID, const EditorCommandTarget& target );
        /**
         * @brief 게임 스레드 쪽 — UI 가 넘긴 키보드 주인(`EditorUIFrameOutput::_bEditorOwnsKeyboard`)을 엔진 키보드 포커스에 입힙니다. 에디터가 가지면 `Tool` 로 두고, 놓으면 앞의 주인으로 돌립니다.
         * @details 셸이 UI 명령을 적용하는 자리(`ImGuiEditor::applyUICommands`)에서 부른다. UI 는 입력 매니저의 포커스를 바로 바꾸지 않는다. 그동안 다른 주인(콘솔)이 포커스를 바꿨으면 돌리지 않는다.
         */
        void applyKeyboardOwner( bool bEditorOwns );

    private:
        /** @brief 포커스 패널을 찾아 Model 에 알립니다(그 패널의 레이어만 켜진다). */
        void syncFocusedPanelLayer();
        /**
         * @brief UI 쪽 — 에디터 창이 키보드를 가졌는지 정해 이번 UI 프레임 값 묶음에 적고 돌려줍니다. 엔진 포커스는 게임 스레드가 `applyKeyboardOwner` 로 입힌다.
         * @details 에디터 창의 키는 아래 `forwardEditorKeyboard` 가 입력 매니저에 넣는다. 그 키를 게임 · 셸 · UI 맵이 보지 않게 막는 것이 그 포커스다.
         *          Simulate 중에는 게임 뷰가 포커스여도 게임에 키를 주지 않는다(월드만 돈다).
         */
        bool decideKeyboardOwner();
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

    private:
        /** @brief 메뉴에서 고른, 다음 프레임 첫머리에 실행할 명령입니다. */
        struct QueuedCommand
        {
            string              _commandID;
            EditorCommandTarget _target;
        };

        EditorShortcutMap     _shortcuts;              ///< 맵 · 판정 · 재배치(Model)
        vector<QueuedCommand> _listQueuedCommand;      ///< `queueCommand` 가 넣은 것(다음 `processFrame` 이 비운다)
        EnumArray<Key, bool>  _arrForwardedKey;        ///< 에디터 창에서 넣은 눌린 키(키보드를 놓으면 놓는다)
        Subscription          _toolFocus;              ///< 에디터 창이 키보드를 가진 동안 쥔 `Tool` 포커스(게임 스레드 — `applyKeyboardOwner`)
        uint8                 _bCommandsSuspended : 1; ///< 재배치 창이 키를 받는 중이면 true
    };
} // namespace sw::editor
