/**
 * @file ShooterGimmicks.h
 * @brief 슈터 기믹입니다. 폭발 드럼통(연쇄 폭발), 포탑(대상 고르기, 조준, 사격), 부서지는 엄폐물(단계, 파괴 훅)을 다룹니다. 점프대는 `LaunchPadComponent` 로, 키카드 문은 상호작용과 회로로 만듭니다.
 * @details 피해는 모두 같은 오브젝트의 `GimmickSensorComponent` 로 받는다. 총알, 위험 지대, 다른 폭발이 모두 같은 경로(`GimmickDamageUtil::applyDamage`)를 쓴다.
 *          오브젝트에 파괴 컴포넌트(`FractureComponentBase`, `.fracture` 가 있는 메시)가 있으면 몸을 끄는 대신 조각으로 부서진다. 폭발은 반경에 닿은
 *          파괴 오브젝트에 위치 기반 폭발 피해(`applyRadialDamageAtWorld`)를 주어 벽과 상자를 맞은 위치에서 깨뜨린다. 그래서 연쇄 폭발이 근처의 파괴물도 부순다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionView.h"

namespace sw
{
    /**
     * @class ExplosiveBarrelComponent
     * @brief 피해가 `_health` 를 넘으면 터집니다. `_radius` 안의 오브젝트에 거리 감쇠 피해를 주고(가까울수록 큼, 다른 드럼통도 터짐), "Explosion" 연출을 내고, 몸을 끕니다.
     * @details 터짐은 스텝(60 Hz) 끝에 한 번 일어난다. 연쇄 폭발은 다음 스텝으로 이어지므로 한 프레임에 무한히 번지지 않는다. `_fuseTime` 이 있으면 플레이 시작부터 그만큼
     *          지난 뒤 스스로 터진다(시한 폭탄, 쇼케이스). 반경에 닿은 파괴 오브젝트(자기 포함)에는
     *          중심 변형 `_fractureStrain` 과 충격량 `_blastImpulse` 의 폭발 피해를 주고, 자기에게 파괴 컴포넌트가 있으면 몸을 끄지 않고 조각으로 날아간다.
     *          반경 안 다른 오브젝트에 쓰는 일(`applyRadialDamage`)은 틱 뒤 게임 스레드에서 한다 — 병렬 틱에서 남의 오브젝트(파괴 조각)에 쓰지 않는다. 같은 프레임의 물리 앞이다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Explosive Barrel", Tooltip = "Explodes after enough damage, damaging everything in a radius (chains)" )
    class SW_GF_API ExplosiveBarrelComponent : public Component
    {
    public:
        REFLECT_BODY();

        ExplosiveBarrelComponent();
        virtual ~ExplosiveBarrelComponent() override = default;

        void    onTick( TickContext& context ) override;
        void    stepOnce();
        bool    hasExploded() const { return _bExploded; }
        float32 getDamageTaken() const { return _damageTaken; }

    private:
        void explode( GameObject& owner );
        /** @brief 터진 자리(`_blastCenter`)에서 반경 안 다른 오브젝트에 거리 감쇠 피해 · 파괴 오브젝트에 폭발 피해를 줍니다(게임 스레드, 틱 밖). */
        void applyRadialDamage( GameObjectManager& manager ) const;

    private:
        PROPERTY( Category = "Barrel", DisplayName = "Health", Min = 0.0 )
        float32 _health;
        PROPERTY( Category = "Barrel", DisplayName = "Radius", Min = 0.0, Units = m )
        float32 _radius;
        PROPERTY( Category = "Barrel", DisplayName = "Damage", Min = 0.0, Tooltip = "Damage at the centre; falls off linearly to the radius" )
        float32 _damage;
        PROPERTY( Category = "Barrel", DisplayName = "Fuse Time", Min = 0.0, Tooltip = "Explodes on its own this long after play starts (0 = only from damage)", Units = s )
        float32 _fuseTime;
        PROPERTY( Category = "Barrel", DisplayName = "Fracture Strain", Min = 0.0, Tooltip = "Strain at the centre for destructible (fractured) objects in the radius" )
        float32 _fractureStrain;
        PROPERTY( Category = "Barrel", DisplayName = "Blast Impulse", Min = 0.0, Tooltip = "Impulse given to pieces broken off by the blast", Units = "N*s" )
        float32 _blastImpulse;
        PROPERTY( RuntimeState, Category = "Barrel", DisplayName = "Damage Taken", Tooltip = "Runtime" )
        float32 _damageTaken;
        PROPERTY( RuntimeState, Category = "Barrel", DisplayName = "Exploded", Tooltip = "Runtime" )
        bool _bExploded;

        int32          _fuseStepsLeft; ///< 도화선이 다 타기까지 남은 스텝 수(-1 이면 아직 세지 않음)
        FixedStepTimer _clock;
        float3         _blastCenter; ///< 터진 자리 — 틱 뒤 `applyRadialDamage` 가 읽는다(틱 뒤 일은 캡처 없이 핸들로 다시 찾는다)
    };
} // namespace sw

namespace sw
{
    class IWorldQuery;

