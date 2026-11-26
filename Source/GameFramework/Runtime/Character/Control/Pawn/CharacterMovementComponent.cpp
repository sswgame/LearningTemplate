#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Pawn/CharacterMovementComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/ClimbZoneComponent.h"

namespace sw
{
    namespace
    {
        struct CharacterPawnMovementComponentInternal
        {
            /** @brief 이보다 느리면 "서 있다" 로 봅니다(움직이는 쪽을 볼 때 방향을 바꾸지 않는다). */
            static constexpr float32 kMinFacingSpeed = 0.05f;
            /** @brief 몸 요가 이보다 덜 바뀌면 트랜스폼을 쓰지 않습니다(라디안). */
            static constexpr float32 kMinYawChange = 1.0e-5f;

            /** @brief @p current 를 @p target 으로 최대 @p maxDelta 만큼 옮긴 수평 속도입니다. */
            static float3 moveTowards( const float3& current, const float3& target, float32 maxDelta )
            {
                const float32 deltaX = target._x - current._x;
                const float32 deltaZ = target._z - current._z;
                const float32 length = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
                if ( length <= maxDelta || length <= 0.0f )
                    return float3{ target._x, 0.0f, target._z };
                const float32 scale = maxDelta / length;
                return float3{ current._x + deltaX * scale, 0.0f, current._z + deltaZ * scale };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CharacterMovementComponent::CharacterMovementComponent()
        : _sprintButton{}
        , _jumpButton{}
        , _walkSpeed{ 4.0f }
        , _sprintSpeed{ 7.0f }
        , _acceleration{ 30.0f }
        , _deceleration{ 40.0f }
        , _jumpSpeed{ 5.0f }
        , _swimSpeed{ 2.5f }
        , _climbPushOffSpeed{ 3.0f }
        , _climbReleaseSeconds{ 0.3f }
        , _turnRate{ 12.0f }
        , _facingMode{ PawnFacingMode::MoveDirection }
        , _velocity{}
        , _climbReleaseLeft{ 0.0f }
        , _bClimbing{ false }
        , _teleport{}
        , _facingYaw{ 0.0f }
        , _sprintIndex{}
        , _jumpIndex{}
        , _bNetDriven{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void CharacterMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        resolveButtons();
        const SceneComponent* pScene = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
            _facingYaw = pScene->getLocalRotation()._y;
    }

    void CharacterMovementComponent::resolveButtons()
    {
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _sprintIndex                = pPawn != nullptr && _sprintButton.empty() == false ? pPawn->findButton( _sprintButton ) : nullopt;
        _jumpIndex                  = pPawn != nullptr && _jumpButton.empty() == false ? pPawn->findButton( _jumpButton ) : nullopt;
    }

    void CharacterMovementComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bNetDriven == SW_TRUE )
            return;
        GameObject*                   pOwner      = getOwner();
        const PawnComponent*          pPawn       = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pPawn == nullptr || pController == nullptr )
            return;
        // 멈춰 둔 몸(탑승 · 래그돌)은 걷지 않는다 — 다시 걸을 때 옛 속도로 나가지 않게 이어 오던 것을 지운다.
        if ( pController->isSuspended() )
        {
            _velocity  = float3{};
            _bClimbing = false;
            pController->setMoveVelocity( float3{} );
            pController->setVerticalMoveSpeed( 0.0f );
            return;
        }
        // 운동을 지우는 순간이동(리스폰 · 문)은 컨트롤러의 것만이 아니라 여기서 잇는 걷는 속도 · 매달림도 지운다 — 남기면 옛 걸음으로 미끄러져 나간다.
        if ( _teleport.consume( *pController ) == TeleportType::ResetPhysics )
        {
            _velocity         = float3{};
            _climbReleaseLeft = 0.0f;
            _bClimbing        = false;
        }
        // 이펙트의 이동 제약(둔화 배율 · 속박/기절 · 점프 차단) — 이동기는 이 한 곳에서 읽는다(`MovementConstraint`).
        CharacterMoveState state         = getState();
        state._facingYaw                 = _facingYaw; // 트랜스폼에 쓴 값 그대로 — 감은 값과 견주면 저작한 각이 ±π 밖일 때 다시 쓴다
        const CharacterMotionInput input = advance( state, pPawn->getIntent(), MovementConstraint::read( *pOwner ), *pController, context.getDeltaTime().count() );
        _velocity                        = state._velocity;
        _climbReleaseLeft                = state._climbReleaseLeft;
        _bClimbing                       = state._bClimbing == SW_TRUE;
        pController->applyMotionInput( input );
        applyFacingYaw( state._facingYaw );
    }

