/**
 * @file ShooterAutoAimControllerComponent.h
 * @brief Shooter3D 의 자동 플레이 — 플레이어 폰에 빙의하는 AI 조종자의 감각입니다. 판단(돌기 · 겨누기 · 쏘기 · 무기 고르기)은 행동 트리 데이터(`data/ai/autoaim.bt.xml`)입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"

namespace sw
{
    /**
     * @class ShooterAutoAimControllerComponent
     * @brief 자동 플레이가 켜지면 디렉터가 `prefabs/autoplay_ai.prefab.xml` 로 세워 플레이어 폰에 빙의시킵니다(`gv_shooterAutoPlay` · 에디터 툴바 스위치 하나 — 끄면 플레이어 조종자가 다시 쥔다).
     * @details 몸(`ShooterPlayerComponent`)에는 자동 플레이 분기가 없습니다 — 트리가 사람과 같은 의도(이동 · 조종 회전 · `Fire` · `Melee` · `Weapon1` · `Weapon2`)를 냅니다.
     *          감각은 블랙보드에 사실과 문턱을 적습니다: `CirclePoint`(아레나 가운데를 도는 나선 — 접선 + 안쪽 — 의 앞 점, 판이 아니거나 플레이어가 쓰러졌으면 지운다) · `HasEnemy` ·
     *          `AimPoint`(보이는 적 가운데 가장 가까운 가슴, 보이는 적이 없으면 가장 가까운 가슴) · `TargetVisible` · `TargetDistance` · `AimError`(조준 요 오차의 크기) ·
     *          `WeaponIndex` · `RifleAmmo` · `ShotgunAmmo` 와 칸의 `AimTolerance` · `EngageDistance` · `CloseRangeDistance` · `MeleeReach` · `Melee`.
     *          조종 회전은 `_turnRate` 로 돈다 — 표적이 바뀌어도 시점이 튀지 않게. 적 목록 · 상자는 디렉터가 앞 프레임에 적은 것을 읽습니다(조종 단계 — 틱 전, 게임 스레드).
     *          언리얼 `AAIController` + BT 서비스(감각) + 트리(판단)와 같은 나눔이다 — 스켈레톤 적(`ShooterEnemyAIControllerComponent`)과 같은 모양.
     */
    /**
     * @struct ShooterAutoPlayOptions
     * @brief 자동 플레이 조종자 모두에 걸리는 게임 범위 값입니다. 게임이 멤버로 들고 게임 범위에 겁니다(`Shooter3DGame`). 게임 스레드에서 씁니다.
     * @details 조종자마다 밀어 넣지 않고 조종자가 읽는다 — 한 프로세스 재생(넷 시뮬)의 서버 · 클라이언트 월드는 활성 씬과 다른 매니저라, 활성 씬의 조종자만 켜면
     *          그 월드의 자동 플레이는 근접을 누르지 않았다(shooter3d.netauthority). 게임 범위는 월드와 상관없이 하나다.
     */
    struct ShooterAutoPlayOptions
    {
        /**
         * @brief 데이터 칸 `Melee` 와 상관없이 닿는 거리의 적을 근접으로도 칠 스위치입니다. 개발 부분이 생길 때 걸고(`Dev/ShooterDevPart.cpp` — `-gv_shooterAutoMelee`) 질 때 푼다.
         * @details 값이 아니라 스위치를 가리킨다 — 개발 부분의 갱신은 게임 플레이 중에만 돌아(에디터의 넷 시뮬은 아니다) 매 프레임 옮기면 그 월드에는 닿지 않았다.
         */
        const int32* _pForceMelee{ nullptr };

        bool isMeleeForced() const { return _pForceMelee != nullptr && *_pForceMelee != 0; }
    };

    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Auto Aim Controller", Tooltip = "AI controller that aims, shoots, picks weapons and circles the arena with the player pawn when auto play is on" )
    class ShooterAutoAimControllerComponent : public AIControllerComponent
    {
    public:
        REFLECT_BODY();

        ShooterAutoAimControllerComponent();
        ~ShooterAutoAimControllerComponent() override = default;

    protected:
        void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn ) override;

    private:
        PROPERTY( Category = "Auto Play", DisplayName = "Engage Distance", Tooltip = "Shoots at enemies inside this distance", Min = 0.0, Units = m )
        float32 _engageDistance;
        PROPERTY( Category = "Auto Play", DisplayName = "Close Range Distance", Tooltip = "Inside this distance the shotgun, outside it the rifle", Min = 0.0, Units = m )
        float32 _closeRangeDistance;
        PROPERTY( Category = "Auto Play", DisplayName = "Aim Tolerance", Tooltip = "Fires when the aim is off by less than this (blackboard AimTolerance)", Min = 0.0, Units = rad )
        float32 _aimTolerance;
        PROPERTY( Category = "Auto Play", DisplayName = "Chest Height", Tooltip = "Fraction of the enemy's height aimed at", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _chestHeight;
        PROPERTY( Category = "Auto Play", DisplayName = "Circle Tangent", Tooltip = "Sideways part of the circling move, multiplied by the position", Min = 0.0 )
        float32 _circleTangent;
        PROPERTY( Category = "Auto Play", DisplayName = "Circle Inward", Tooltip = "Part of the circling move that pulls toward the centre, multiplied by the position", Min = 0.0 )
        float32 _circleInward;
        PROPERTY( Category = "Auto Play", DisplayName = "Melee", Tooltip = "Also presses Melee when a visible enemy is inside the player's melee reach" )
        uint8 _bMelee : 1;
    };
} // namespace sw
