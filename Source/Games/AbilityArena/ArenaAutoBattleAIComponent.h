/**
 * @file ArenaAutoBattleAIComponent.h
 * @brief 자동 전투 — 플레이어 유닛에 빙의하는 AI 조종자입니다. 자동 플레이 스위치(`-gv_arenaAutoPlay`)를 켜면 디렉터가 이것을 플레이어 폰에 빙의시키고, 끄면 플레이어 조종자가 되찾습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class ArenaAutoBattleAIComponent
     * @brief 감각(체력 비율 · 대상 · 둘러싼 적 수 · 대시 중)만 냅니다. 판단(회복 · 근접 · 화염구 · 대시)은 행동 트리 데이터(`data/ai/autobattle.bt.xml`)이고, 플레이어와 같은 폰 버튼만 누릅니다.
     * @details 몸 안의 자동 플레이 분기가 아니라 조종자 바꾸기입니다(언리얼에서 자동 플레이 AIController 가 플레이어 폰을 Possess 하는 것과 같다) — 움직임 ·
     *          어빌리티는 키보드로 할 때와 같은 `ArenaUnitComponent` 를 지납니다. 플레이어 유닛은 움직이는 쪽을 바라봅니다(대시는 물러나는 쪽으로).
     *          블랙보드: `SelfPosition` · `TargetPosition`(위치), `TargetDistance`(m), `HealthRatio`(체력 어트리뷰트가 있을 때만), `NearbyEnemyCount`(`_crowdRadius` 안), `Dashing`.
     *          전투 문맥(어빌리티 · 디렉터 · 자기 자리)이 없으면 `SelfPosition` 을 지우고, 대상이 없으면 `TargetPosition` · `TargetDistance` 를 지웁니다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Auto Battle AI", Tooltip = "AI controller that plays the arena player while auto play is on; its behavior tree decides" )
    class ArenaAutoBattleAIComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaAutoBattleAIComponent();
        virtual ~ArenaAutoBattleAIComponent() override = default;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Crowd Radius", Tooltip = "Enemies inside this distance count into NearbyEnemyCount", Min = 0.0, Units = m )
        float32 _crowdRadius;
    };
} // namespace sw
