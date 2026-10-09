#include "pch.h"

#include "GameFramework/Base/Actor/Control/Pawn/CharacterPawnMovementComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"

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
    CharacterPawnMovementComponent::CharacterPawnMovementComponent()
        : _sprintButton{}
        , _jumpButton{}
        , _walkSpeed{ 4.0f }
        , _sprintSpeed{ 7.0f }
        , _acceleration{ 30.0f }
        , _deceleration{ 40.0f }
        , _jumpSpeed{ 5.0f }
        , _turnRate{ 12.0f }
        , _facingMode{ PawnFacingMode::MoveDirection }
        , _velocity{}
        , _facingYaw{ 0.0f }
        , _sprintIndex{ -1 }
        , _jumpIndex{ -1 }
        , _bSuspended{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void CharacterPawnMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        GameObject*          pOwner  = getOwner();
        const PawnComponent* pPawn   = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _sprintIndex                 = pPawn != nullptr && _sprintButton.empty() == false ? pPawn->findButton( _sprintButton ) : -1;
        _jumpIndex                   = pPawn != nullptr && _jumpButton.empty() == false ? pPawn->findButton( _jumpButton ) : -1;
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr )
            _facingYaw = pScene->getLocalRotation()._y;
    }

    void CharacterPawnMovementComponent::onTick( float32 deltaTime )
    {
        using Internal = CharacterPawnMovementComponentInternal;
        Component::onTick( deltaTime );
        if ( _bSuspended == SW_TRUE )
            return;
        GameObject*                   pOwner      = getOwner();
        const PawnComponent*          pPawn       = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pPawn == nullptr || pController == nullptr )
            return;
        const ControlIntent& intent = pPawn->getIntent();

        // 목표 수평 속도 — 의도의 월드 이동(길이 1 이하) × 걸음 · 달리기 속도.
        const float3  worldMove = intent.computeWorldMove();
        const float32 speed     = intent.isDown( _sprintIndex ) ? _sprintSpeed : _walkSpeed;
        const float3  target{ worldMove._x * speed, 0.0f, worldMove._z * speed };
        const bool    bHasTarget = target._x != 0.0f || target._z != 0.0f;
        _velocity                = Internal::moveTowards( _velocity, target, ( bHasTarget ? _acceleration : _deceleration ) * deltaTime );
        pController->setMoveVelocity( _velocity );
        if ( intent.wasTriggered( _jumpIndex ) && pController->isGrounded() )
            pController->jump( _jumpSpeed );

        // 몸 방향 — 조종 요, 또는 움직이는 쪽.
        float32       targetYaw   = _facingYaw;
        const float32 movingSpeed = MathUtil::sqrt( _velocity._x * _velocity._x + _velocity._z * _velocity._z );
        if ( _facingMode == PawnFacingMode::ControlYaw )
            targetYaw = intent._controlYaw;
        else if ( movingSpeed > Internal::kMinFacingSpeed )
            targetYaw = MathUtil::atan2( _velocity._x, _velocity._z );
        const float32 newYaw = OrientationUtil::turnTowardAngle( _facingYaw, targetYaw, _turnRate * deltaTime );
        if ( MathUtil::abs( MathUtil::wrapAngle( newYaw - _facingYaw ) ) <= Internal::kMinYawChange )
            return;
        _facingYaw             = newYaw;
        SceneComponent* pScene = pOwner->getPrimarySceneComponent();
        if ( pScene != nullptr )
        {
            float3 rotation = pScene->getLocalRotation();
            rotation._y     = _facingYaw;
            pScene->setLocalRotation( rotation );
        }
    }

    void CharacterPawnMovementComponent::setSuspended( bool bSuspended )
    {
        const uint8 bNewSuspended = bSuspended ? SW_TRUE : SW_FALSE;
        if ( _bSuspended == bNewSuspended )
            return;
        _bSuspended = bNewSuspended;
        if ( bSuspended == false )
            return;
        _velocity                                 = float3{};
        GameObject*                   pOwner      = getOwner();
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
            pController->setMoveVelocity( float3{} );
    }
} // namespace sw