    CharacterMotionInput CharacterMovementComponent::advance( CharacterMoveState& inoutState, const ControlIntent& intent, const MovementConstraint& constraint,
                                                              const CharacterControllerComponent& controller, float32 deltaTime ) const
    {
        using Internal = CharacterPawnMovementComponentInternal;
        CharacterMotionInput input;
        if ( advanceClimb( inoutState, intent, constraint, deltaTime, input ) )
            return input;
        // 목표 수평 속도 — 의도의 월드 이동(길이 1 이하) × 걸음 · 달리기 속도.
        const bool    bSwimming = inoutState._motion.getMovementMode() == CharacterMovementMode::Swimming;
        const float3  worldMove = constraint._bMoveBlocked == SW_TRUE ? float3{} : intent.computeWorldMove();
        const float32 landSpeed = intent.isDown( _sprintIndex ) ? _sprintSpeed : _walkSpeed;
        const float32 speed     = bSwimming ? _swimSpeed : landSpeed;
        const float3  target{ worldMove._x * speed, 0.0f, worldMove._z * speed };
        const bool    bHasTarget = target._x != 0.0f || target._z != 0.0f;
        inoutState._velocity     = Internal::moveTowards( inoutState._velocity, target, ( bHasTarget ? _acceleration : _deceleration ) * deltaTime );
        input._moveVelocity      = inoutState._velocity;
        // 둔화 배율은 컨트롤러의 이동 계수로 넘긴다 — 표면 · 지역 입구(`ZoneContactComponent`)가 같은 계약을 넘겨도 최솟값이라 두 번 곱하지 않는다.
        input._moveScale = constraint._speedScale;
        // 헤엄의 위아래 — 위아래 의도와 점프 누름(언리얼은 물에서 점프가 위로 헤엄친다). 물 밖에서는 0 이라 컨트롤러가 쓰지 않는다.
        const bool bJumpAllowed = constraint._bJumpBlocked == SW_FALSE;
        float32    swimUp       = worldMove._y;
        if ( bJumpAllowed && intent.isDown( _jumpIndex ) )
            swimUp = 1.0f;
        input._verticalMoveSpeed = bSwimming ? MathUtil::clamp( swimUp, -1.0f, 1.0f ) * _swimSpeed : 0.0f;
        // 점프는 디뎠거나 몸이 다 잠기지 않은 채(수면) 헤엄칠 때다 — 깊은 물속의 점프 누름은 위로 헤엄치기뿐이다.
        const bool bAtSurface = bSwimming && controller.getPhysicsField()._immersion < 1.0f;
        if ( intent.wasTriggered( _jumpIndex ) && bJumpAllowed && ( inoutState._motion._bGrounded == SW_TRUE || bAtSurface ) )
            input._jumpSpeed = _jumpSpeed;

        // 몸 방향 — 조종 요, 또는 움직이는 쪽.
        float32       targetYaw   = inoutState._facingYaw;
        const float32 movingSpeed = MathUtil::sqrt( inoutState._velocity._x * inoutState._velocity._x + inoutState._velocity._z * inoutState._velocity._z );
        if ( _facingMode == PawnFacingMode::ControlYaw )
            targetYaw = intent._controlYaw;
        else if ( movingSpeed > Internal::kMinFacingSpeed )
            targetYaw = MathUtil::atan2( inoutState._velocity._x, inoutState._velocity._z );
        inoutState._facingYaw = turnToward( inoutState._facingYaw, targetYaw, deltaTime );
        return input;
    }

    bool CharacterMovementComponent::advanceClimb( CharacterMoveState& inoutState, const ControlIntent& intent, const MovementConstraint& constraint, float32 deltaTime,
                                                   CharacterMotionInput& outInput ) const
    {
        const bool bWasClimbing      = inoutState._bClimbing == SW_TRUE;
        inoutState._bClimbing        = SW_FALSE;
        inoutState._climbReleaseLeft = MathUtil::max( 0.0f, inoutState._climbReleaseLeft - deltaTime );
        if ( inoutState._climbReleaseLeft > 0.0f )
            return false;
        const GameObject*               pOwner   = getOwner();
        const GameObjectManager* const  pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ClimbZoneComponent* const pZone    = pManager != nullptr ? ClimbZoneComponent::findClimbZone( *pManager, pOwner->getHandle() ) : nullptr;
        if ( pZone == nullptr )
            return false;
        const float3  facing    = pZone->computeFacing();
        const float3  worldMove = constraint._bMoveBlocked == SW_TRUE ? float3{} : intent.computeWorldMove();
        const float32 into      = worldMove._x * facing._x + worldMove._z * facing._z;
        const float32 climb     = MathUtil::clamp( worldMove._y + into, -1.0f, 1.0f );
        // 땅에서는 올라가는 쪽으로만 잡는다. 공중에서는 어느 쪽이든 누르면 잡고(발판 끝에서 뒷걸음쳐 내려가기), 매달린 뒤에는 놓아도 그 자리에 머문다.
        const bool bGrab = inoutState._motion._bGrounded == SW_TRUE ? climb > 0.0f : ( climb != 0.0f || bWasClimbing );
        if ( bGrab == false )
            return false;
        inoutState._velocity = float3{};
        if ( bWasClimbing && intent.wasTriggered( _jumpIndex ) && constraint._bJumpBlocked == SW_FALSE )
        {
            const float3 flatMove{ worldMove._x, 0.0f, worldMove._z };
            const bool   bAlongMove      = pZone->isSwing() && flatMove.getLengthSquared() > 0.0f;
            const float3 away            = bAlongMove ? flatMove.normalize() : facing * -1.0f;
            outInput._launchVelocity     = away * _climbPushOffSpeed + float3{ 0.0f, _jumpSpeed, 0.0f };
            inoutState._climbReleaseLeft = _climbReleaseSeconds;
            return true;
        }
        const float3 right{ facing._z, 0.0f, -facing._x };
        const float3 along   = pZone->computeAxis() * ( climb * pZone->getClimbSpeed() );
        const float3 lateral = right * ( ( worldMove._x * right._x + worldMove._z * right._z ) * pZone->getLateralSpeed() );
        // 벽 쪽으로 미는 몫은 걷는 속도로 함께 민다 — 벽이 막는 동안은 붙어 있게 하고, 꼭대기에서는 발판 위로 걸어 나가게 한다. 밧줄은 뒤에 벽이 없어 밀면 놓친다.
        const float3 toward     = pZone->isSwing() ? float3{} : facing * ( MathUtil::max( into, 0.0f ) * _walkSpeed );
        outInput._climbVelocity = ( along + lateral + toward ) * constraint._speedScale;
        outInput._bClimb        = SW_TRUE;
        inoutState._bClimbing   = SW_TRUE;
        inoutState._facingYaw   = turnToward( inoutState._facingYaw, MathUtil::atan2( facing._x, facing._z ), deltaTime );
        return true;
    }