    /** @brief 포탑 탄이 맞힐 대상 하나입니다(이번 스텝의 자리와 오브젝트). */
    struct TurretShotTarget
    {
        float3 _position{};
        uint64 _objectID{ 0 };
    };

    /**
     * @class TurretShotWorld
     * @brief 포탑 탄이 묻는 세계입니다. 대상은 구(`_other` 는 목록 번호)이고, 대상이 아닌 막는 바디(벽 · 문)는 표면입니다.
     * @details 발사 순간의 시야만 보지 않는다 — 걸음마다 선분을 막는 질의(`IWorldQuery`)에 묻으므로 쏜 뒤 생긴 벽에 탄이 맞아 끝난다.
     *          대상의 몸(물리 바디)은 막지 않는다. 쏜 포탑 자신은 건너뛴다. 언리얼 ProjectileMovement 가 매 이동을 스윕하는 것과 같은 자리다(기억에 의존한 비교).
     */
    class SW_GF_API TurretShotWorld final : public IProjectileWorld
    {
    public:
        TurretShotWorld( const vector<TurretShotTarget>& listTarget, float32 hitRadius, const IWorldQuery& blockers, uint64 ownerID );

        bool findTargetPoint( const ProjectileState& projectile, float3& outPoint ) const override;
        bool sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const override;

    private:
        bool isTarget( uint64 objectID ) const;

    private:
        const vector<TurretShotTarget>& _listTarget;
        const IWorldQuery&              _blockers;
        uint64                          _ownerID;
        float32                         _hitRadius;
    };

    /**
     * @class TurretComponent
     * @brief 사거리, 시야각, 시야 안에서 태그(`_targetTags`)를 가진 가장 가까운 것을 고르고(`InteractionView` 와 같은 방식), 요를 `_turnSpeed` 로 돌립니다.
     *        `_aimTolerance` 안에 들어오면 `_fireInterval` 마다 쏩니다. `_projectileSpeed` 가 0 이면 히트스캔이고(시야 광선이 대상에 닿으면 피해), 0 보다 크면 날아가는 탄입니다.
     * @details 회로의 Enable 노드로 켜고 끈다(오브젝트 켜기). 요는 소유자 로컬 회전의 Y 이다(3D). 스텝은 60 Hz 이다.
     *          날아가는 탄은 대상의 속도(스텝 사이 자리 차이)로 앞을 겨눈다(`computeAim` → `Ballistics`). 언리얼 `SuggestProjectileVelocity` 의 자리다(기억에 의존한 비교).
     *          따라잡을 수 없거나 닿지 않는 대상에는 쏘지 않는다. 날아가는 중인 탄은 값 목록(`ProjectileSimulation`)이고 세이브에 싣지 않는다 — 불러온 뒤 다음 발부터 쏜다.
     *          날아가는 탄은 걸음마다 막는 것을 본다(`TurretShotWorld`) — 쏜 뒤 끼어든 벽에 맞아 끝난다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Turret", Tooltip = "Picks the nearest visible tagged target, turns, fires hitscan shots" )
    class SW_GF_API TurretComponent : public Component
    {
    public:
        REFLECT_BODY();

        TurretComponent();
        virtual ~TurretComponent() override = default;

        void onTick( TickContext& context ) override;
        void stepOnce();

        /**
         * @brief 대상을 맞힐 겨눌 곳과 발사 방향을 구합니다. @p projectileSpeed 가 0 이면(히트스캔) 대상 그 자리, 아니면 앞을 겨눈 자리이고, @p gravityScale 이 0 보다 크면 낮은 포물선입니다.
         * @return 맞힐 수 있으면 true 입니다. 따라잡지 못하거나 포물선이 닿지 않으면 false 입니다.
         */
        [[nodiscard]] static bool computeAim( const float3& origin, const float3& targetPosition, const float3& targetVelocity, float32 projectileSpeed, float32 gravityScale,
                                              float3& outAimPoint, float3& outDirection );

        GameObjectHandle getTarget() const { return _target; }
        int32            getShotCount() const { return _shotCount; }
        /** @brief 대상에 닿은 수입니다. 히트스캔은 쏜 수와 같고, 날아가는 탄은 닿은 것만 셉니다. */
        int32 getHitCount() const { return _hitCount; }

    private:
        /** @brief 날아가는 탄을 한 걸음 옮기고 닿은 대상에 피해를 줍니다. `_listTagged` 를 이번 스텝의 대상 목록으로 씁니다. */
        void stepProjectiles( GameObject& owner );
        /** @brief 지금 겨눈 쪽으로 한 발 쏩니다. */
        void fireAt( GameObject& owner, GameObject& victim, const float3& origin, const float3& direction );

