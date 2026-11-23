/**
 * @file TurretComponent.h
 * @brief 시야 안의 태그 대상을 골라 돌고 쏘는 포탑과 그 탄이 묻는 세계입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
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
        /** @brief 지난 스텝의 대상 자리와 날아가는 탄을 옮깁니다. */
        void onWorldOriginShifted( const float3& delta ) override;
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
        void fireAt( GameObject& owner, const GameObject& victim, const float3& origin, const float3& direction );

    private:
        PROPERTY( Category = "Turret", DisplayName = "Target Tags" )
        TagContainer _targetTags;
        PROPERTY( Category = "Turret", DisplayName = "Range", Min = 0.0, Units = m )
        float32 _range;
        PROPERTY( Category = "Turret", DisplayName = "View Angle", Min = 0.0, Tooltip = "Half angle it can see, 0 sees all around" )
        Radians _viewAngle;
        PROPERTY( Category = "Turret", DisplayName = "Turn Speed", Min = 0.0, Units = "rad/s" )
        float32 _turnSpeed;
        PROPERTY( Category = "Turret", DisplayName = "Aim Tolerance", Min = 0.0 )
        Radians _aimTolerance;
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
