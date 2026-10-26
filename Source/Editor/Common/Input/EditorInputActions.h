/**
 * @file EditorInputActions.h
 * @brief 에디터 입력 맵(`editor.input.xml`)을 소유하고, 포커스 창의 레이어를 켜고, 발화한 액션의 명령을 실행합니다.
 * @details 에디터 단축키는 엔진 입력 맵(`InputMap`) 하나가 판정한다(EditorUX 10-4). 키 · 수정 키 · 트리거 · 사용자 재배치를 에디터가 따로 두지 않는다.
 *          이 서브시스템은 입력 맵에 없는 에디터 고유의 것만 맡는다: 어느 패널 창이 포커스인지(레이어 켜기), 액션 → 명령 연결.
 *          유니티 단축키 관리자의 컨텍스트, 언리얼 위젯별 `FUICommandList` 와 같은 선이다(기억).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw::editor
{
    /**
     * @class EditorInputActions
     * @brief 에디터 입력 맵의 주인입니다. `EditorContext` 가 명령 등록부 · 패널 매니저 뒤에 만들고 앞에 지웁니다.
     * @details 레이어 규칙: `Editor.Global` 은 늘 켜져 있고, `Editor.Panel.<패널 id>` 는 그 패널 창이 포커스일 때만 켜진다.
     *          같은 누름으로 포커스 패널 레이어의 액션과 전역 액션이 함께 발화하면 패널 쪽만 실행한다(포커스 창 → 바깥 → 전역).
     *          텍스트 입력 중(ImGui `WantTextInput`)이거나 재배치 창이 키를 받는 중이면 맵은 갱신하되 명령은 실행하지 않는다.
     */
    class EditorInputActions final : public EditorSubsystem
    {
    public:
        /** @brief 늘 켜진 전역 레이어 이름입니다. */
        static constexpr const utf8* kGlobalLayer = "Editor.Global";
        /** @brief 패널 레이어 이름의 앞부분입니다. 레이어 이름은 이 뒤에 패널 id(`EditorPanelEntry::_id`)를 붙인 것입니다. */
        static constexpr const utf8* kPanelLayerPrefix = "Editor.Panel.";

        EditorInputActions();
        ~EditorInputActions() override;
        EditorInputActions( const EditorInputActions& )            = delete;
        EditorInputActions& operator=( const EditorInputActions& ) = delete;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        static constexpr const utf8* kSubsystemName = "EditorInputActions";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        /** @brief 입력 맵을 만들어 엔진 입력 매니저에 붙이고 `editor.input.xml` 을 읽습니다. 못 읽으면 실패입니다(단축키 없는 에디터로 서지 않는다). */
        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;

        /** @brief 한 프레임: 키보드 주인을 맞추고, 떼어 낸 창의 키를 넘기고, 포커스 창의 레이어를 켜고 맵을 갱신합니다. ImGui 프레임 안에서, 엔진 입력 매니저의 `beginFrame` 뒤에 부릅니다. */
        void processFrame( float32 deltaSeconds );
        /** @brief 에디터 입력 맵입니다(값으로 소유 — 늘 있다. 초기화 전 · 종료 뒤에는 입력 매니저가 끊겨 갱신해도 아무것도 하지 않는다). */
        InputMap&       getInputMap() { return _map; }
        const InputMap& getInputMap() const { return _map; }
        /** @brief 지금 켜진 패널 레이어 이름입니다. 포커스 패널에 레이어가 없으면 빈 이름입니다. */
        const hashed_string& getFocusedPanelLayer() const { return _focusedPanelLayer; }

    private:
        /** @brief 포커스 패널을 찾아 그 레이어만 켭니다. 바뀌었을 때만 레이어를 건드립니다. */
        void syncFocusedPanelLayer();
        /**
         * @brief 에디터 창이 키보드를 가진 동안 엔진 키보드 포커스를 `Tool` 로 두고, 놓으면 앞의 주인으로 돌립니다.
         * @details 키는 늘 엔진 입력 매니저로 간다(`ImGuiEditor::processEvent`). 에디터 창(게임 뷰 밖)에 친 키를 게임 · 셸 · UI 맵이 보지 않게 막는 것이 이 포커스다.
         *          Simulate 중에는 게임 뷰가 포커스여도 게임에 키를 주지 않는다(월드만 돈다).
         */
        void syncKeyboardOwner();
        /**
         * @brief 떼어 낸 에디터 창(ImGui 보조 뷰포트)이 포커스면 그 창의 키를 엔진 입력 매니저에 넣습니다. 포커스를 잃으면 누른 채 남은 키를 놓습니다.
         * @details 보조 뷰포트의 키 메시지는 ImGui 백엔드만 받아 입력 매니저에 닿지 않는다. 떼어 낸 게임 뷰는 `GameViewPanel` 이 같은 일을 한다.
         */
        void forwardDetachedKeyboard();
        /** @brief 떼어 낸 창에서 넣은 눌린 키를 모두 놓습니다. 큐가 차서 못 넣은 키는 다음에 다시 시도합니다. */
        void releaseForwardedKeys( InputManager& input );

    private:
        InputMap           _map;
        hashed_string      _focusedPanelLayer;                                    ///< 지금 켠 패널 레이어(없으면 빈 이름)
        bool               _arrForwardedKey[static_cast<size_t>( Key::Count )]{}; ///< 떼어 낸 창에서 넣은 눌린 키(포커스를 잃으면 놓는다)
        InputKeyboardFocus _focusBeforeTool{ InputKeyboardFocus::Game };          ///< `Tool` 로 바꾸기 전의 키보드 주인
        bool               _bToolOwnsKeyboard{ false };                           ///< 이 서브시스템이 키보드 포커스를 `Tool` 로 둔 중이면 true
    };
} // namespace sw::editor
