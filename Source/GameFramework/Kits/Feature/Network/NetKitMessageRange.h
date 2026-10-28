/**
 * @file NetKitMessageRange.h
 * @brief 네트워크 키트별 메시지 영역과 와이어 버전을 정의합니다.
 * @details Core 가 프레임워크에 할당한 범위(`NetMessageRange::kFramework` ~ `kGame`)를 키트끼리 나눠 쓴다.
 *          Core 는 키트 이름을 모른다. 키트의 메시지 값은 "자기 영역 + n" 으로 적고, 영역을 넘지 않는지 static_assert 로 검사한다.
 *          한 게임이 키트 둘을 같이 써도 첫 바이트로 구분된다.
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

namespace sw
{
    /**
     * @struct NetKitWireVersion
     * @brief 키트별 와이어 버전입니다. 키트 메시지의 형식이나 의미를 바꾸면 그 키트의 버전을 올립니다.
     * @details 게임은 쓰는 키트의 버전과 자기 버전을 묶어 호스트에 넘긴다. 버전이 다른 빌드끼리의 연결은 `VersionMismatch` 로 거절된다.
     * @code
     *     settings._wireVersion = NetWireVersion::combine( { NetKitWireVersion::kClientServer, NetKitWireVersion::kDestruction, kMyGameWireVersion } );
     * @endcode
     */
    struct NetKitWireVersion
    {
        static constexpr uint32 kClientServer    = 6; ///< 버전 6부터 의도(`ControlIntent::write`) 끝에 발동 순간 시선 필드(선택)가 붙는다(5: 입력 메시지의 보던 틱이 메시지마다가 아니라 입력 항목마다의 스탬프, 4: 소유자 상태 필드)
        static constexpr uint32 kLockstep        = 3;
        static constexpr uint32 kTurnRelay       = 3; ///< 버전 3부터 행동 상한이 8 KB이다
        static constexpr uint32 kMMO             = 4; ///< 버전 4부터 갱신 확인 메시지(옛 `kUpdateAck`)가 없고 연결 계층의 전달 통지로 판정한다
        static constexpr uint32 kDestruction     = 2; ///< 버전 2부터 스냅샷을 조각이 아니라 메시지 하나로 보낸다
        static constexpr uint32 kPairedAnimation = 1;
    };
} // namespace sw
