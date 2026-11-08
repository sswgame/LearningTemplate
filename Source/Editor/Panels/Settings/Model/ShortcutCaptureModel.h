/**
 * @file ShortcutCaptureModel.h
 * @brief 단축키 창이 조합을 받는 상태입니다 — 받기 시작 · 받은 조합 적용 · 겹침을 물을 조합 · 바꾸기 · 취소 · 되돌리기(ImGui 없음).
 * @details 눌린 조합을 읽는 일(`ImGuiKeyForward::findPressedChord`)과 그리기는 패널(`ShortcutsPanel`)이 한다. 겹침 규칙 · 저장 형식은 입력 맵의 것이라 여기 없고,
 *          바꾸는 일은 `IShortcutBindings` 로 받아 에디터 서비스 없이 시험한다(언리얼 Keyboard Shortcuts 의 `FUICommandInfo` 재배치와 Slate 행이 나뉜 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Input/KeyCodeUtil.h"

namespace sw::editor
{
    /** @class IShortcutBindings @brief 조합 받기가 부르는 입력 맵 쪽입니다. 에디터는 `EditorInputActions` 로 잇고, 테스트는 가짜를 넘긴다. */
    class IShortcutBindings
    {
    public:
        virtual ~IShortcutBindings() = default;

        /** @brief @p action 의 조합을 바꿉니다. 겹치면 바꾸지 않고 겹친 액션을 @p outConflictAction 에 담고 false, 키보드 바인딩이 없으면 비운 채 false 입니다. */
        [[nodiscard]] virtual bool rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction ) = 0;
        /** @brief @p action 에 받은 조합을, @p otherAction 에 @p action 의 옛 조합을 줍니다. 못 바꾸면 false 입니다. */
        [[nodiscard]] virtual bool swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask ) = 0;
        virtual void               resetShortcut( string_view action )                                                      = 0;
        virtual void               resetAllShortcuts()                                                                      = 0;
        /** @brief 조합을 받는 동안 에디터 명령 실행을 멈춥니다. */
        virtual void setCommandsSuspended( bool bSuspended ) = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ShortcutCaptureModel
     * @brief 파일 머리말을 참고하세요. 받기 → (겹치면) 묻기 → 끝의 세 상태이고, 받기 · 묻기 동안만 명령 실행이 멈춘다.
     * @details 명령 실행 멈춤은 이 상태의 일부다. 끝나는 길마다 풀지 않으면 에디터 단축키가 모두 죽은 채 남는다 — 그래서 끝내는 길은 `cancel` 과 그 내부(`finish`) 하나다.
     */
    class ShortcutCaptureModel
    {
    public:
        ShortcutCaptureModel();

        /** @brief @p action 의 조합을 받기 시작합니다. 받거나 묻던 다른 액션은 버립니다. */
        void beginCapture( IShortcutBindings& bindings, const string& action );
        /** @brief 조합을 기다리는 중이면 true 입니다(묻는 동안은 false — 패널이 키를 읽지 않아도 된다). */
        [[nodiscard]] bool isWaitingForChord() const { return _captureAction.empty() == false && _conflictAction.empty(); }
        /**
         * @brief 받은 조합을 적용합니다. 수정 키 없는 Esc 는 취소입니다(유니티 · 언리얼과 같다). 겹치면 조합을 들고 묻는 상태로 갑니다. 기다리는 중이 아니면 아무 일도 하지 않습니다.
         * @details 키보드 바인딩이 없는 액션은 바꿀 것이 없으므로 그대로 끝낸다.
         */
        void submitChord( IShortcutBindings& bindings, Key key, uint8 modifierMask );
        /**
         * @brief 묻던 겹침을 바꾸기로 답합니다. 받은 조합은 이 액션이, 이 액션의 옛 조합은 겹친 액션이 갖습니다(언리얼 · 유니티의 바꾸기와 같다).
         * @details 못 바꾸면 경고를 남기고 계속 묻는다 — 곧바로 끝내면 사용자에게는 바꾸기가 된 것처럼 보인다.
         */
        void confirmReplace( IShortcutBindings& bindings );
        /** @brief 받기나 묻기를 그만두고 명령 실행을 되돌립니다. 받는 중이 아니면 아무 일도 하지 않습니다(접힌 창이 프레임마다 부른다). */
        void cancel( IShortcutBindings& bindings );

        /** @brief @p action 을 기본으로 되돌립니다. 받거나 묻던 일은 먼저 그만둔다 — 묻고 있던 겹침은 되돌린 맵에서 더는 맞지 않는다. */
        void resetShortcut( IShortcutBindings& bindings, const string& action );
        /** @brief 모든 조합을 기본으로 되돌립니다. 받거나 묻던 일은 먼저 그만둔다. */
        void resetAllShortcuts( IShortcutBindings& bindings );

        /** @brief @p action 의 조합을 받거나 묻는 중이면 true 입니다. */
        [[nodiscard]] bool isCapturing( const string& action ) const { return action == _captureAction; }
        /** @brief 겹침을 묻는 중이면 true 입니다. */
        [[nodiscard]] bool hasConflict() const { return _conflictAction.empty() == false; }
        /** @brief @p action 이 지금 묻는 겹침의 한쪽이면(그 줄을 빨갛게 한다) true 입니다. */
        [[nodiscard]] bool isConflictRow( const string& action ) const { return hasConflict() && ( action == _conflictAction || action == _captureAction ); }

    private:
        void finish( IShortcutBindings& bindings );

    private:
        string _captureAction;   ///< 조합을 받는 중인 액션(비면 받지 않는다)
        string _conflictAction;  ///< 받은 조합과 겹친 액션(비면 충돌 없음)
        Key    _pendingKey;      ///< 충돌을 물을 동안 받아 둔 키
        uint8  _pendingModifier; ///< 충돌을 물을 동안 받아 둔 수정 키(`ModifierKey`)
    };
} // namespace sw::editor
