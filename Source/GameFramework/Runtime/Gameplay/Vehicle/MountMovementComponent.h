/**
 * @file MountMovementComponent.h
 * @brief 말의 이동입니다. 의도를 걸음새(서기, 걷기, 속보, 구보, 질주), 조향, 루트 모션으로 바꿉니다. 탈것 폰(말 캡슐 캐릭터 컨트롤러)의 이동 규칙입니다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Movement/InertiaSettings.h"

namespace sw
{
    /** @brief 네발 탈것의 걸음새입니다. 애니메이터 `Gait` 파라미터 값이 이 순서와 같습니다. */
    ENUM()
    enum class MountGait : uint8
    {
        Idle = 0, ///< 서 있다. 제자리에서 돈다
        Walk,     ///< 평보
        Trot,     ///< 속보
        Canter,   ///< 구보
        Gallop,   ///< 습보(질주). 질주 버튼을 누르고 앞 방향 입력이 절반 이상이며 허용될 때만 쓴다(스태미나)
    };
} // namespace sw

namespace sw
{
    struct ControlIntent;

    /**
     * @brief 탈것 걸음이 다음 걸음으로 이어 가는 것 전부입니다 — 같은 (상태, 의도)면 같은 다음 상태다(`MountMovementComponent::simulate`). 칸 선언이 네트워크 바이트다(`ReplicatedLayout`, 양자화하지 않는다).
     * @details 걸음새 상한 · 질주 허용(스태미나 · 유대)은 키트가 정하는 값이지만 걸음에 들므로 상태로 싣는다 — 다시 돌리는 걸음이 지금 값이 아니라 그 걸음의 값을 쓴다.
     */
    REFLECT()
    struct SW_GF_API MountMoveState
    {
        REFLECT_BODY();

        PROPERTY( Replicated, Min = -1000000, Max = 1000000 )
        float3 _position{}; ///< 루트(월드)

        PROPERTY( Replicated, Min = -3.2, Max = 3.2 )
        float32 _facingYaw{ 0.0f }; ///< [-π, π] 로 감은 값

        PROPERTY( Replicated, Min = -1000, Max = 1000 )
        float32 _forwardSpeed{ 0.0f };

        PROPERTY( Replicated, Min = 0, Max = 4 )
        uint8 _gait{ 0 }; ///< `MountGait`. 범위는 cpp 의 static_assert 가 열거의 끝 값과 맞춘다

        PROPERTY( Replicated, Min = 0, Max = 4 )
        uint8 _maxGait{ 4 };

        PROPERTY( Replicated, Min = 0, Max = 1 )
        uint8 _bGallopAllowed{ SW_TRUE };

