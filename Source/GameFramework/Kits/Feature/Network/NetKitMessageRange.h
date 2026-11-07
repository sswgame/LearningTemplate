/**
 * @file NetKitMessageRange.h
 * @brief 네트워크 키트별 메시지 영역을 정의합니다. 키트의 메시지 · 와이어 버전은 키트마다의 메시지 표(`NetMessageTable` — `NetLockstepMessage::Table` 등)에 있다.
 * @details Core 가 프레임워크에 할당한 범위(`NetMessageRange::kFramework` ~ `kGame`)를 키트끼리 나눠 쓴다.
 *          Core 는 키트 이름을 모른다. 키트의 메시지 값은 "자기 영역 + n" 으로 적고, 영역을 넘지 않는지 static_assert 로 검사한다.
 *          한 게임이 키트 둘을 같이 써도 첫 바이트로 구분된다.
 *          게임은 쓰는 키트 표의 와이어 버전과 자기 값을 묶어 호스트에 넘긴다 — 예전의 손으로 올리던 키트 버전(`NetKitWireVersion`)은 표의 해시가 대신한다.
 * @code
 *     settings._wireVersion = NetWireVersion::combine( { NetClientServerMessage::Table::computeWireVersion(), NetDestructionMessage::Table::computeWireVersion(), kMyGameWireVersion } );
 * @endcode
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @struct NetKitMessageRange @brief 키트마다 16개씩 할당한 메시지 영역의 시작 값입니다. */
    struct NetKitMessageRange
    {
        static constexpr uint8 kClientServer    = NetMessageRange::kFramework + 0 * NetMessageRange::kSize; ///< GF_NetClientServer
        static constexpr uint8 kLockstep        = NetMessageRange::kFramework + 1 * NetMessageRange::kSize; ///< GF_NetLockstep
        static constexpr uint8 kTurnRelay       = NetMessageRange::kFramework + 2 * NetMessageRange::kSize; ///< GF_NetTurnRelay
        static constexpr uint8 kMMO             = NetMessageRange::kFramework + 3 * NetMessageRange::kSize; ///< GF_NetMMO
        static constexpr uint8 kDestruction     = NetMessageRange::kFramework + 4 * NetMessageRange::kSize; ///< GF_NetDestruction
        static constexpr uint8 kPairedAnimation = NetMessageRange::kFramework + 5 * NetMessageRange::kSize; ///< GF_NetPairedAnimation

        static_assert( kPairedAnimation + NetMessageRange::kSize <= NetMessageRange::kGame, "network kit message ranges must stay below the game range" );
        // 첫 바이트는 와이어 형식이다. 값이 바뀌면 다른 빌드와 통신할 수 없으므로, 바꾸려면 프로토콜 버전을 올려야 한다.
        static_assert( kClientServer == 0x10 && kLockstep == 0x20 && kTurnRelay == 0x30 && kMMO == 0x40 && kDestruction == 0x50 && kPairedAnimation == 0x60,
                       "network kit message ranges are wire format" );
    };
} // namespace sw
