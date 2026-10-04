/**
 * @file NetKitMessageRange.h
 * @brief 네트워크 키트마다의 메시지 영역과 와이어 판입니다 — Core 가 프레임워크에 내준 몫(`NetMessageRange::kFramework` .. `kGame`)을 키트끼리 나눕니다.
 * @details Core 는 키트 이름을 모른다. 키트의 메시지 값은 "자기 영역 + n" 으로 적고, 영역을 넘지 않는지 static_assert 로 지킨다.
 *          한 게임이 키트 둘을 같이 써도 첫 바이트로 갈린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Network/NetTypes.h"

namespace sw
{
    /** @struct NetKitMessageRange @brief 키트마다 16 개씩의 메시지 영역 첫 값입니다. */
    struct NetKitMessageRange
    {
        static constexpr uint8 kClientServer = NetMessageRange::kFramework + 0 * NetMessageRange::kSize; ///< GF_NetClientServer
        static constexpr uint8 kLockstep     = NetMessageRange::kFramework + 1 * NetMessageRange::kSize; ///< GF_NetLockstep
        static constexpr uint8 kTurnRelay    = NetMessageRange::kFramework + 2 * NetMessageRange::kSize; ///< GF_NetTurnRelay
        static constexpr uint8 kMmo          = NetMessageRange::kFramework + 3 * NetMessageRange::kSize; ///< GF_NetMmo
        static constexpr uint8 kDestruction  = NetMessageRange::kFramework + 4 * NetMessageRange::kSize; ///< GF_NetDestruction

        static_assert( kDestruction + NetMessageRange::kSize <= NetMessageRange::kGame, "network kit message ranges must stay below the game range" );
        // 첫 바이트는 선(wire) 형식이다 — 값이 바뀌면 다른 빌드와 말이 안 통한다. 바꾸려면 프로토콜 판을 올린다.
        static_assert( kClientServer == 0x10 && kLockstep == 0x20 && kTurnRelay == 0x30 && kMmo == 0x40 && kDestruction == 0x50,
                       "network kit message ranges are wire format" );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetKitWireVersion
     * @brief 키트마다의 와이어 판입니다. 키트 메시지의 형식 · 의미를 바꾸는 커밋은 그 키트의 판을 올린다.
     * @details 게임은 쓰는 키트의 판과 자기 판을 묶어 호스트에 준다 — 판이 다른 빌드끼리는 연결이 `VersionMismatch` 로 거절된다.
     * @code
     *     settings._wireVersion = NetWireVersion::combine( { NetKitWireVersion::kClientServer, NetKitWireVersion::kDestruction, kMyGameWireVersion } );
     * @endcode
     */
    struct NetKitWireVersion
    {
        static constexpr uint32 kClientServer = 1;
        static constexpr uint32 kLockstep     = 3;
        static constexpr uint32 kTurnRelay    = 1;
        static constexpr uint32 kMmo          = 1;
        static constexpr uint32 kDestruction  = 1;
    };
} // namespace sw
