#include "pch.h"

#include "GameFramework/Base/Actor/Control/Controller/AiControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Navigation/NavMeshAgentComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"

namespace sw
{
    SW_LOG_CALLER( "AiControllerComponent" );

    namespace
    {
        struct AiControllerComponentInternal
        {
            /** @brief 이보다 짧은 이동 방향은 "서 있다" 로 봅니다(바라보기를 바꾸지 않는다). */
            static constexpr float32 kMinMoveLength = 1.0e-3f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    AiControllerComponent::AiControllerComponent()
        : _turnRate{ 6.0f }
        , _arriveDistance{ 0.3f }
        , _pending{}
        , _destination{}
        , _focusPoint{}
        , _heldButtonMask{ 0 }
        , _moveStatus{ NavMoveStatus::Idle }
        , _bHasDestination{ SW_FALSE }
        , _bHasFocus{ SW_FALSE }
        , _bDestinationSent{ SW_FALSE }
        , _bStopPending{ SW_FALSE }
        , _bWarnedDriveMode{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AiControllerComponent::produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent )
    {
        using Internal = AiControllerComponentInternal;
        // 틱 사이(think 밖)에 부른 pressButton · setAnalog 도 이번 의도에 싣는다 — 낸 뒤에 비운다.
        think( context, pawn );
        outIntent = _pending;
        _pending  = ControlIntent{};
        outIntent._buttonDown |= _heldButtonMask;

        const GameObject*     pPawnOwner = pawn.getOwner();
        const SceneComponent* pScene     = pPawnOwner != nullptr ? pPawnOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return;
        const float3  position      = pScene->getWorldPosition();
        const float3  moveDirection = computeMoveDirection( pawn, position );
        const float32 moveLength    = MathUtil::sqrt( moveDirection._x * moveDirection._x + moveDirection._z * moveDirection._z );

        // 바라보기 — 초점이 있으면 그쪽(요 · 피치), 없으면 움직이는 쪽(요만, 피치는 0 으로).
        const float32 maxStep = _turnRate * context._deltaTime;
        float32       yaw     = getControlYaw();
        float32       pitch   = getControlPitch();
        if ( _bHasFocus == SW_TRUE )
        {
            const float3  toFocus    = _focusPoint - position;
            const float32 horizontal = MathUtil::sqrt( toFocus._x * toFocus._x + toFocus._z * toFocus._z );
            if ( horizontal > Internal::kMinMoveLength )
                yaw = OrientationUtil::turnTowardAngle( yaw, MathUtil::atan2( toFocus._x, toFocus._z ), maxStep );
            const float32 targetPitch = MathUtil::clamp( MathUtil::atan2( toFocus._y, horizontal ), -pawn.getMaxPitch(), pawn.getMaxPitch() );
            pitch                     = OrientationUtil::turnTowardAngle( pitch, targetPitch, maxStep );
        }
        else if ( moveLength > Internal::kMinMoveLength )
        {
            yaw   = OrientationUtil::turnTowardAngle( yaw, MathUtil::atan2( moveDirection._x, moveDirection._z ), maxStep );
            pitch = OrientationUtil::turnTowardAngle( pitch, 0.0f, maxStep );
        }
        setControlRotation( yaw, pitch );

        // 이동 축은 이번 조종 요 기준이다(시스템이 같은 요를 의도에 넣는다).
        outIntent._controlYaw = yaw;
        outIntent.setWorldMove( moveDirection );
    }

    float3 AiControllerComponent::computeMoveDirection( const PawnComponent& pawn, const float3& pawnPosition )
    {
        GameObject*            pPawnOwner = pawn.getOwner();
        NavMeshAgentComponent* pAgent     = pPawnOwner != nullptr ? pPawnOwner->getComponent<NavMeshAgentComponent>() : nullptr;
        if ( pAgent != nullptr )
            return computeAgentMoveDirection( *pAgent );
        if ( _bHasDestination == SW_FALSE )
            return float3{};
        const float32 deltaX   = _destination._x - pawnPosition._x;
        const float32 deltaZ   = _destination._z - pawnPosition._z;
        const float32 distance = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        if ( distance <= _arriveDistance )
        {
            _bHasDestination = SW_FALSE;
            _moveStatus      = NavMoveStatus::Arrived;
            return float3{};
        }
        return float3{ deltaX / distance, 0.0f, deltaZ / distance };
    }

