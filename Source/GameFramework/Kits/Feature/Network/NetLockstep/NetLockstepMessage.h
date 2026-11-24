/**
 * @file NetLockstepMessage.h
 * @brief 락스텝 키트(락스텝 · 롤백 세션)의 메시지 표입니다. 두 세션이 한 영역을 나눠 받으므로 표를 따로 둔다.
 * @details 입력 · 체크섬 · 이탈 본문은 고정 배치형(`NetMessageBodyDef`)이다. 롤백 입력은 비트 예산에 맞춰 잘라 싣는 입력 버퍼(`RollbackInputBuffer`)라 손으로 쓰는 본문이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Net/NetMessageBody.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    /** @brief 락스텝 선 형식의 상한입니다. 본문 형식(`PROPERTY( MaxCount )`)과 메시지 표가 함께 쓴다. */
    struct LockstepLimit
    {
        static constexpr int32 kMaxInputBytes = 1000; ///< 틱 하나의 입력 상한. 종류, 플레이어, 틱, 길이와 함께 메시지 하나(1024 B)에 들어간다. 넘는 입력은 예약하지 않는다
    };

    /** @brief 틱 하나의 입력(양쪽 — 서버가 받아 다른 클라이언트에 그대로 나눈다) 본문입니다. 필드 순서가 와이어 형식이다. */
    REFLECT()
    struct LockstepInputBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _matchID{ 0 }; ///< 판 번호(`LockstepConfig::_matchID`) — 다르면 지난 판의 늦은 메시지다
        PROPERTY( Min = 0 )
        int32 _player{ 0 };
        PROPERTY()
        uint32 _tick{ 0 };
        PROPERTY( MaxCount = LockstepLimit::kMaxInputBytes, Min = 0, Max = 255 )
        vector<uint8> _input{};
    };

    /** @brief 틱 하나의 상태 체크섬 본문입니다. */
    REFLECT()
    struct LockstepChecksumBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _matchID{ 0 }; ///< 판 번호(`LockstepConfig::_matchID`) — 다르면 지난 판의 늦은 메시지다
        PROPERTY( Min = 0 )
        int32 _player{ 0 };
        PROPERTY()
        uint32 _tick{ 0 };
        PROPERTY()
        uint32 _checksum{ 0 };
    };

    /** @brief 이탈 알림(서버 → 모두) 본문입니다. 그 플레이어가 빈 입력이 되는 틱이다. */
    REFLECT()
    struct LockstepLeaveBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _matchID{ 0 }; ///< 판 번호(`LockstepConfig::_matchID`) — 다르면 지난 판의 늦은 메시지다
        PROPERTY( Min = 0 )
        int32 _player{ 0 };
        PROPERTY()
        uint32 _tick{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 락스텝 키트의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`). */
    struct NetLockstepMessage
    {
        using Input         = NetMessageBodyDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered, LockstepInputBody>;
        using Checksum      = NetMessageBodyDef<1, NetMessageDirection::Both, NetChannelKind::ReliableOrdered, LockstepChecksumBody>;
        using RollbackInput = NetMessageDef<2, NetMessageDirection::Both, NetChannelKind::Unreliable, 2>;                              ///< 판 2: 판 번호(`RollbackConfig::_matchID`)를 앞에 싣는다
        using Leave         = NetMessageBodyDef<3, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, LockstepLeaveBody>; ///< 서버 → 모두
        using Table         = NetMessageTable<NetKitMessageRange::kLockstep, Input, Checksum, RollbackInput, Leave>;

        static constexpr uint8 kInput         = Table::getKindOf<Input>();
        static constexpr uint8 kChecksum      = Table::getKindOf<Checksum>();
        static constexpr uint8 kRollbackInput = Table::getKindOf<RollbackInput>();
        static constexpr uint8 kLeave         = Table::getKindOf<Leave>();
        static constexpr int32 kMaxInputBytes = LockstepLimit::kMaxInputBytes; ///< 값은 `LockstepLimit` 한 곳(본문 형식의 MaxCount 와 같은 값)
    };
} // namespace sw
