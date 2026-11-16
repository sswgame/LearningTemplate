/**
 * @file NetLockstepMessage.h
 * @brief 락스텝 키트(락스텝 · 롤백 세션)의 메시지 표입니다. 두 세션이 한 영역을 나눠 받으므로 표를 따로 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    /** @brief 락스텝 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`). */
    struct NetLockstepMessage
    {
        using Input         = NetMessageDef<0, NetMessageDirection::Both, NetChannelType::ReliableOrdered>;
        using Checksum      = NetMessageDef<1, NetMessageDirection::Both, NetChannelType::ReliableOrdered>;
        using RollbackInput = NetMessageDef<2, NetMessageDirection::Both, NetChannelType::Unreliable>;
        using Leave         = NetMessageDef<3, NetMessageDirection::ToClient, NetChannelType::ReliableOrdered>; ///< 서버 → 모두. 플레이어와, 그 플레이어가 빈 입력이 되는 틱을 알린다
        using Table         = NetMessageTable<NetKitMessageRange::kLockstep, Input, Checksum, RollbackInput, Leave>;

        static constexpr uint8 kInput         = Table::getKindOf<Input>();
        static constexpr uint8 kChecksum      = Table::getKindOf<Checksum>();
        static constexpr uint8 kRollbackInput = Table::getKindOf<RollbackInput>();
        static constexpr uint8 kLeave         = Table::getKindOf<Leave>();
        static constexpr int32 kMaxInputBytes = 1000; ///< 틱 하나의 입력 상한. 종류, 플레이어, 틱, 길이와 함께 메시지 하나(1024 B)에 들어간다. 넘는 입력은 예약하지 않는다
    };
} // namespace sw
