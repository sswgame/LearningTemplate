/**
 * @file PlatformerGimmicks.h
 * @brief 플랫포머 기믹 — 무너지는 발판 · 한쪽 발판 · 스프링/점프대(발사대) · 컨베이어 · 사다리/밧줄 구역입니다. 움직이는 발판은 회로의 `Mover` 프리팹입니다.
 * @details 모두 같은 오브젝트의 `GimmickSensorComponent`(겹친 것) 또는 겹침 훅을 읽어 2D · 3D 모두에서 돕니다. 시간은 60 Hz 걸음 수로 셉니다(결정적).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/FixedStepTimer.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 무너지는 발판의 상태입니다. */
    ENUM()
    enum class CrumbleState : uint8
    {
        Solid = 0, ///< 서 있을 수 있다
        Shaking,   ///< 누가 올라섰다 — `_crumbleDelay` 뒤에 무너진다
        Fallen     ///< 몸이 꺼졌다 — `_respawnDelay` 뒤에 되살아난다(0 이면 영영)
    };
} // namespace sw

namespace sw
{
    /** @class CrumblePlatformComponent */
    REFLECT( Category = "Gimmick", DisplayName = "Crumble Platform", Tooltip = "Falls a while after something stands on it, then respawns" )
    class SW_GF_API CrumblePlatformComponent : public Component
    {
    public:
        REFLECT_BODY();

        CrumblePlatformComponent();
        virtual ~CrumblePlatformComponent() override = default;

        void onTick( float32 deltaTime ) override;
        /** @brief 한 걸음(60 Hz)을 진행합니다(시험 · 롤백 재시뮬레이션). */
        void stepOnce();

        CrumbleState getState() const { return _state; }
        void         setDelays( float32 crumbleDelay, float32 respawnDelay );

    private:
        PROPERTY( Category = "Crumble", DisplayName = "Crumble Delay", Min = 0.0, Units = s )
        float32 _crumbleDelay;
        PROPERTY( Category = "Crumble", DisplayName = "Respawn Delay", Min = 0.0, Tooltip = "0 never respawns", Units = s )
        float32 _respawnDelay;
        PROPERTY( Category = "Crumble", DisplayName = "Steps In State", Tooltip = "Runtime" )
        int32 _stepsInState;
        PROPERTY( Category = "Crumble", DisplayName = "State", Tooltip = "Runtime" )
        CrumbleState _state;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class OneWayPlatformComponent
     * @brief 한쪽에서만 막는 발판입니다 — 이동 몸이 `isSolidFor( 속도 )` 를 묻습니다(위로 뛰어 오르면 통과, 내려오면 착지). 아래 키로 내려가기는 `_bDropThrough`.
     */
    REFLECT( Category = "Gimmick", DisplayName = "One-Way Platform", Tooltip = "Solid only against movement opposite to its normal" )
    class SW_GF_API OneWayPlatformComponent : public Component
    {
    public:
        REFLECT_BODY();

        OneWayPlatformComponent();
        virtual ~OneWayPlatformComponent() override = default;

        /** @brief @p velocity 로 움직이는 몸을 막는가 — 법선을 거슬러(법선 · 속도 < 0) 올 때만 막습니다. 아래로 내려가기를 허락했고 @p bDropRequested 면 막지 않습니다. */
        bool isSolidFor( const float3& velocity, bool bDropRequested ) const;

    private:
        PROPERTY( Category = "One Way", DisplayName = "Normal", Tooltip = "Side you can stand on (world)" )
        float3 _normal;
        PROPERTY( Category = "One Way", DisplayName = "Drop Through", Tooltip = "Pressing down drops through" )
        bool _bDropThrough;
    };
} // namespace sw

namespace sw
{
    /**
     * @class LaunchPadComponent
     * @brief 스프링 · 점프대 — 겹치기 시작한 것(태그 거르기)을 `_launchVelocity`(월드)로 쏩니다. `GravityComponent` 가 있으면 수직 속도를 그 자리에서 주고,
     *        `GimmickLaunchEvent` 를 냅니다(캐릭터 컨트롤러 · 이동 몸이 받는다).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Launch Pad", Tooltip = "Spring / jump pad: launches whatever starts overlapping" )
    class SW_GF_API LaunchPadComponent : public Component
    {
    public:
        REFLECT_BODY();

        LaunchPadComponent();
        virtual ~LaunchPadComponent() override = default;

        void onOverlapBegin( const OverlapInfo& overlap ) override;
        /** @brief @p target 을 쏩니다(겹침이 부른다 — 시험 · 스크립트도 부를 수 있다). */
        void  launch( GameObject& target );
        int32 getLaunchCount() const { return _launchCount; }

    private:
        PROPERTY( Category = "Launch", DisplayName = "Launch Velocity", Units = "m/s" )
        float3 _launchVelocity;
        PROPERTY( Category = "Launch", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Launch", DisplayName = "Launch Count", Tooltip = "Runtime" )
        int32 _launchCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ConveyorComponent
     * @brief 컨베이어 · 흐르는 물 — 같은 오브젝트의 센서에 겹친 것을 `_velocity`(월드, m/s)로 옮깁니다(틱 쓰기 큐 — 다른 오브젝트의 자리를 세터로 쓴다).
     *        이동 몸이 스스로 움직이는 게임은 `getSurfaceVelocity` 를 읽어 더하고 `_bMoveOccupants` 를 끕니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Conveyor", Tooltip = "Carries overlapping objects at a surface velocity" )
    class SW_GF_API ConveyorComponent : public Component
    {
    public:
        REFLECT_BODY();

        ConveyorComponent();
        virtual ~ConveyorComponent() override = default;

        void onTick( float32 deltaTime ) override;

        const float3& getSurfaceVelocity() const { return _velocity; }

    private:
        PROPERTY( Category = "Conveyor", DisplayName = "Velocity", Units = "m/s" )
        float3 _velocity;
        PROPERTY( Category = "Conveyor", DisplayName = "Move Occupants", Tooltip = "Move overlapping objects directly" )
        bool _bMoveOccupants;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ClimbZoneComponent
     * @brief 사다리 · 밧줄 구역 — 같은 오브젝트의 센서에 겹친 것은 `_axis` 를 따라 오를 수 있습니다. 이동 몸이 `findClimbZone` 으로 묻습니다(`_bSwing` 이면 밧줄).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Climb Zone", Tooltip = "Ladder or rope volume: overlapping objects may climb along the axis" )
    class SW_GF_API ClimbZoneComponent : public Component
    {
    public:
        REFLECT_BODY();

        ClimbZoneComponent();
        virtual ~ClimbZoneComponent() override = default;

        /** @brief @p object 가 이 구역 안인가입니다. */
        bool contains( GameObjectHandle object ) const;
        /** @brief @p object 가 들어 있는 구역입니다. 없으면 nullptr 입니다(씬의 구역을 훑는다). */
        static const ClimbZoneComponent* findClimbZone( const GameObjectManager& manager, GameObjectHandle object );

        const float3& getAxis() const { return _axis; }
        float32       getClimbSpeed() const { return _climbSpeed; }
        bool          isSwing() const { return _bSwing; }

    private:
        PROPERTY( Category = "Climb", DisplayName = "Axis", Tooltip = "Climb direction (world)" )
        float3 _axis;
        PROPERTY( Category = "Climb", DisplayName = "Climb Speed", Min = 0.0, Units = "m/s" )
        float32 _climbSpeed;
        PROPERTY( Category = "Climb", DisplayName = "Swing", Tooltip = "Rope: can swing and jump off sideways" )
        bool _bSwing;
    };
} // namespace sw
