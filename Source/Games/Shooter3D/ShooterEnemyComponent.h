/**
 * @file ShooterEnemyComponent.h
 * @brief 스켈레톤 적 하나(폰) — 땅에서 일어나 AI 조종자의 의도대로 걸어오고, 휘두르기 버튼이 눌리면 휘두르고, 맞으면 움찔하고, 쓰러집니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Combat/Health/HealthSourceComponent.h"
#include "GameFramework/Base/Character/Combat/Health/Vitality.h"

namespace sw
{
    /** @brief 적 하나의 행동 단계입니다. */
    enum class ShooterEnemyPhase : uint8
    {
        Rising = 0, ///< 땅에서 일어나는 중(스폰 클립) — 움직이지 않는다
        Chasing,    ///< 플레이어 쪽으로 온다
        Attacking,  ///< 휘두르는 중 — 클립의 맞는 알림(`_attackHitEvent`)이 오면 손이 닿을 때 플레이어를 때린다
        Staggered,  ///< 맞아 움찔 — 잠깐 멈춘다
        Dying,      ///< 쓰러지는 중 — 시체 시간이 지나면 디렉터가 걷는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShooterEnemyComponent
     * @brief 스켈레톤 프리팹(`prefabs/skeleton.prefab.xml`)의 규칙 컴포넌트입니다. 디렉터가 틱 뒤에 스폰하고 `launch` 로 위치 · 체력 · 속도를 줍니다.
     * @details 몸(같은 오브젝트의 `SkeletalMeshComponent`) · 애니메이터 · 외형(`CharacterAppearanceComponent` — 디렉터가 프리셋 · 씨앗을 고른다) · 폰(`PawnComponent` —
     *          버튼 `Attack`, 자동 빙의 `AI` → `ShooterEnemyAIControllerComponent`) · 몸 이동(`ShooterBodyMovementComponent` — 플레이어와 같은 이동)과 같은 오브젝트입니다.
     *          판단(쫓기 · 멈추기 · 휘두를지)은 AI 조종자가 하고, 여기는 **폰의 의도만** 읽습니다: 쫓는 단계에서 몸 이동을 한 걸음 부르고, 몸 요를 조종 요 쪽으로 돌리고,
     *          `Attack` 이 눌려 있으면 휘두르기 간격마다 휘두릅니다. 기본 틱 그룹(`DuringPhysics`)에서 디렉터가 적은 플레이어 위치 · 막는 상자를 읽고 **자기 오브젝트에만**
     *          씁니다(위치 · 요 · 애니메이터 파라미터 · HP 바 — 체력 원천 알림 `notifyHealthChanged`). 내비메시 에이전트(`SteerOnly`)는 그 위치를 다음 갱신에서 받는다.
     *          애니메이터 그래프(`data/anim/skeleton.animgraph.xml`)는 파라미터 `Move`(0 서기 · 1 걷기 · 2 달리기) ·
     *          `Attack` · `Hit`(트리거) · `Dead` 로 움직입니다. 플레이어를 때리는 것은 다른 오브젝트에 쓰는 일이라 틱 뒤로 미룹니다.
     *          휘두름이 맞는 시각은 클립 데이터다 — 클립 알림 `Impact` → 알림 표(`characters/skeleton_minion/skeleton_minion.notifies.xml`)의 `AbilityEvent` →
     *          `onAnimationNotify` 가 이벤트 `_attackHitEvent` 를 받으면 다음 틱에 손이 닿는지 보고 때린다. 휘두름이 끝날 때까지 이벤트가 없으면 데이터 오류로 알린다.
     *          **맞음**은 플레이어의 물리 질의 히트스캔이 틱 뒤에 `CharacterHitUtil::deliverHit` 으로 보낸다(`onHitReceived`). 히트박스는 같은 오브젝트의
     *          `RagdollComponent`(물리 에셋 `skeleton_warrior.physics.xml` — 뼈마다 바디와 히트 존 배율)이고, 피해는 기본 피해 × 히트 존 배율이다.
     *          쓰러지면 래그돌로 넘긴다(맞은 바디에 충격량). 쓰러지지 않은 맞음의 몸 흔들림은 래그돌 컴포넌트가 같은 맞음으로 스스로 한다.
     *          `_radius` · `_height` 는 몸 이동 · 내비 에이전트 · 자동 조준이 쓰는 몸 크기다(판정에는 쓰지 않는다).
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Enemy", Tooltip = "Skeleton enemy that rises, walks to the player, swings, staggers when hit and collapses" )
    class ShooterEnemyComponent : public HealthSourceComponent
    {
    public:
        REFLECT_BODY();

        ShooterEnemyComponent();
        virtual ~ShooterEnemyComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 무기 판정이 맞혔습니다(틱 밖 · 게임 스레드). 기본 피해 × 히트 존 배율을 깎고, 쓰러지면 맞은 바디에 충격량을 주며 래그돌로 넘깁니다. */
        void onHitReceived( const HitInfo& hit ) override;
        /** @brief 휘두름 클립의 맞는 알림(`_attackHitEvent`)을 받습니다(게임 스레드, 애니메이션 평가 뒤). 휘두르는 중이면 다음 틱이 맞힘을 처리합니다. */
        void onAnimationNotify( const AnimationNotifyInfo& notify ) override;

        /**
         * @brief 스폰합니다 — 디렉터 · 발 위치 · 요 · 체력 · 속도(게임 스레드).
         * @return 체력이 범위 밖(1 보다 작음)이라 거절되면 false 이다. 거절하면 아무것도 바꾸지 않으므로 부르는 쪽이 오브젝트를 정리한다(체력을 올려 대신 서지 않는다).
         */
        [[nodiscard]] bool launch( GameObjectHandle director, const float3& position, float32 yaw, float32 health, float32 speed );

        bool isLaunched() const { return _bLaunched == SW_TRUE; }
        bool isAlive() const { return _bLaunched == SW_TRUE && _vitality.isAlive(); }
        bool isDead() const { return _bLaunched == SW_TRUE && _vitality.isAlive() == false; }
        /** @brief 체력 원천의 읽기입니다 — 쓰러짐은 `isDead()`. */
        HealthReading getHealthReading() const override;
        /** @brief 쓰러진 뒤 시체 시간이 지나 걷어도 되면 true 입니다. */
        bool              isRemovable() const { return isDead() && _phaseTime >= _corpseTime; }
        ShooterEnemyPhase getPhase() const { return _phase; }
        /** @brief 발 위치입니다(몸 이동의 위치 — 틱마다 옮겨 적는다). */
        const float3& getPosition() const { return _position; }
        float32       getRadius() const { return _radius; }
        float32       getHeight() const { return _height; }
        /** @brief 휘두르기를 시작하고 맞히는 거리입니다(AI 조종자가 `Attack` 을 누르는 거리). */
        float32 getReach() const { return _reach; }
        /** @brief 손 닿는 거리의 이 비율 안이면 멈춰 섭니다(내비 에이전트 정지 거리와 AI 조종자가 같이 읽습니다). */
        float32          getStopFraction() const { return _stopFraction; }
        GameObjectHandle getDirector() const { return _director; }

    private:
        /** @brief 체력을 깎고 움찔하거나 쓰러진다(틱 밖 · 게임 스레드). 이번 피해로 쓰러졌으면 true 입니다(이미 쓰러졌으면 false). */
        [[nodiscard]] bool applyDamage( float32 amount );
        void               enterPhase( ShooterEnemyPhase phase );
        /** @brief 같은 오브젝트의 애니메이터 파라미터를 씁니다(자기 오브젝트). */
        void updateAnimator( float32 moveCode );
        /** @brief 플레이어를 때리는 것을 틱 뒤로 미룹니다. */
        void requestPlayerDamage( GameObjectHandle player ) const;
        /**
         * @brief 쓰러질 때 외형의 소켓 부품 가운데 강체(`RigidBodyComponent`)가 있는 것(무기 · 방패 프리팹)을 물리로 뗍니다(틱 뒤 · 게임 스레드).
         * @details 어느 부품을 떨어뜨릴지는 데이터(부품 프리팹에 강체가 있는가)가 정한다. 뗀 부품은 시체와 함께 외형이 걷는다(`despawnParts`).
         */
        void requestDropSocketParts() const;

    private:
        PROPERTY( Category = "Enemy", DisplayName = "Director", Tooltip = "Object with the ShooterDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Enemy", DisplayName = "Radius", Tooltip = "Capsule radius of the hit box and the body", Min = 0.0, Units = m )
        float32 _radius;
        PROPERTY( Category = "Enemy", DisplayName = "Height", Tooltip = "Top of the hit capsule above the feet", Min = 0.0, Units = m )
        float32 _height;
        PROPERTY( Category = "Enemy", DisplayName = "Reach", Tooltip = "Starts a swing inside this distance", Min = 0.0, Units = m )
        float32 _reach;
        PROPERTY( Category = "Enemy", DisplayName = "Stop Fraction", Tooltip = "Fraction of the reach at which the enemy stops walking (navmesh stopping distance and AI stop)", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _stopFraction;
        PROPERTY( Category = "Enemy", DisplayName = "Damage", Tooltip = "Damage per swing that lands", Min = 0.0 )
        float32 _damage;
        PROPERTY( Category = "Enemy", DisplayName = "Attack Interval", Tooltip = "Seconds between swings", Min = 0.0, Units = s )
        float32 _attackInterval;
        PROPERTY( Category = "Enemy", DisplayName = "Attack Hit Event", Tooltip = "Gameplay event (notify table) the swing clip sends when the blade lands" )
        hashed_string _attackHitEvent;
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
        PROPERTY( Category = "Enemy", DisplayName = "Turn Rate", Tooltip = "How fast the body turns to the control yaw", Min = 0.0, Units = "rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "Enemy", DisplayName = "Walk Clip Speed", Tooltip = "Below this speed the standing clip plays", Min = 0.0, Units = "m/s" )
        float32 _walkClipSpeed;

        float3            _position; ///< 발
        float32           _yaw;
        Vitality          _vitality; ///< 체력(플레이어와 같은 Base 체력 — 실드 · 회복 · 기절은 쓰지 않는다)
        float32           _speed;
        float32           _phaseTime;
        Countdown         _attackCooldown; ///< 휘두르기 간격(늦음을 잇는다)
        ShooterEnemyPhase _phase;
        uint8             _bLaunched                : 1;
        uint8             _bHitPending              : 1; ///< 다음 틱에 애니메이터에 Hit 트리거를 건다
        uint8             _bAttackLanded            : 1; ///< 이번 휘두름이 이미 맞았다
        uint8             _bAttackStarting          : 1; ///< 다음 틱에 애니메이터에 Attack 트리거를 건다
        uint8             _bAttackHitEvent          : 1; ///< 이번 휘두름의 맞는 알림을 받았다(다음 틱이 맞힘을 처리한다)
        uint8             _bHitEventMissingReported : 1; ///< 맞는 알림 없이 끝난 휘두름을 이미 오류로 알렸다(한 번만)
        uint8             _reserved                 : 2;
    };
} // namespace sw
