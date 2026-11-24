/**
 * @file BattleRoyaleTestTerrain.h
 * @brief 막는 것이 없는 지형입니다(자기장 · 판 규칙 테스트가 빌려 준다).
 */
#pragma once
#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleZone.h"

namespace sw
{
    /** @brief 어느 자리든 원 중심으로 허락합니다. */
    class AllowAllTerrain final : public IBrZoneTerrain
    {
    public:
        bool isZoneCenterAllowed( const float2& ) const override { return true; }
    };
} // namespace sw
