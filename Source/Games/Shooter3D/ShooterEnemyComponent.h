/**
 * @file ShooterEnemyComponent.h
 * @brief 스켈레톤 적 하나 — 땅에서 일어나 플레이어 쪽으로 걸어오며 이웃과 떨어지고 상자를 돌아가고(내비메시 에이전트), 닿으면 휘두르고, 맞으면 움찔하고, 쓰러집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Combat/HealthSourceComponent.h"
#include "GameFramework/Base/Utility/Countdown.h"

namespace sw
{
    /** @brief 적 하나의 행동 단계입니다. */
    enum class ShooterEnemyPhase : uint8
    {
        Rising = 0, ///< 땅에서 일어나는 중(스폰 클립) — 움직이지 않는다
        Chasing,    ///< 플레이어 쪽으로 온다
        Attacking,  ///< 휘두르는 중 — 클립의 맞는 시각에 손이 닿으면 플레이어를 때린다
        Staggered,  ///< 맞아 움찔 — 잠깐 멈춘다
        Dying,      ///< 쓰러지는 중 — 시체 시간이 지나면 디렉터가 걷는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterEnemyComponent
     * @brief 스켈레톤 프리팹(`prefabs/skeleton.prefab.xml`)의 컴포넌트입니다. 디렉터가 틱 뒤에 세우고 `launch` 로 자리 · 체력 · 빠르기를 줍니다.
     * @details 몸(같은 오브젝트의 `SkeletalMeshComponent`) · 애니메이터 · 외형(`CharacterAppearanceComponent` — 디렉터가 프리셋 · 씨앗을 고른다)과 같은
     *          오브젝트입니다. 기본 틱 그룹(`DuringPhysics`)에서 디렉터가 적은 플레이어 자리 · 적 자리 · 막는 상자를 읽고 **자기 오브젝트에만** 씁니다(자리 ·
     *          요 · 애니메이터 파라미터 · HP 바 — 체력 원천 알림 `notifyHealthChanged`). 애니메이터 그래프(`data/anim/skeleton.animgraph.json`)는 파라미터 `Move`(0 서기 · 1 걷기 · 2 달리기) ·
     *          `Attack` · `Hit`(트리거) · `Dead` 로 움직입니다. 플레이어를 때리는 것은 다른 오브젝트에 쓰는 일이라 틱 뒤로 미룹니다.
     *          맞음(`applyDamage`)은 플레이어의 사격이 틱 뒤에 겁니다. 히트박스는 발에서 `_height` 까지의 캡슐(반지름 `_radius`)입니다 —
     *          물리 질의 · 부위 히트박스가 들어오면 그쪽으로 옮긴다(소켓 `Chest` 가 히트박스 중심 자리).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Enemy", Tooltip = "Skeleton enemy that rises, walks to the player, swings, staggers when hit and collapses" )
    class ShooterEnemyComponent : public HealthSourceComponent
    {
    public:
        REFLECT_BODY();

        ShooterEnemyComponent();
        virtual ~ShooterEnemyComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 세웁니다 — 디렉터 · 발 자리 · 요 · 체력 · 빠르기(게임 스레드). */
        void launch( GameObjectHandle director, const float3& position, float32 yaw, float32 health, float32 speed );
        /** @brief 맞았다 — 체력을 깎고 움찔하거나 쓰러진다(틱 밖 · 게임 스레드). */
        void applyDamage( float32 amount );

        bool isLaunched() const { return _bLaunched == SW_TRUE; }
        bool isAlive() const { return _bLaunched == SW_TRUE && _health > 0.0f; }
        bool isDead() const { return _bLaunched == SW_TRUE && _health <= 0.0f; }
        /** @brief 체력 원천의 읽기입니다 — 쓰러짐은 `isDead()`. */
        HealthReading getHealthReading() const override;
        /** @brief 쓰러진 뒤 시체 시간이 지나 걷어도 되면 true 입니다. */
        bool              isRemovable() const { return isDead() && _phaseTime >= _corpseTime; }
        ShooterEnemyPhase getPhase() const { return _phase; }
        /** @brief 발 자리입니다. */
        const float3& getPosition() const { return _position; }
        float32       getRadius() const { return _radius; }
        float32       getHeight() const { return _height; }

    private:
        void enterPhase( ShooterEnemyPhase phase );
        /** @brief 같은 오브젝트의 애니메이터 파라미터를 씁니다(자기 오브젝트). */
        void updateAnimator( float32 moveCode );
        /** @brief 플레이어를 때리는 것을 틱 뒤로 미룹니다. */
        void requestPlayerDamage( GameObjectHandle player ) const;

    private:
        PROPERTY( Category = "Enemy", DisplayName = "Director", Tooltip = "Object with the ShooterDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Enemy", DisplayName = "Radius", Tooltip = "Capsule radius of the hit box and the body", Min = 0.0, Units = m )
        float32 _radius;
        PROPERTY( Category = "Enemy", DisplayName = "Height", Tooltip = "Top of the hit capsule above the feet", Min = 0.0, Units = m )
        float32 _height;
        PROPERTY( Category = "Enemy", DisplayName = "Reach", Tooltip = "Starts a swing inside this distance", Min = 0.0, Units = m )
        float32 _reach;
        PROPERTY( Category = "Enemy", DisplayName = "Damage", Tooltip = "Damage per swing that lands", Min = 0.0 )
        float32 _damage;
        PROPERTY( Category = "Enemy", DisplayName = "Attack Interval", Tooltip = "Seconds between swings", Min = 0.0, Units = s )
        float32 _attackInterval;
        PROPERTY( Category = "Enemy", DisplayName = "Attack Hit Time", Tooltip = "Seconds into the swing when the blade lands", Min = 0.0, Units = s )
        float32 _attackHitTime;
        PROPERTY( Category = "Enemy", DisplayName = "Attack Length", Tooltip = "Seconds the swing locks movement", Min = 0.0, Units = s )
        float32 _attackLength;
        PROPERTY( Category = "Enemy", DisplayName = "Rise Time", Tooltip = "Seconds spent rising from the ground before chasing", Min = 0.0, Units = s )
        float32 _riseTime;
        PROPERTY( Category = "Enemy", DisplayName = "Stagger Time", Tooltip = "Seconds a hit stops the enemy", Min = 0.0, Units = s )
        float32 _staggerTime;
        PROPERTY( Category = "Enemy", DisplayName = "Corpse Time", Tooltip = "Seconds the body stays after collapsing", Min = 0.0, Units = s )
        float32 _corpseTime;
        PROPERTY( Category = "Enemy", DisplayName = "Run Speed", Tooltip = "At or above this chase speed the run clip plays instead of the walk", Min = 0.0, Units = "m/s" )
        float32 _runSpeed;
        PROPERTY( Category = "Enemy", DisplayName = "Turn Rate", Tooltip = "How fast the body turns to its heading", Min = 0.0, Units = "rad/s" )
        float32 _turnRate;

        float3            _position;   ///< 발
        float3            _lastTarget; ///< 내비메시 에이전트에 마지막으로 건 목적지
        float32           _yaw;
        float32           _health;
        float32           _maxHealth;
        float32           _speed;
        float32           _phaseTime;
        Countdown         _attackCooldown; ///< 휘두르기 간격(늦음을 잇는다)
        ShooterEnemyPhase _phase;
        uint8             _bLaunched       : 1;
        uint8             _bHitPending     : 1; ///< 다음 틱에 애니메이터에 Hit 트리거를 건다
        uint8             _bAttackLanded   : 1; ///< 이번 휘두름이 이미 맞았다
        uint8             _bAttackStarting : 1; ///< 다음 틱에 애니메이터에 Attack 트리거를 건다
        uint8             _reserved        : 4;
    };
} // namespace sw
