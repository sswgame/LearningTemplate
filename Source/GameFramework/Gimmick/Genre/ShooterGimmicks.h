/**
 * @file ShooterGimmicks.h
 * @brief 슈터 기믹 — 폭발 드럼통(사슬 폭발), 포탑(고르기 · 조준 · 사격), 부서지는 엄폐물(단계 · 파괴 훅)입니다. 점프대는 `LaunchPadComponent`, 키카드 문은 상호작용 + 회로입니다.
 * @details 피해는 모두 같은 오브젝트의 `GimmickSensorComponent` 로 받습니다(`GimmickDamageUtil::applyDamage` — 총알 · 위험 지대 · 다른 폭발이 같은 길).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/FixedStepTimer.h"

namespace sw
{
    /**
     * @class ExplosiveBarrelComponent
     * @brief 피해가 `_health` 를 넘으면 터집니다 — `_radius` 안의 오브젝트에 거리 감쇠 피해(가까울수록 큼, 다른 드럼통도 터진다), "Explosion" 연출, 몸을 끕니다.
     * @details 터짐은 걸음(60 Hz) 끝에 한 번 — 사슬 폭발은 다음 걸음에 이어집니다(한 프레임에 무한히 번지지 않는다).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Explosive Barrel", Tooltip = "Explodes after enough damage, damaging everything in a radius (chains)" )
    class SW_GF_API ExplosiveBarrelComponent : public Component
    {
    public:
        REFLECT_BODY();

        ExplosiveBarrelComponent();
        virtual ~ExplosiveBarrelComponent() override = default;

        void    onTick( float32 deltaTime ) override;
        void    stepOnce();
        bool    hasExploded() const { return _bExploded; }
        float32 getDamageTaken() const { return _damageTaken; }

    private:
        void explode( GameObject& owner );

    private:
        PROPERTY( Category = "Barrel", DisplayName = "Health", Min = 0.0 )
        float32 _health;
        PROPERTY( Category = "Barrel", DisplayName = "Radius", Min = 0.0, Meta = "Units=m" )
        float32 _radius;
        PROPERTY( Category = "Barrel", DisplayName = "Damage", Min = 0.0, Tooltip = "Damage at the centre; falls off linearly to the radius" )
        float32 _damage;
        PROPERTY( Category = "Barrel", DisplayName = "Damage Taken", Tooltip = "Runtime" )
        float32 _damageTaken;
        PROPERTY( Category = "Barrel", DisplayName = "Exploded", Tooltip = "Runtime" )
        bool _bExploded;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class TurretComponent
     * @brief 사거리 · 시야각 · 시야 안에서 태그(`_targetTags`)를 가진 가장 가까운 것을 고르고(`InteractionSelector` 와 같은 고르기), 요를 `_turnSpeed` 로 돌려
     *        `_aimTolerance` 안에 들면 `_fireInterval` 마다 쏩니다(히트스캔 — 시야 광선이 대상에 닿으면 피해).
     * @details 회로의 Enable 노드로 켜고 끕니다(오브젝트 켜기). 요는 소유자 로컬 회전의 Y 입니다(3D). 걸음은 60 Hz.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Turret", Tooltip = "Picks the nearest visible tagged target, turns, fires hitscan shots" )
    class SW_GF_API TurretComponent : public Component
    {
    public:
        REFLECT_BODY();

        TurretComponent();
        virtual ~TurretComponent() override = default;

        void onTick( float32 deltaTime ) override;
        void stepOnce();

        GameObjectHandle getTarget() const { return _target; }
        int32            getShotCount() const { return _shotCount; }

    private:
        PROPERTY( Category = "Turret", DisplayName = "Target Tags" )
        TagContainer _targetTags;
        PROPERTY( Category = "Turret", DisplayName = "Range", Min = 0.0, Meta = "Units=m" )
        float32 _range;
        PROPERTY( Category = "Turret", DisplayName = "View Angle", Min = 0.0, Tooltip = "Half angle it can see, 0 sees all around", Meta = "Units=rad" )
        float32 _viewAngle;
        PROPERTY( Category = "Turret", DisplayName = "Turn Speed", Min = 0.0, Meta = "Units=rad/s" )
        float32 _turnSpeed;
        PROPERTY( Category = "Turret", DisplayName = "Aim Tolerance", Min = 0.0, Meta = "Units=rad" )
        float32 _aimTolerance;
        PROPERTY( Category = "Turret", DisplayName = "Fire Interval", Min = 0.01, Meta = "Units=s" )
        float32 _fireInterval;
        PROPERTY( Category = "Turret", DisplayName = "Damage", Min = 0.0 )
        float32 _damage;
        PROPERTY( Category = "Turret", DisplayName = "Target", Tooltip = "Runtime" )
        GameObjectHandle _target;
        PROPERTY( Category = "Turret", DisplayName = "Cooldown Steps", Tooltip = "Runtime" )
        int32 _cooldownSteps;
        PROPERTY( Category = "Turret", DisplayName = "Shot Count", Tooltip = "Runtime" )
        int32 _shotCount;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructibleComponent
     * @brief 부서지는 엄폐물 — 받은 피해가 `_listStageThreshold` 를 넘을 때마다 단계가 오르고("Stage" 연출 + 회로 신호), 마지막을 넘으면 몸을 끕니다.
     * @details 파편 · 부서짐 물리는 이 훅(`GimmickCueEvent` "Destroyed" · `getStage`)에 붙습니다 — 물리 백엔드가 깨진 메시를 강체로 흩는다. 같은 오브젝트의 센서
     *          신호에 단계(0..)를 넣어 회로가 읽게 합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Destructible", Tooltip = "Damage stages ending in destruction (fracture hook)" )
    class SW_GF_API DestructibleComponent : public Component
    {
    public:
        REFLECT_BODY();

        DestructibleComponent();
        virtual ~DestructibleComponent() override = default;

        void onTick( float32 deltaTime ) override;
        void stepOnce();

        int32 getStage() const { return _stage; }
        bool  isDestroyed() const { return _stage >= static_cast<int32>( _listStageThreshold.size() ) && _listStageThreshold.empty() == false; }

    private:
        PROPERTY( Category = "Destructible", DisplayName = "Stage Thresholds", Tooltip = "Total damage at which each stage begins; the last destroys" )
        vector<float32> _listStageThreshold;
        PROPERTY( Category = "Destructible", DisplayName = "Damage Taken", Tooltip = "Runtime" )
        float32 _damageTaken;
        PROPERTY( Category = "Destructible", DisplayName = "Stage", Tooltip = "Runtime" )
        int32 _stage;

        FixedStepTimer _clock;
    };
} // namespace sw
