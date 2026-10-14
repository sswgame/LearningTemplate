/**
 * @file MonsterTrainerAI.h
 * @brief 트레이너 AI입니다. 가장 큰 기대 피해를 주는 기술을 고르고, 상성이 불리하면 유리한 개체로 교체합니다. 쓰러진 자리는 가장 센 개체로 채웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterBattle.h"

namespace sw
{
    /**
     * @struct MonsterTrainerAI
     * @brief 상태가 없는 규칙 모음입니다. 같은 전투 상태에서는 항상 같은 행동을 합니다(난수를 쓰지 않습니다).
     * @details "불리"는 내 기술의 최고 상성이 1배 미만이고 상대 타입이 나에게 2배 이상인 경우다(AI 는 상대 기술을 모르고 타입만 본다).
     *          교체 후보는 상대 타입이 2배 미만으로 들어오고 내 최고 상성이 1배 이상인 개체 중 기대 피해가 가장 큰 것이다. 후보가 없으면 버틴다.
     */
    struct SW_KIT_API MonsterTrainerAI
    {
        /** @brief 이번 라운드의 행동입니다. */
        static MonsterAction chooseAction( const MonsterBattle& battle, int32 side );
        /** @brief 기대 피해가 가장 큰 기술 슬롯을 반환합니다(PP 가 남은 슬롯만 보고, 같으면 앞 슬롯). 없으면 −1 을 반환합니다. */
        static int32 chooseBestMoveSlot( const MonsterBattle& battle, int32 side, int32 partyIndex, float32& outExpectedDamage );
        /** @brief 파티 @p partyIndex 의 개체가 지금 상대 앞에서 불리한지 반환합니다. */
        static bool isDisadvantaged( const MonsterBattle& battle, int32 side, int32 partyIndex );
        /** @brief 쓰러진 자리를 채울 개체(기대 피해가 가장 큰 개체)를 반환합니다. 없으면 −1 을 반환합니다. */
        static int32 chooseReplacement( const MonsterBattle& battle, int32 side );

    private:
        static float32 computeBestOwnMultiplier( const MonsterBattle& battle, const MonsterInstance& monster, int32 foeSide );
        static float32 computeThreatMultiplier( const MonsterBattle& battle, const MonsterInstance& monster, int32 foeSide );
    };
} // namespace sw
