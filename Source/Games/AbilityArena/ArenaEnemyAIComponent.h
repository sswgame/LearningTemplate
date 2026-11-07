/**
 * @file ArenaEnemyAIComponent.h
 * @brief 적 유닛의 AI 조종자 — 감각(대상 · 거리)만 냅니다. 판단은 행동 트리 데이터(`data/ai/grunt.bt.xml` · `caster.bt.xml`)이고, 폰(`ArenaUnitComponent`)과 따로 살며 의도만 냅니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class ArenaEnemyAIComponent
     * @brief 디렉터가 적 유닛을 세울 때 그 종류의 AI 프리팹(`gruntai` · `casterai` — 트리만 다르다)으로 함께 세워 빙의시킵니다. 대상은 디렉터의 유닛 모습에서 가장 가까운 적대 유닛입니다.
     * @details 감각은 조종 시스템이 틱 전에 게임 스레드에서 부릅니다(`updateBlackboard`) — 대상의 자리는 그 메시의 지금 월드 자리(이번 프레임 시작)를 읽습니다.
     *          블랙보드: `SelfPosition` · `TargetPosition`(위치) · `TargetDistance`(m). 대상이 없으면 셋 다 지웁니다.
     *          판단 상태(`ai.state`, 감시 값 `AI.State`)는 트리가 정한다: 근접은 `Idle` · `Chase` · `Strike`, 원거리는 `Idle` · `Chase` · `Retreat` · `Hold` 다.
     *          바라보기는 대상을 초점으로(`ai.focus`) — 적 유닛은 조종 요를 바라본다(`PawnFacingMode::ControlYaw`), 물러나도 대상을 본다.
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena Enemy AI", Tooltip = "Arena enemy AI controller: senses the nearest hostile; its behavior tree decides" )
    class ArenaEnemyAIComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        ArenaEnemyAIComponent();
        virtual ~ArenaEnemyAIComponent() override = default;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;
    };
} // namespace sw
