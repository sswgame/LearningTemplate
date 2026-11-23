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
        , _climbReleaseLeft{ 0.0f }
        , _bClimbing{ false }
        , _velocity{}
        , _facingYaw{ 0.0f }
        , _sprintIndex{}
        , _jumpIndex{}
        , _bSuspended{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void CharacterMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        GameObject*          pOwner  = getOwner();
        const PawnComponent* pPawn   = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _sprintIndex                 = pPawn != nullptr && _sprintButton.empty() == false ? pPawn->findButton( _sprintButton ) : nullopt;
        _jumpIndex                   = pPawn != nullptr && _jumpButton.empty() == false ? pPawn->findButton( _jumpButton ) : nullopt;
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
            _facingYaw = pScene->getLocalRotation()._y;
    }

    void CharacterMovementComponent::onTick( TickContext& context )
    {
        using Internal = CharacterPawnMovementComponentInternal;
        Component::onTick( context );
        if ( _bSuspended == SW_TRUE )
            return;
        GameObject*                   pOwner      = getOwner();
        const PawnComponent*          pPawn       = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pPawn == nullptr || pController == nullptr )
            return;
        const ControlIntent& intent = pPawn->getIntent();

        // 목표 수평 속도 — 의도의 월드 이동(길이 1 이하) × 걸음 · 달리기 속도.
        // 이펙트의 이동 제약(둔화 배율 · 속박/기절 · 점프 차단) — 이동기는 이 한 곳에서 읽는다(`MovementConstraint`).
        const MovementConstraint constraint = MovementConstraint::read( *pOwner );
        if ( updateClimb( context, intent, constraint, *pController ) )
            return;
        const bool    bSwimming = pController->getMovementMode() == CharacterMovementMode::Swimming;
        const float3  worldMove = constraint._bMoveBlocked == SW_TRUE ? float3{} : intent.computeWorldMove();
        const float32 landSpeed = intent.isDown( _sprintIndex ) ? _sprintSpeed : _walkSpeed;
        const float32 speed     = bSwimming ? _swimSpeed : landSpeed;
        const float3  target{ worldMove._x * speed, 0.0f, worldMove._z * speed };
        const bool    bHasTarget = target._x != 0.0f || target._z != 0.0f;
        _velocity                = Internal::moveTowards( _velocity, target, ( bHasTarget ? _acceleration : _deceleration ) * context.getDeltaTime() );
        pController->setMoveVelocity( _velocity );
        // 둔화 배율은 컨트롤러의 이동 계수로 넘긴다 — 표면 · 지역 입구(`ZoneContactComponent`)가 같은 계약을 넘겨도 최솟값이라 두 번 곱하지 않는다.
        pController->applyMoveScale( constraint._speedScale );
        // 헤엄의 위아래 — 위아래 의도와 점프 누름(언리얼은 물에서 점프가 위로 헤엄친다). 물 밖에서는 0 이라 컨트롤러가 쓰지 않는다.
        const bool bJumpAllowed = constraint._bJumpBlocked == SW_FALSE;
        float32    swimUp       = worldMove._y;
        if ( bJumpAllowed && intent.isDown( _jumpIndex ) )
            swimUp = 1.0f;
        pController->setVerticalMoveSpeed( bSwimming ? MathUtil::clamp( swimUp, -1.0f, 1.0f ) * _swimSpeed : 0.0f );
        // 점프는 디뎠거나 몸이 다 잠기지 않은 채(수면) 헤엄칠 때다 — 깊은 물속의 점프 누름은 위로 헤엄치기뿐이다.
        const bool bAtSurface = bSwimming && pController->getPhysicsField()._immersion < 1.0f;
        if ( intent.wasTriggered( _jumpIndex ) && bJumpAllowed && ( pController->isGrounded() || bAtSurface ) )
            pController->jump( _jumpSpeed );

        // 몸 방향 — 조종 요, 또는 움직이는 쪽.
        float32       targetYaw   = _facingYaw;
        const float32 movingSpeed = MathUtil::sqrt( _velocity._x * _velocity._x + _velocity._z * _velocity._z );
        if ( _facingMode == PawnFacingMode::ControlYaw )
            targetYaw = intent._controlYaw;
        else if ( movingSpeed > Internal::kMinFacingSpeed )
            targetYaw = MathUtil::atan2( _velocity._x, _velocity._z );
        turnToward( *pOwner, targetYaw, context.getDeltaTime() );
    }

    bool CharacterMovementComponent::updateClimb( const TickContext& context, const ControlIntent& intent, const MovementConstraint& constraint, CharacterControllerComponent& controller )
    {
        const bool bWasClimbing = _bClimbing;
        _bClimbing              = false;
        _climbReleaseLeft       = MathUtil::max( 0.0f, _climbReleaseLeft - context.getDeltaTime() );
        if ( _climbReleaseLeft > 0.0f )
            return false;
        GameObject&                     owner = context.getSelf();
        const ClimbZoneComponent* const pZone = ClimbZoneComponent::findClimbZone( context.getManager(), owner.getHandle() );
        if ( pZone == nullptr )
            return false;
        const float3  facing    = pZone->computeFacing();
        const float3  worldMove = constraint._bMoveBlocked == SW_TRUE ? float3{} : intent.computeWorldMove();
        const float32 into      = worldMove._x * facing._x + worldMove._z * facing._z;
        const float32 climb     = MathUtil::clamp( worldMove._y + into, -1.0f, 1.0f );
        // 땅에서는 올라가는 쪽으로만 잡는다. 공중에서는 어느 쪽이든 누르면 잡고(발판 끝에서 뒷걸음쳐 내려가기), 매달린 뒤에는 놓아도 그 자리에 머문다.
        const bool bGrab = controller.isGrounded() ? climb > 0.0f : ( climb != 0.0f || bWasClimbing );
        if ( bGrab == false )
            return false;
        _velocity = float3{};
        controller.setMoveVelocity( _velocity );
        controller.setVerticalMoveSpeed( 0.0f );
        if ( bWasClimbing && intent.wasTriggered( _jumpIndex ) && constraint._bJumpBlocked == SW_FALSE )
        {
            const float3 flatMove{ worldMove._x, 0.0f, worldMove._z };
            const bool   bAlongMove = pZone->isSwing() && flatMove.getLengthSquared() > 0.0f;
            const float3 away       = bAlongMove ? flatMove.normalize() : facing * -1.0f;
            controller.launch( away * _climbPushOffSpeed + float3{ 0.0f, _jumpSpeed, 0.0f } );
            _climbReleaseLeft = _climbReleaseSeconds;
            return true;
        }
        const float3 right{ facing._z, 0.0f, -facing._x };
        const float3 along   = pZone->computeAxis() * ( climb * pZone->getClimbSpeed() );
        const float3 lateral = right * ( ( worldMove._x * right._x + worldMove._z * right._z ) * pZone->getLateralSpeed() );
        // 벽 쪽으로 미는 몫은 걷는 속도로 함께 민다 — 벽이 막는 동안은 붙어 있게 하고, 꼭대기에서는 발판 위로 걸어 나가게 한다. 밧줄은 뒤에 벽이 없어 밀면 놓친다.
        const float3 toward = pZone->isSwing() ? float3{} : facing * ( MathUtil::max( into, 0.0f ) * _walkSpeed );
        controller.applyClimbVelocity( ( along + lateral + toward ) * constraint._speedScale );
        _bClimbing = true;
        turnToward( owner, MathUtil::atan2( facing._x, facing._z ), context.getDeltaTime() );
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

    void CharacterMovementComponent::turnToward( GameObject& owner, float32 targetYaw, float32 deltaTime )
    {
        using Internal       = CharacterPawnMovementComponentInternal;
        const float32 newYaw = OrientationUtil::turnTowardAngle( _facingYaw, targetYaw, _turnRate * deltaTime );
        if ( MathUtil::abs( MathUtil::wrapAngle( newYaw - _facingYaw ) ) <= Internal::kMinYawChange )
            return;
        _facingYaw             = newYaw;
        SceneComponent* pScene = owner.getPrimarySceneComponent();
        if ( pScene != nullptr )
        {
            float3 rotation = pScene->getLocalRotation();
            rotation._y     = _facingYaw;
            pScene->setLocalRotation( rotation );
        }
    }

    void CharacterMovementComponent::setSuspended( bool bSuspended )
    {
        const uint8 bNewSuspended = bSuspended ? SW_TRUE : SW_FALSE;
        if ( _bSuspended == bNewSuspended )
            return;
        _bSuspended = bNewSuspended;
        if ( bSuspended == false )
            return;
        _velocity                                 = float3{};
        _bClimbing                                = false;
        GameObject*                   pOwner      = getOwner();
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
        {
            pController->setMoveVelocity( float3{} );
            pController->setVerticalMoveSpeed( 0.0f );
        }
    }
} // namespace sw
