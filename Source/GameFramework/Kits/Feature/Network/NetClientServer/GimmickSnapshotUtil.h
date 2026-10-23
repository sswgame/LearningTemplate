/**
 * @file GimmickSnapshotUtil.h
 * @brief 기믹 회로 상태 묶음(`GimmickStateBundle`)을 네트워크 스냅샷(`NetSnapshot`)의 엔티티로 싣고 푸는 도우미입니다.
 * @details 스냅샷은 엔티티 하나의 상태가 `NetSnapshot::kMaxEntityBytes`(255) 바이트까지라서, 회로 하나의 상태가 그보다 크면 **조각**으로 나눠 연속한 엔티티 여럿에 싣는다.
 *          회로 `n`(키 순 번호)의 조각 `c` 는 엔티티 id `firstEntityID + n * kMaxChunksPerCircuit + c` 에 들어가고, 바이트는
 *          `u64 ownerKey  u8 chunkIndex  u8 chunkCount  payload[...]` 이다. 조각을 이은 바이트는 그 회로의 `GimmickCircuitState` 를 고정 배치형(`FixedLayout::write`,
 *          스키마 해시 64 비트 머리 포함)으로 쓴 것이다. 넷 채널이 핸드셰이크에서 해시를 섞기 전(docs/09_Decisions.md 5-9 넷 채널 스키마 해시)에는 이 머리가 다른 빌드의 바이트를 거절하는 유일한 장치다.
 *          varuint 인코딩이라 상태 값에 따라 길이와 조각 수가 바뀔 수 있다(Delay 의 절대 스텝 번호). 델타 복제는 바뀐 엔티티만 보내므로 상태가 변하지 않은 회로는 대역폭을 쓰지 않는다.
 *          받는 쪽은 `extractBundle` 로 조각을 모아 읽고(`SchemaMismatch` 를 포함한 실패는 그 회로만 건너뛴다) `GimmickStateBundle::apply` 로 씬에 적용한다. 조각이 하나라도 빠진 회로는 이번 스냅샷에서 건너뛴다(델타가 못 실은 조각은 다음에 온다).
 *          회로가 늘거나 줄면 번호가 밀려 뒤쪽 엔티티가 모두 바뀐다 — 회로 집합은 씬을 만들 때 정해지고 플레이 중에는 거의 변하지 않는다고 가정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickStateBundle.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"

namespace sw
{
    /**
     * @struct GimmickSnapshotUtil
     * @brief 묶음 ↔ 스냅샷 엔티티 변환입니다. 상태가 없습니다.
     */
    struct SW_KIT_API GimmickSnapshotUtil
    {
        static constexpr uint32 kMaxChunksPerCircuit = 16;
        static constexpr uint32 kChunkHeaderBytes    = 10; ///< ownerKey(8) + chunkIndex(1) + chunkCount(1)
        static constexpr uint32 kChunkPayloadBytes   = static_cast<uint32>( NetSnapshot::kMaxEntityBytes ) - kChunkHeaderBytes;
        static constexpr uint32 kMaxCircuitBytes     = kMaxChunksPerCircuit * kChunkPayloadBytes;
        static constexpr uint32 kMaxCircuitCount     = 4096; ///< 엔티티 id 범위 `[firstEntityID, firstEntityID + kMaxCircuitCount * kMaxChunksPerCircuit)`

        /**
         * @brief 묶음의 회로마다 조각 엔티티를 @p inoutSnapshot 에 더하고 엔티티를 id 순으로 다시 정렬합니다.
         * @param firstEntityID 이 묶음에 할당한 엔티티 id 범위의 시작입니다(게임의 다른 엔티티와 겹치지 않게 정한다).
         * @param typeID 엔티티의 종류 번호입니다(클라이언트가 이 엔티티를 회로 상태로 알아보는 표시).
         * @return 싣지 못한 회로 수입니다: 상태의 바이트가 `kMaxCircuitBytes` 를 넘거나, 선언한 범위 밖 값이 있어 쓰지 못했거나, 회로가 `kMaxCircuitCount` 를 넘는 것.
         */
        static uint32 appendEntities( NetSnapshot& inoutSnapshot, const GimmickStateBundle& bundle, uint32 firstEntityID, uint32 typeID );
        /**
         * @brief 스냅샷의 범위 안 조각 엔티티(종류 번호가 @p typeID 인 것)를 모아 묶음을 만듭니다. 조각이 빠졌거나 어긋났거나 읽지 못한(다른 빌드, 잘림, 범위 밖) 회로는 건너뜁니다.
         * @return 건너뛴 회로 수입니다.
         */
        static uint32 extractBundle( const NetSnapshot& snapshot, uint32 firstEntityID, uint32 typeID, GimmickStateBundle& outBundle );
    };
} // namespace sw
