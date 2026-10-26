/**
 * @file ImGuiKeyForward.h
 * @brief ImGui 가 받은 키보드 입력을 엔진 키 · 글자 입력 사건으로 바꿉니다.
 * @details 게임 뷰 패널을 다른 OS 창(ImGui 보조 뷰포트)으로 떼면 그 창의 키 메시지는 ImGui 백엔드만 받고 게임 입력(`InputManager`)에는 닿지 않습니다.
 *          그 키를 ImGui 가 이번 프레임에 알게 된 변화에서 읽어 게임 입력에 넣을 사건으로 만듭니다. 키 ↔ ImGui 키 짝은 여기 한 곳에 둡니다(입력 맵 패널의 키 바인딩 잡기도 이것을 씁니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/RawInputEvent.h"

namespace sw::editor
{
    /**
     * @struct ImGuiKeyForward
     * @brief 이번 ImGui 프레임의 키보드 변화를 엔진 입력 사건으로 읽습니다. ImGui 컨텍스트가 있고 프레임 안(`NewFrame` 뒤)이어야 합니다.
     */
    struct ImGuiKeyForward
    {
        /** @brief 엔진 키가 지금 ImGui 에서 눌려 있으면 true 입니다. 짝이 없는 키(`Key::Unknown`)는 false 입니다. */
        [[nodiscard]] static bool isKeyDown( Key key );
        /** @brief 이번 프레임에 새로 눌린 키(자동 반복 제외) 하나를 돌려줍니다. 없으면 `Key::Unknown` 입니다. */
        [[nodiscard]] static Key findPressedKey();
        /**
         * @brief 이번 프레임에 눌리거나(자동 반복 포함) 떼어진 키를 @p outListEvent 에 덧붙입니다. 사건마다 지금 누른 수정 키(Ctrl · Shift · Alt · Super)가 붙습니다.
         * @details 누름 사건의 반복 표시(`_bRepeat`)는 자동 반복이면 켭니다.
         */
        static void appendKeyChanges( vector<RawInputEvent>& outListEvent );
        /** @brief 이번 프레임에 ImGui 가 받은 글자 입력(`io.InputQueueCharacters`)을 글자마다 `TextInput` 사건으로 @p outListEvent 에 덧붙입니다. */
        static void appendTextInput( vector<RawInputEvent>& outListEvent );
        /**
         * @brief 이번 프레임에 새로 눌린 수정 키가 아닌 키 하나와 지금 누른 수정 키 마스크(`ModifierKey`)를 돌려줍니다(단축키 재배치가 받는 조합). 없으면 false 입니다.
         * @details 수정 키만 누른 프레임은 조합이 아니다 — Ctrl 을 먼저 누르고 U 를 누르면 U 의 프레임에 `Ctrl+U` 가 된다.
         */
        [[nodiscard]] static bool findPressedChord( Key& outKey, uint8& outModifierMask );
    };
} // namespace sw::editor
