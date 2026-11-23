/**
 * @file InputUserDevices.h
 * @brief 로컬 멀티의 입력 사용자 한 명이 쓰는 장치 묶음과, 장치를 사용자에게 짝짓는 규칙입니다.
 * @details 유니티 Input System 의 `InputUser`(사용자마다 짝지은 장치 + 그 장치로 거른 액션)와 언리얼 `IPlatformInputDeviceMapper`(장치 → 플랫폼 사용자) 자리다.
 *          유니티처럼 짝은 사용자에게 남는다 — 패드가 끊겼다 돌아와도 같은 사용자다(연결 순서로 다시 짝지으면 두 사람의 캐릭터가 뒤바뀐다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 입력 사용자 한 명의 장치입니다. `InputMap::setUserDevices` 에 넘기면 그 맵은 이 장치만 읽는다. */
    struct SW_API InputUserDevices
    {
        optional<uint32> _padIndex;           ///< 짝지은 패드 번호. 없으면 아직 패드를 받지 못했다
        uint8            _bKeyboardMouse : 1; ///< 키보드 · 마우스 · 터치를 쓴다(사용자 0 만)

        InputUserDevices()
            : _padIndex{}
            , _bKeyboardMouse{ SW_FALSE }
        {
        }

        /**
         * @brief 짝이 없는 사용자에게 연결된 패드를 짝짓습니다 — 사용자 1 부터 먼저 받고, 남은 패드를 사용자 0(키보드 · 마우스)이 받는다.
         * @param inoutListUser 사용자 번호 순서. 이미 지은 짝은 패드가 끊겨도 그대로다.
         * @param listConnectedPad 지금 연결된 패드 번호(오름차순).
         */
        static void pairDevices( vector<InputUserDevices>& inoutListUser, const vector<uint32>& listConnectedPad );
    };
} // namespace sw
