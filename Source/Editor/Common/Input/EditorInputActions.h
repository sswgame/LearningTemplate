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

        /** @brief 한 프레임: 포커스 창의 레이어를 켜고 맵을 갱신합니다. 엔진 입력 매니저의 `beginFrame` 뒤에 부릅니다. */
        void processFrame( float32 deltaSeconds );
        /** @brief 에디터 입력 맵입니다(값으로 소유 — 늘 있다. 초기화 전 · 종료 뒤에는 입력 매니저가 끊겨 갱신해도 아무것도 하지 않는다). */
        InputMap&       getInputMap() { return _map; }
        const InputMap& getInputMap() const { return _map; }
        /** @brief 지금 켜진 패널 레이어 이름입니다. 포커스 패널에 레이어가 없으면 빈 이름입니다. */
        const hashed_string& getFocusedPanelLayer() const { return _focusedPanelLayer; }

    private:
        /** @brief 포커스 패널을 찾아 그 레이어만 켭니다. 바뀌었을 때만 레이어를 건드립니다. */
        void syncFocusedPanelLayer();

    private:
        InputMap      _map;
        hashed_string _focusedPanelLayer; ///< 지금 켠 패널 레이어(없으면 빈 이름)
    };
} // namespace sw::editor