    private:
        PROPERTY( Category = "Turret", DisplayName = "Target Tags" )
        TagContainer _targetTags;
        PROPERTY( Category = "Turret", DisplayName = "Range", Min = 0.0, Units = m )
        float32 _range;
        PROPERTY( Category = "Turret", DisplayName = "View Angle", Min = 0.0, Tooltip = "Half angle it can see, 0 sees all around", Units = rad )
        float32 _viewAngle;
        PROPERTY( Category = "Turret", DisplayName = "Turn Speed", Min = 0.0, Units = "rad/s" )
        float32 _turnSpeed;
        PROPERTY( Category = "Turret", DisplayName = "Aim Tolerance", Min = 0.0, Units = rad )
        float32 _aimTolerance;
        PROPERTY( Category = "Turret", DisplayName = "Fire Interval", Min = 0.01, Units = s )
        float32 _fireInterval;
        PROPERTY( Category = "Turret", DisplayName = "Damage", Min = 0.0 )
        float32 _damage;
        PROPERTY( Category = "Turret", DisplayName = "Projectile Speed", Min = 0.0, Tooltip = "0 fires hitscan shots; above 0 the shot flies and the turret leads moving targets", Units = "m/s" )
        float32 _projectileSpeed;
        PROPERTY( Category = "Turret", DisplayName = "Projectile Gravity Scale", Min = 0.0, Tooltip = "Scene gravity multiplier on the flying shot; above 0 the turret aims along an arc" )
        float32 _projectileGravityScale;
        PROPERTY( Category = "Turret", DisplayName = "Hit Radius", Min = 0.01, Tooltip = "A flying shot hits a target within this distance of its origin", Units = m )
        float32 _hitRadius;
        PROPERTY( RuntimeState, Category = "Turret", DisplayName = "Target", Tooltip = "Runtime" )
        GameObjectHandle _target;
        PROPERTY( RuntimeState, Category = "Turret", DisplayName = "Cooldown Steps", Tooltip = "Runtime" )
        int32 _cooldownSteps;
        PROPERTY( RuntimeState, Category = "Turret", DisplayName = "Shot Count", Tooltip = "Runtime" )
        int32 _shotCount;
        PROPERTY( RuntimeState, Category = "Turret", DisplayName = "Hit Count", Tooltip = "Runtime" )
        int32 _hitCount;

        FixedStepTimer               _clock;
        vector<InteractionCandidate> _listCandidate;          ///< 스텝마다 대상 후보를 모으는 버퍼(용량 재사용)
        vector<GameObject*>          _listTagged;             ///< 스텝마다 태그 색인의 답을 받는 버퍼(용량 재사용, `TagIndex::findObjectsWithTags`)
        vector<TurretShotTarget>     _listShotTarget;         ///< 날아가는 탄이 이번 스텝에 맞힐 대상(용량 재사용)
        ProjectileSimulation         _projectiles;            ///< 날아가는 탄
        GameObjectHandle             _previousTarget;         ///< 지난 스텝의 대상(속도를 잴 때 같은 대상인지 본다)
        float3                       _previousTargetPosition; ///< 지난 스텝의 대상 자리
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructibleComponent
     * @brief 부서지는 엄폐물입니다. 받은 피해가 `_listStageThreshold` 를 넘을 때마다 단계가 오르고("Stage" 연출과 회로 신호), 마지막 임계값을 넘으면 몸을 끕니다.
     * @details 같은 오브젝트의 센서 신호에 단계(0..)를 넣어 회로가 읽게 한다. 오브젝트에 파괴 컴포넌트(`FractureComponentBase`)가 있으면 단계가 오를 때마다
     *          중심에 변형(`_stageStrain` × 단계 비율)을 주어 조각을 깎아 내고, 마지막 단계에서는 몸을 끄는 대신 `_shatterStrain` 으로 통째로 부순다.
     *          파쇄 데이터가 없으면 몸을 끈다(`GimmickCueEvent` "Destroyed").
     */
    REFLECT( Category = "Gimmick", DisplayName = "Destructible", Tooltip = "Damage stages ending in destruction (fracture hook)" )
    class SW_GF_API DestructibleComponent : public Component
    {
    public:
        REFLECT_BODY();

        DestructibleComponent();
        virtual ~DestructibleComponent() override = default;

        void onTick( TickContext& context ) override;
        void stepOnce();

        int32 getStage() const { return _stage; }
        bool  isDestroyed() const { return _stage >= static_cast<int32>( _listStageThreshold.size() ) && _listStageThreshold.empty() == false; }

    private:
        PROPERTY( Category = "Destructible", DisplayName = "Stage Thresholds", Tooltip = "Total damage at which each stage begins; the last destroys" )
        vector<float32> _listStageThreshold;
        PROPERTY( Category = "Destructible", DisplayName = "Stage Strain", Min = 0.0, Tooltip = "Fracture strain at the centre when a stage is reached (scaled by stage / last)" )
        float32 _stageStrain;
        PROPERTY( Category = "Destructible", DisplayName = "Shatter Strain", Min = 0.0, Tooltip = "Fracture strain that shatters the object at the last stage" )
        float32 _shatterStrain;
        PROPERTY( RuntimeState, Category = "Destructible", DisplayName = "Damage Taken", Tooltip = "Runtime" )
        float32 _damageTaken;
        PROPERTY( RuntimeState, Category = "Destructible", DisplayName = "Stage", Tooltip = "Runtime" )
        int32 _stage;

        FixedStepTimer _clock;
    };
} // namespace sw
