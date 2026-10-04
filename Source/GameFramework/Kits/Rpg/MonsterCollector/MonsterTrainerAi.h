/**
 * @file MonsterTrainerAi.h
 * @brief 트레이너 AI — 가장 큰 기대 피해 기술을 고르고, 상성이 불리하면 유리한 개체로 교체합니다. 쓰러진 자리는 가장 센 개체로 채웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterBattle.h"

namespace sw
{
    /**
     * @struct MonsterTrainerAi
     * @brief 상태가 없는 규칙 모음입니다. 같은 전투 상태면 늘 같은 행동입니다(난수를 쓰지 않는다).
     * @details "불리" = 내 기술의 최고 상성이 1 배 미만이고, 상대 타입이 나에게 2 배 이상인 경우입니다(AI 는 상대 기술을 모르고 타입만 봅니다).
     *          교체 후보는 상대 타입이 2 배 미만으로 들어가고 내 최고 상성이 1 배 이상인 개체 중 기대 피해가 가장 큰 것입니다. 없으면 버팁니다.
     */
    struct SW_GF_API MonsterTrainerAi
    {
        /** @brief 이번 라운드의 행동입니다. */
        static MonsterAction chooseAction( const MonsterBattle& battle, int32 side );
        /** @brief 기대 피해가 가장 큰 기술 칸입니다(PP 가 남은 칸만, 같으면 앞 칸). 없으면 −1 입니다. */
        static int32 chooseBestMoveSlot( const MonsterBattle& battle, int32 side, int32 partyIndex, float32& outExpectedDamage );
        /** @brief 파티 @p partyIndex 의 개체가 지금 상대 앞에서 불리한가입니다. */
        static bool isDisadvantaged( const MonsterBattle& battle, int32 side, int32 partyIndex );
        /** @brief 쓰러진 자리를 채울 개체입니다(기대 피해가 가장 큰 개체). 없으면 −1 입니다. */
        static int32 chooseReplacement( const MonsterBattle& battle, int32 side );

    private:
        static float32 computeBestOwnMultiplier( const MonsterBattle& battle, const MonsterInstance& monster, int32 foeSide );
        static float32 computeThreatMultiplier( const MonsterBattle& battle, const MonsterInstance& monster, int32 foeSide );
    };
} // namespace sw