    void CharacterMovementComponent::setFacingYaw( float32 facingYaw )
    {
        _facingYaw             = facingYaw;
        SceneComponent* pScene = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return;
        float3 rotation = pScene->getLocalRotation();
        rotation._y     = _facingYaw;
        pScene->setLocalRotation( rotation );
    }

    float32 CharacterMovementComponent::turnToward( float32 facingYaw, float32 targetYaw, float32 deltaTime ) const
    {
        using Internal       = CharacterPawnMovementComponentInternal;
        const float32 newYaw = OrientationUtil::turnTowardAngle( facingYaw, targetYaw, _turnRate * deltaTime );
        return MathUtil::abs( MathUtil::wrapAngle( newYaw - facingYaw ) ) <= Internal::kMinYawChange ? facingYaw : MathUtil::wrapAngle( newYaw );
    }

    void CharacterMovementComponent::applyFacingYaw( float32 facingYaw )
    {
        if ( facingYaw != _facingYaw )
            setFacingYaw( facingYaw );
    }

    void CharacterMovementComponent::setNetDriven( bool bNetDriven )
    {
        _bNetDriven                               = bNetDriven ? SW_TRUE : SW_FALSE;
        GameObject*                   pOwner      = getOwner();
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
            pController->setManualSimulation( bNetDriven );
    }

    CharacterMoveState CharacterMovementComponent::getState() const
    {
        const GameObject*                   pOwner      = getOwner();
        const CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        CharacterMoveState                  state;
        if ( pController != nullptr )
            state._motion = pController->getMotionState();
        state._velocity         = _velocity;
        state._climbReleaseLeft = _climbReleaseLeft;
        state._facingYaw        = MathUtil::wrapAngle( _facingYaw );
        state._bClimbing        = _bClimbing ? SW_TRUE : SW_FALSE;
        return state;
    }

    void CharacterMovementComponent::setState( const CharacterMoveState& state )
    {
        _velocity                                 = state._velocity;
        _climbReleaseLeft                         = state._climbReleaseLeft;
        _bClimbing                                = state._bClimbing == SW_TRUE;
        GameObject*                   pOwner      = getOwner();
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
            pController->setMotionState( state._motion );
        applyFacingYaw( state._facingYaw );
    }

    CharacterMoveState CharacterMovementComponent::simulate( const CharacterMoveState& state, const ControlIntent& intent, float32 deltaTime, CharacterMotionPass pass )
    {
        GameObject*                   pOwner      = getOwner();
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController == nullptr || pController->getCharacterHandle().isValid() == false )
            return state; // 몸이 아직 서지 않았다(첫 물리 프레임 전 · 탑승 중) — 입력은 썼고 자리는 그대로다
        CharacterMoveState         next  = state;
        const CharacterMotionInput input = advance( next, intent, MovementConstraint::read( *pOwner ), *pController, deltaTime );
        next._motion                     = pController->simulateMotion( next._motion, input, deltaTime, pass );
        return next;
    }

    void CharacterMovementComponent::copyTuning( const CharacterMovementComponent& source )
    {
        _sprintButton        = source._sprintButton;
        _jumpButton          = source._jumpButton;
        _walkSpeed           = source._walkSpeed;
        _sprintSpeed         = source._sprintSpeed;
        _acceleration        = source._acceleration;
        _deceleration        = source._deceleration;
        _jumpSpeed           = source._jumpSpeed;
        _swimSpeed           = source._swimSpeed;
        _climbPushOffSpeed   = source._climbPushOffSpeed;
        _climbReleaseSeconds = source._climbReleaseSeconds;
        _turnRate            = source._turnRate;
        _facingMode          = source._facingMode;
        resolveButtons();
    }
} // namespace sw
