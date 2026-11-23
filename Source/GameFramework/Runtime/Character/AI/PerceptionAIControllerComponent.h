/**
 * @file PerceptionAIControllerComponent.h
 * @brief 시각 · 청각 감각을 씬의 자극 색인에서 받아 블랙보드에 적는 AI 조종자입니다(경비 · 순찰).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Radians.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class PerceptionAIControllerComponent
     * @brief 판단 차례마다 폰의 눈에서 다른 팀의 자극 원천(`AIStimulusSourceComponent` — 보이는 몸 · 낸 소리)을 감지해 블랙보드에 적습니다. 판단은 행동 트리 데이터입니다.
     * @details 블랙보드: `SeenPosition` · `SeenDistance`(지금 보이는 가장 가까운 대상), `AlertPosition`(보이지 않지만 기억하는 대상 중 가장 새로운 것 — 들린 소리, 놓친 대상), `SelfPosition`.
     *          없는 것은 지운다(트리가 `hasBlackboard` 로 묻는다). 언리얼 `AAIController` + `UAIPerceptionComponent` + BT 서비스의 자리다.
     *          가림은 보지 않는다(격자 없는 씬) — 벽을 보는 게임은 자기 조종자가 격자를 넘긴다.
     */
    REFLECT( Category = "AI", DisplayName = "Perception AI Controller", Tooltip = "Senses sight and hearing from the scene's stimulus index into the blackboard; its behavior tree decides" )
    class SW_GF_API PerceptionAIControllerComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        PerceptionAIControllerComponent();
        ~PerceptionAIControllerComponent() override = default;

        /** @brief 바탕의 되돌림에 더해 기억한 대상을 비웁니다. */
        [[nodiscard]] SpawnStateReset resetToSpawnState() override;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Perception", DisplayName = "Team", Min = 0, Max = 3, Tooltip = "Own team; stimuli of other teams are sensed" )
        uint8 _team;
        PROPERTY( Category = "Perception", DisplayName = "Eye Height", Min = 0.0, Units = m )
        float32 _eyeHeight;
        PROPERTY( Category = "Perception", DisplayName = "Sight Range", Min = 0.0, Units = m )
        float32 _sightRange;
        PROPERTY( Category = "Perception", DisplayName = "Lose Sight Range", Min = 0.0, Units = m, Tooltip = "A target already seen stays seen up to here" )
        float32 _loseSightRange;
        PROPERTY( Category = "Perception", DisplayName = "Sight Half Angle", Min = 0.0, Max = 180.0 )
        Radians _sightHalfAngle;
        PROPERTY( Category = "Perception", DisplayName = "Peripheral Range", Min = 0.0, Units = m, Tooltip = "Sensed even behind the back inside this distance" )
        float32 _peripheralRange;
        PROPERTY( Category = "Perception", DisplayName = "Hearing Scale", Min = 0.0, Tooltip = "Multiplier on noise radii" )
        float32 _hearingScale;
        PROPERTY( Category = "Perception", DisplayName = "Memory Duration", Min = 0.0, Units = s, Tooltip = "How long a target no longer sensed is remembered" )
        float32 _memoryDuration;

        AIPerception _perception;
    };
} // namespace sw
