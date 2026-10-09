/**
 * @file MountMovementComponent.h
 * @brief 말 이동 — 의도 → 걸음새(서기 · 걷기 · 속보 · 구보 · 질주) · 조향 · 루트 모션. 탈것 폰(말 캡슐 캐릭터 컨트롤러)의 이동 규칙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 네발 탈것의 걸음새입니다(애니메이터 `Gait` 파라미터 값 = 이 순서). */
    ENUM()
    enum class MountGait : uint8
    {
        Idle = 0, ///< 서 있다(제자리에서 돈다)
        Walk,     ///< 평보
        Trot,     ///< 속보
        Canter,   ///< 구보
        Gallop,   ///< 습보(질주) — 질주 버튼 + 앞 절반 이상, 허용될 때만(스태미나)
    };
} // namespace sw

namespace sw
{
    /**
     * @class MountMovementComponent
     * @brief 말 오브젝트(폰 + 캐릭터 컨트롤러 + 외형 · 애니메이터 + 좌석)에 붙어 의도를 걸음새로 바꿉니다. 의도의 월드 이동(조종 요 기준 — 3인칭과 같다)의
     *        크기가 걸음새를 정하고, 말은 그 방향으로 걸음새마다의 조향 속도만큼 몸을 돌리며 **자기 요 방향으로** 갑니다(옆걸음 · 제자리 뒷걸음 없음 —
     *        말은 돌아서 간다). 플레이어가 W 로 몰든 AI 가 목적지로 몰든 같은 규칙입니다.
     * @details 걸음새 사이는 `_acceleration` · `_deceleration` 으로 속도가 이어집니다. 조향 속도는 걸음새마다 표(PROPERTY)입니다. `_bUseRootMotion` 이면
     *          애니메이터 파라미터(`Gait` 0..4 · `Turn` −1..1)만 쓰고 이동은 애니메이션 루트 모션이 합니다(`CharacterControllerComponent::addRootMotionDisplacement`
     *          — 애니메이션 단계가 넣는다). 캐릭터 컨트롤러가 없으면 루트를 직접 옮깁니다(물리 없는 탈것). 질주 허용(`setGallopAllowed`)과
     *          걸음새 상한(`setMaxGait`)은 키트가 정합니다(스태미나 · 유대).
     *          언리얼 Red Dead 류 말 이동과 같은 모양 — 같은 의도면 플레이어가 타든 AI 가 몰든 같은 궤적입니다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Mount Movement", Tooltip = "Control intent -> gait (idle, walk, trot, canter, gallop), steering and root motion for a ridden animal" )
    class SW_GF_API MountMovementComponent : public Component
    {
    public:
        REFLECT_BODY();

        MountMovementComponent();
        ~MountMovementComponent() override = default;

        /** @brief 질주 버튼 자리를 풀고 PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        MountGait getGait() const { return _gait; }
        /** @brief 몸이 향한 쪽으로의 속도입니다(m/s). */
        float32 getForwardSpeed() const { return _forwardSpeed; }
        float32 getFacingYaw() const { return _facingYaw; }
        /** @brief 질주를 허용합니다(키트가 스태미나로 정한다). 허용되지 않으면 질주 요청은 구보입니다. */
        void setGallopAllowed( bool bAllowed ) { _bGallopAllowed = bAllowed ? SW_TRUE : SW_FALSE; }
        bool isGallopAllowed() const { return _bGallopAllowed == SW_TRUE; }
        /** @brief 걸음새 상한입니다(유대가 낮은 말 · 짐 실은 말). */
        void      setMaxGait( MountGait gait ) { _maxGait = gait; }
        MountGait getMaxGait() const { return _maxGait; }
        /** @brief 걸음새의 목표 속도입니다(m/s). */
        float32 computeGaitSpeed( MountGait gait ) const;
        /** @brief 걸음새의 조향 속도입니다(rad/s). */
        float32 computeGaitTurnRate( MountGait gait ) const;
        /** @brief 이동 크기(0..1) · 질주 버튼 → 걸음새입니다(상한 · 질주 허용 적용 전). */
        static MountGait computeRequestedGait( float32 moveAmount, bool bSprint );

    private:
        PROPERTY( Category = "Mount", DisplayName = "Sprint Button", Tooltip = "Intent button that asks for a gallop while held" )
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

        float32                _forwardSpeed;
        float32                _facingYaw;
        int32                  _sprintIndex;
        MountGait              _gait;
        MountGait              _maxGait;
        uint8                  _bGallopAllowed : 1;
        [[maybe_unused]] uint8 _reserved       : 7;
    };
} // namespace sw
