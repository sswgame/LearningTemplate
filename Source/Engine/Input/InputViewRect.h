/**
 * @file InputViewRect.h
 * @brief 게임 화면이 창 클라이언트의 어느 사각형인지(창 픽셀)입니다. 입력이 포인터를 게임 화면 좌표로 옮길 때 씁니다.
 * @details 값은 엔진이 프레임마다 하나로 정한다(`Engine::update` → `InputManager::setGameViewRect`) — 주 출력(에디터 게임 뷰의 이미지)이 있으면 그 크기,
 *          없으면 백버퍼(게임 창 전체)다. 창 크기(`IWindow`)를 게임 화면 크기로 읽지 않는다 — 에디터 게임 뷰는 창의 일부다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 게임 화면의 창 안 사각형입니다(왼쪽 위 원점, 창 픽셀). */
    struct InputViewRect
    {
        int2 _origin{}; ///< 게임 화면 왼쪽 위의 창 픽셀
        int2 _size{};   ///< 게임 화면의 픽셀 크기. 0 이면 게임 화면이 없다(헤드리스 · 첫 프레임 전)

        /** @brief 크기가 없으면 true 입니다. */
        bool isEmpty() const { return _size._x <= 0 || _size._y <= 0; }
    };
} // namespace sw