        MountGait getGait() const { return static_cast<MountGait>( _gait ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @class MountMovementComponent
     * @brief 말 오브젝트(폰, 캐릭터 컨트롤러, 외형과 애니메이터, 좌석)에 붙어서 의도를 걸음새로 바꿉니다. 의도의 월드 이동(조종 요 기준이며 3인칭과 같음)의
     *        크기가 걸음새를 정하고, 말은 그 방향으로 걸음새마다의 조향 속도만큼 몸을 돌리며 자기 요 방향으로 갑니다. 옆걸음과 제자리 뒷걸음은 없고,
     *        말은 돌아서 갑니다. 플레이어가 W 로 몰든 AI 가 목적지로 몰든 같은 규칙입니다.
     * @details 걸음새 사이의 속도는 `_acceleration` 과 `_deceleration` 으로 이어진다. 조향 속도는 걸음새마다 테이블(PROPERTY)로 둔다. `_bUseRootMotion` 이 켜져 있으면
     *          애니메이터 파라미터(`Gait` 0..4, `Turn` -1..1)만 쓰고 이동은 애니메이션 루트 모션이 한다(`CharacterControllerComponent::addRootMotionDisplacement` 를
     *          애니메이션 단계가 부른다). 캐릭터 컨트롤러가 없으면 루트를 직접 옮긴다(물리 없는 탈것). 질주 허용(`setGallopAllowed`)과
     *          걸음새 상한(`setMaxGait`)은 키트가 정한다(스태미나, 유대).
     *          관성 이동의 바깥 힘(`InertiaSettings` — 경사 중력 · 공기 저항, 바퀴 탈것 · 썰매와 같은 식)을 더한 뒤 걸음새 속도로 다가간다. 경사는 캐릭터 컨트롤러가 디딘 면의 법선으로 잰다.
     *          걸음새의 가속 · 감속이 경사의 당김보다 크면 걸음새 속도를 지키고, 가파르면 오르막에서 처지고 내리막에서 빨라진다. 표면은 이동 계수(`GroundSurfaceComponent`)가 이미 줄이므로
     *          구름 저항 배율은 기본 0 이다(발굽은 바퀴가 아니다).
     *          언리얼 CharacterMovement 위의 말 이동과 같은 구조이다. 같은 의도이면 플레이어가 타든 AI 가 몰든 같은 궤적이 나온다.
     *          네트워크 예측(`setNetDriven`): 캐릭터 컨트롤러 없이 루트를 옮기는 탈것은 걸음이 (상태, 의도)만의 순수 함수(`simulate`)라, 탄 클라이언트가 말의 이동을 자기 몸처럼 예측한다.
     *          컨트롤러가 있는 탈것은 켤 수 없다(오류) — 물리 이동은 서버 권위 + 보간으로 둔다(언리얼 Chaos 차량의 기본 복제와 같다).
     */
    REFLECT( Category = "Vehicle", DisplayName = "Mount Movement", Tooltip = "Control intent -> gait (idle, walk, trot, canter, gallop), steering and root motion for a ridden animal" )
    class SW_GF_API MountMovementComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PrePhysics;

        MountMovementComponent();
        ~MountMovementComponent() override = default;

        /** @brief 질주 버튼 인덱스를 찾고 PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 네트가 움직이는 동안의 시뮬레이션 자리를 옮깁니다(트랜스폼은 씬이 옮긴다). */
        void onWorldOriginShifted( const float3& delta ) override;

        MountGait getGait() const { return _gait; }
        /** @brief 몸이 향한 방향의 속도입니다(m/s). */
        float32 getForwardSpeed() const { return _forwardSpeed; }
        float32 getFacingYaw() const { return _facingYaw; }
        /** @brief 질주를 허용합니다. 키트가 스태미나로 정합니다. 허용되지 않으면 질주 요청은 구보로 처리합니다. */
        void setGallopAllowed( bool bAllowed ) { _bGallopAllowed = bAllowed ? SW_TRUE : SW_FALSE; }
        bool isGallopAllowed() const { return _bGallopAllowed == SW_TRUE; }
        /** @brief 걸음새 상한입니다. 유대가 낮은 말이나 짐을 실은 말에 씁니다. */
        void      setMaxGait( MountGait gait ) { _maxGait = gait; }
        MountGait getMaxGait() const { return _maxGait; }
        /** @brief 걸음새의 목표 속도입니다(m/s). */
        float32 computeGaitSpeed( MountGait gait ) const;
        /** @brief 걸음새의 조향 속도입니다(rad/s). */
        float32 computeGaitTurnRate( MountGait gait ) const;
        /** @brief 이동 크기(0..1)와 질주 버튼을 걸음새로 바꿉니다. 상한과 질주 허용은 아직 적용하지 않은 값입니다. */
        static MountGait computeRequestedGait( float32 moveAmount, bool bSprint );

        /**
         * @brief 고정 넷 틱이 이 탈것을 움직이는가입니다. 켜면 틱이 아무것도 하지 않고 `simulate` · `setState` 로 움직인다. 게임 스레드, 틱 밖에서 부릅니다.
         * @return 캐릭터 컨트롤러가 있거나 루트 모션으로 가는 탈것이면 오류를 남기고 false 입니다(켜지지 않는다).
         */
        [[nodiscard]] bool setNetDriven( bool bNetDriven );
        bool               isNetDriven() const { return _bNetDriven == SW_TRUE; }
        /** @brief 지금 이동 상태입니다. */
        MountMoveState getState() const;
        /** @brief 이동 상태를 바꾸고 루트를 @p displayPosition(그릴 자리 — 예측 보정 · 보간이 든다)에 둡니다. 애니메이터 파라미터도 맞춥니다. 네트가 움직이는 탈것만, 게임 스레드 · 틱 밖에서 부릅니다. */
        void setState( const MountMoveState& state, const float3& displayPosition );
        /** @brief @p state 에서 의도 @p intent 로 한 걸음(@p deltaTime)의 다음 상태입니다(순수 — 멤버를 바꾸지 않는다). */
        MountMoveState simulate( const MountMoveState& state, const ControlIntent& intent, float32 deltaTime ) const;

    private:
        /** @brief 걸음새 · 조향 · 속도를 한 걸음 나아갑니다(자리는 옮기지 않는다). @p slopeSin 은 진행 방향 오르막 사인, @p outTurn 은 조향(-1..1)입니다. */
        void advance( MountMoveState& inoutState, const ControlIntent& intent, float32 slopeSin, float32 deltaTime, float32& outTurn ) const;
        /** @brief 애니메이터 파라미터(걸음새 · 조향)를 씁니다. */
        void applyAnimatorParameters( float32 turn ) const;

    private:
        PROPERTY( Category = "Mount", DisplayName = "Sprint Button", Tooltip = "Intent button that asks for a gallop while held; empty = this mount cannot be asked to gallop" )
        hashed_string _sprintButton;
        PROPERTY( Category = "Mount", DisplayName = "Gait Parameter", Tooltip = "Animator float parameter set to the gait (0 idle .. 4 gallop); empty = none" )
        hashed_string _gaitParameter;
        PROPERTY( Category = "Mount", DisplayName = "Turn Parameter", Tooltip = "Animator float parameter set to the steering (-1..1); empty = none" )
        hashed_string _turnParameter;
        PROPERTY( Category = "Mount", DisplayName = "Walk Speed", Min = 0.0, Units = "m/s" )
        float32 _walkSpeed;
        PROPERTY( Category = "Mount", DisplayName = "Trot Speed", Min = 0.0, Units = "m/s" )
        float32 _trotSpeed;
        PROPERTY( Category = "Mount", DisplayName = "Canter Speed", Min = 0.0, Units = "m/s" )
        float32 _canterSpeed;
        PROPERTY( Category = "Mount", DisplayName = "Gallop Speed", Min = 0.0, Units = "m/s" )
        float32 _gallopSpeed;
        PROPERTY( Category = "Mount", DisplayName = "Acceleration", Min = 0.0, Units = "m/s2" )
        float32 _acceleration;
        PROPERTY( Category = "Mount", DisplayName = "Deceleration", Min = 0.0, Units = "m/s2" )
        float32 _deceleration;
        PROPERTY( Category = "Mount", DisplayName = "Idle Turn Rate", Min = 0.0, Tooltip = "Turning in place", Units = "rad/s" )
        float32 _idleTurnRate;
        PROPERTY( Category = "Mount", DisplayName = "Walk Turn Rate", Min = 0.0, Units = "rad/s" )
        float32 _walkTurnRate;
        PROPERTY( Category = "Mount", DisplayName = "Trot Turn Rate", Min = 0.0, Units = "rad/s" )
        float32 _trotTurnRate;
        PROPERTY( Category = "Mount", DisplayName = "Canter Turn Rate", Min = 0.0, Units = "rad/s" )
        float32 _canterTurnRate;
        PROPERTY( Category = "Mount", DisplayName = "Gallop Turn Rate", Min = 0.0, Units = "rad/s" )
        float32 _gallopTurnRate;
        PROPERTY( Category = "Mount", DisplayName = "Use Root Motion", Tooltip = "Animation root motion moves the mount; this component only sets the animator parameters" )
        bool _bUseRootMotion;
        PROPERTY( Category = "Mount", DisplayName = "Inertia", Tooltip = "Slope pull and air drag on top of the gait speed (same model as kinematic vehicles)" )
        InertiaSettings _inertia;

        PROPERTY( RuntimeState )
        float32 _forwardSpeed;
        PROPERTY( RuntimeState )
        float32      _facingYaw;
        SW_TRANSIENT optional<int32> _sprintIndex;
        PROPERTY( RuntimeState )
        MountGait _gait;
        PROPERTY( RuntimeState )
        MountGait           _maxGait;
        SW_TRANSIENT float3 _netPosition; ///< 네트가 움직이는 동안의 시뮬레이션 자리(트랜스폼은 그릴 자리다)
        PROPERTY( RuntimeState )
        uint8                  _bGallopAllowed : 1;
        SW_TRANSIENT uint8     _bNetDriven     : 1; ///< 고정 넷 틱이 움직인다(`setNetDriven`)
        [[maybe_unused]] uint8 _reserved       : 6;
    };
} // namespace sw
