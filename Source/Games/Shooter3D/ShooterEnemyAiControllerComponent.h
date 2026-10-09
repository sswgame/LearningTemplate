/**
 * @file ShooterEnemyAiControllerComponent.h
 * @brief 스켈레톤 적의 판단 — 플레이어를 쫓고(내비메시 경로 · 군중 회피), 닿으면 휘두르기 버튼을 누릅니다. 몸(폰)은 플레이어와 같은 몸 이동으로 걷는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/AiControllerComponent.h"

namespace sw
{
    /**
     * @class ShooterEnemyAiControllerComponent
     * @brief 스켈레톤 폰(`prefabs/skeleton.prefab.xml` — `_autoPossess Ai`)이 플레이를 시작하면 조종 시스템이 `prefabs/skeleton_ai.prefab.xml` 로 세워 빙의시킵니다.
     * @details 쫓는 동안(`ShooterEnemyPhase::Chasing`) 플레이어 발로 `moveTo` 합니다 — 같은 오브젝트의 내비메시 에이전트(`SteerOnly`)가 경로 · 이웃 비키기로 속도를 내고,
     *          그것이 이동 의도가 되어 몸 이동(`ShooterBodyMovementComponent`)이 움직입니다. 플레이어가 `_retargetDistance` 넘게 움직이면 목적지를 다시 겁니다.
     *          손이 닿는 거리(적의 `_reach`) 안이고 플레이어가 살아 있으면 `Attack` 버튼을 누르고 있습니다 — 휘두르기 간격 · 맞는 시각은 폰의 규칙입니다.
     *          쫓지 않는 단계(일어남 · 휘두름 · 움찔 · 쓰러짐)에는 멈추고, 휘두르는 동안과 가까이 선 동안은 플레이어를 바라봅니다.
     *          폰 상태 · 플레이어 자리는 디렉터가 앞 프레임에 적은 것을 읽습니다(조종 단계 — 틱 전, 게임 스레드).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Enemy AI Controller", Tooltip = "Skeleton AI: chases the player over the navmesh and holds Attack inside reach" )
    class ShooterEnemyAiControllerComponent : public AiControllerComponent
    {
    public:
        REFLECT_BODY();

        ShooterEnemyAiControllerComponent();
        ~ShooterEnemyAiControllerComponent() override = default;

    protected:
        void think( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "AI", DisplayName = "Retarget Distance", Tooltip = "The path is asked again when the player moved this far", Min = 0.0, Units = m )
        float32 _retargetDistance;
    };
} // namespace sw
