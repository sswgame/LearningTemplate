/**
 * @file InputKeyboardFocus.h
 * @brief 키보드를 지금 누가 받는지(`InputManager::setKeyboardFocus`)입니다. 입력 매니저를 쥐지 않는 쪽이 이 열거만 쓰도록 떼어 둔 잎 헤더입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief 키보드(키 조회와 글자 입력)를 지금 누가 받는지 나타냅니다.
     * @details `Game` 이 아니면 게임 쪽 조회(`InputManager::isKeyDown`, 통합 InputMap 의 키보드 바인딩)는 "안 눌림" 이고, 글자는 그 주인의
     *          콜백에만 간다. 장치 상태는 계속 갱신한다.
     */
    enum class InputKeyboardFocus : uint8
    {
        Game = 0,   ///< 게임 코드와 통합 InputMap 이 받습니다.
        DevConsole, ///< 게임 창의 개발 콘솔이 열려 있는 동안 받습니다.
        UI,         ///< 런타임 UI 의 글자 입력 필드가 포커스를 가진 동안 받습니다(`UISystem` 이 잡고 놓습니다).
        /**
         * @brief 게임 화면 밖의 도구 창(에디터 패널 · 떼어 낸 OS 창)이 키보드를 가진 동안 받습니다. 도구는 자기 맵을 `InputMap::setKeyboardFocusIgnored` 로 읽습니다.
         * @details 도구 창의 키도 이 매니저의 장치에 넣어 입력 맵 하나로 판정하되(에디터 단축키), 그동안 게임 쪽 조회는 그 키를 보지 않는다 — Ctrl+S 가 게임의 S 로 새지 않게.
         */
        Tool,
        Count
    };
} // namespace sw