    float3 AiControllerComponent::computeAgentMoveDirection( NavMeshAgentComponent& agent )
    {
        // 에이전트는 경로 · 군중 회피로 속도만 낸다(SteerOnly) — 그 속도를 이동 축으로 넣고, 움직이는 것은 플레이어와 같은 폰 이동이다.
        if ( agent.getDriveMode() != NavAgentDriveMode::SteerOnly && _bWarnedDriveMode == SW_FALSE )
        {
            _bWarnedDriveMode = SW_TRUE;
            SW_LOG_WARNING( "AI controller '%#' steers a navmesh agent whose drive mode is not SteerOnly - the agent moves the body itself, the controller adds no move intent",
                            getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        }
        if ( _bStopPending == SW_TRUE )
        {
            agent.stop();
            _bStopPending = SW_FALSE;
        }
        if ( _bHasDestination == SW_FALSE )
            return float3{};
        if ( _bDestinationSent == SW_FALSE )
        {
            // 이번 프레임의 내비게이션 단계가 경로를 잡는다 — 처음 걸면 속도는 다음 프레임부터 읽는다. 이미 걷던 중(목적지만 바꿈)이면 지금 속도를 그대로
            // 쓴다 — 쫓는 적이 목적지를 고칠 때마다 한 프레임 서면 걸음이 끊긴다.
            const bool bWasMoving = agent.hasDestination() && agent.getMoveStatus() == NavMoveStatus::Moving;
            agent.setDestination( _destination );
            _bDestinationSent = SW_TRUE;
            if ( bWasMoving == false )
                return float3{};
        }
        const NavMoveStatus status = agent.getMoveStatus();
        if ( status == NavMoveStatus::Arrived || status == NavMoveStatus::Failed )
        {
            _bHasDestination  = SW_FALSE;
            _bDestinationSent = SW_FALSE;
            _moveStatus       = status;
            return float3{};
        }
        if ( agent.getDriveMode() != NavAgentDriveMode::SteerOnly )
            return float3{};
        const float32 maxSpeed = agent.getMaxSpeed();
        const float3& velocity = agent.getVelocity();
        if ( maxSpeed <= 0.0f )
            return float3{};
        float3        direction{ velocity._x / maxSpeed, 0.0f, velocity._z / maxSpeed };
        const float32 length = MathUtil::sqrt( direction._x * direction._x + direction._z * direction._z );
        if ( length > 1.0f )
            direction = float3{ direction._x / length, 0.0f, direction._z / length };
        return direction;
    }

    void AiControllerComponent::moveTo( const float3& destination )
    {
        if ( _bHasDestination == SW_TRUE && _destination == destination )
            return;
        _destination      = destination;
        _bHasDestination  = SW_TRUE;
        _bDestinationSent = SW_FALSE;
        _bStopPending     = SW_FALSE;
        _moveStatus       = NavMoveStatus::Moving;
    }

    void AiControllerComponent::stopMoving()
    {
        if ( _bHasDestination == SW_TRUE )
            _bStopPending = SW_TRUE;
        _bHasDestination  = SW_FALSE;
        _bDestinationSent = SW_FALSE;
        _moveStatus       = NavMoveStatus::Idle;
    }

    void AiControllerComponent::setFocus( const float3& worldPoint )
    {
        _focusPoint = worldPoint;
        _bHasFocus  = SW_TRUE;
    }

    void AiControllerComponent::clearFocus()
    {
        _bHasFocus = SW_FALSE;
    }

    void AiControllerComponent::pressButton( const hashed_string& name )
    {
        const PawnComponent* pPawn = findPawn();
        if ( pPawn != nullptr )
            _pending.setButton( pPawn->findButton( name ), true, true );
    }

    void AiControllerComponent::holdButton( const hashed_string& name, bool bHeld )
    {
        const PawnComponent* pPawn       = findPawn();
        const int32          buttonIndex = pPawn != nullptr ? pPawn->findButton( name ) : -1;
        if ( buttonIndex < 0 || ControlIntent::kButtonCount <= buttonIndex )
            return;
        const uint32 bit = 1u << buttonIndex;
        _heldButtonMask  = bHeld ? ( _heldButtonMask | bit ) : ( _heldButtonMask & ~bit );
    }

    void AiControllerComponent::setAnalog( const hashed_string& name, float32 value )
    {
        const PawnComponent* pPawn       = findPawn();
        const int32          analogIndex = pPawn != nullptr ? pPawn->findAnalog( name ) : -1;
        if ( 0 <= analogIndex && analogIndex < ControlIntent::kAnalogCount )
            _pending._arrAnalog[analogIndex] = MathUtil::clamp( value, -1.0f, 1.0f );
    }

    NavMoveStatus AiControllerComponent::getMoveStatus() const
    {
        return _moveStatus;
    }
} // namespace sw
