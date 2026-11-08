/**
 * @file ShortcutsViewModel.h
 * @brief 단축키 창의 ViewModel — 에디터 입력 맵(`EditorInputActions`)과 명령 표(`EditorCommandRegistry`)를 찾아 주고, 조합 받기가 부르는 바꾸기를 입력 맵으로 잇습니다. ImGui 를 모릅니다.
 * @details 패널(`ShortcutsPanel`)은 작업 공간 헤더(`EditorContext` · `EditorService`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약). 조합 받기 상태는 `ShortcutCaptureModel` 이고,
 *          이 ViewModel 이 그 `IShortcutBindings` 다 — 단축키는 사용자 설정이라 되돌리기에 남지 않는다(언리얼 Keyboard Shortcuts 도 트랜잭션 밖이다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Settings/Model/ShortcutCaptureModel.h"

namespace sw::editor
{
    class EditorCommandRegistry;
    class EditorInputActions;

    /** @brief 단축키 창의 ViewModel 입니다. 파일 머리말 참고. 상태가 없어 패널이 값으로 든다. */
    class ShortcutsViewModel final : public IShortcutBindings
    {
    public:
        /** @brief 지금 에디터 컨텍스트의 입력 맵입니다. 에디터가 서 있을 때(그리기)만 부른다 — 없으면 멈춘다. */
        static EditorInputActions& getInputActions();
        /** @brief 지금 에디터 컨텍스트의 명령 표입니다. 에디터가 서 있을 때(그리기)만 부른다 — 없으면 멈춘다. */
        static const EditorCommandRegistry& getCommandRegistry();
        /** @brief 기본과 다른 단축키를 가진 명령 수입니다(감시 값 `Editor.ShortcutOverrideCount`). */
        static uint32 countModifiedShortcuts();

        // IShortcutBindings
        [[nodiscard]] bool rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction ) override;
        [[nodiscard]] bool swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask ) override;
        void               resetShortcut( string_view action ) override;
        void               resetAllShortcuts() override;
        /** @brief 에디터가 내려가는 중(패널 종료)이면 입력 서브시스템이 이미 없을 수 있어 아무 일도 하지 않는다. */
        void setCommandsSuspended( bool bSuspended ) override;
    };
} // namespace sw::editor
