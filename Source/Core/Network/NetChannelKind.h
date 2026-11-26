/**
 * @file NetChannelKind.h
 * @brief 메시지를 싣는 채널 종류를 정의합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 채널 종류입니다. 메시지마다 고르며, 값은 와이어 형식이므로 바꾸면 `NetWireVersion::kCore` 를 올려야 합니다. */
    enum class NetChannelKind : uint8
    {
        ReliableOrdered = 0, ///< 반드시 도착하고, 보낸 순서대로 처리한다(채팅, 거래, 턴 행동, 생성과 파괴 스냅샷). 64KB까지 보낼 수 있다(조각으로 나눈다).
        ReliableUnordered,   ///< 반드시 도착하고, 받는 대로 처리한다. 앞 신뢰 메시지를 잃어도 기다리지 않는다(번호로 스스로 순서를 맞추는 파괴 사건 등). 1KB까지이며 조각나지 않는다.
        UnreliableSequenced, ///< 잃어도 되지만 옛 메시지는 버린다(위치 스냅샷, 입력). 메시지 첫 바이트(종류)마다 따로 처리하므로 다른 종류끼리는 서로 지우지 않는다.
        Unreliable,          ///< 잃어도 되고 순서도 상관없다(소리, 이펙트).
        Count
    };
} // namespace sw
