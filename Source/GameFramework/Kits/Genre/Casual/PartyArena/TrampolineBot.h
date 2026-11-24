/**
 * @file TrampolineBot.h
 * @brief 트램펄린 아레나의 봇 판단입니다 — 무대 가장자리를 피해 가장 가까운 상대에게 붙어 돌진하고, 면에 닿는 순간 점프를 눌러 콤보를 잇습니다.
 * @details 난수 · 상태가 없다 — 같은 무대면 같은 입력이다(리플레이 · 락스텝). 사람과 같은 입력(`TrampolineInput`)만 낸다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/PartyArena/TrampolineArena.h"

namespace sw
{
    /** @brief 봇 하나의 판단입니다. */
    struct SW_KIT_API TrampolineBot
    {
        /** @brief @p player 의 이번 걸음 입력입니다. 무대에 없거나(떨어지는 중 · 부활 대기) 없는 번호면 빈 입력입니다. */
        static TrampolineInput computeInput( const TrampolineArena& arena, int32 player );
    };
} // namespace sw
