/**
 * @file ArenaAutoBattleAIComponent.h
 * @brief 자동 전투 — 플레이어 유닛에 빙의하는 AI 조종자입니다. 자동 플레이 스위치(`-gv_arenaAutoPlay`)를 켜면 디렉터가 이것을 플레이어 폰에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class ArenaAutoBattleAIComponent
     * @brief 반쯤 깎이면 회복, 가까우면 근접, 멀면 다가가며 화염구, 둘러싸이면 반대쪽으로 대시 — 플레이어와 같은 폰 버튼만 누릅니다.
     * @details 몸 안의 자동 플레이 분기가 아니라 조종자 바꾸기입니다(언리얼에서 자동 플레이 AIController 가 플레이어 폰을 Possess 하는 것과 같다) — 움직임 ·
     *          어빌리티는 키보드로 할 때와 같은 `ArenaUnitComponent` 를 지납니다. 플레이어 유닛은 움직이는 쪽을 바라봅니다(대시는 물러나는 쪽으로).
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Auto Battle AI", Tooltip = "AI controller that plays the arena player while auto play is on" )
    class ArenaAutoBattleAIComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaAutoBattleAIComponent();
        virtual ~ArenaAutoBattleAIComponent() override = default;

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Crowd Radius", Tooltip = "Enemies inside this distance count as surrounding the player", Min = 0.0, Units = m )
        float32 _crowdRadius;
        PROPERTY( Category = "Auto Play", DisplayName = "Crowd Count", Tooltip = "Dash away when this many enemies surround the player", Min = 1 )
        uint32 _crowdCount;
        PROPERTY( Category = "Auto Play", DisplayName = "Melee Range", Tooltip = "Switch to melee inside this distance", Min = 0.0, Units = m )
        float32 _meleeRange;
        PROPERTY( Category = "Auto Play", DisplayName = "Heal Below", Tooltip = "Heal when health falls below this share of max health", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healBelow;
    };
} // namespace sw
