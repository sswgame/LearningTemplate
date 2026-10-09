#include "pch.h"

#include "GameFramework/Base/Gameplay/Vehicle/MountMovementComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
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
        struct MountMovementComponentInternal
        {
            /** @brief 이동 크기가 이 안이면 서 있다(스틱 떨림). */
            static constexpr float32 kMoveDeadzone = 0.05f;
            /** @brief 이동 크기가 이 아래면 걷기, 그 위는 속보입니다. */
            static constexpr float32 kTrotThreshold = 0.5f;
            /** @brief 이동 크기가 이 위면 구보입니다. */
            static constexpr float32 kCanterThreshold = 0.95f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    MountMovementComponent::MountMovementComponent()
        : _sprintButton{ "Sprint" }
        , _gaitParameter{ "Gait" }
        , _turnParameter{ "Turn" }
        , _walkSpeed{ 1.8f }
        , _trotSpeed{ 4.0f }
        , _canterSpeed{ 7.0f }
        , _gallopSpeed{ 11.0f }
        , _acceleration{ 6.0f }
        , _deceleration{ 9.0f }
        , _idleTurnRate{ 1.5f }
        , _walkTurnRate{ 2.0f }
        , _trotTurnRate{ 1.8f }
        , _canterTurnRate{ 1.4f }
        , _gallopTurnRate{ 1.0f }
        , _bUseRootMotion{ false }
        , _forwardSpeed{ 0.0f }
        , _facingYaw{ 0.0f }
        , _sprintIndex{ -1 }
        , _gait{ MountGait::Idle }
        , _maxGait{ MountGait::Gallop }
        , _bGallopAllowed{ SW_TRUE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void MountMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _sprintIndex                = pPawn != nullptr && _sprintButton.empty() == false ? pPawn->findButton( _sprintButton ) : -1;
        const SceneComponent* pRoot = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pRoot != nullptr )
            _facingYaw = pRoot->getLocalRotation()._y;
    }

    MountGait MountMovementComponent::computeRequestedGait( float32 moveAmount, bool bSprint )
    {
        using Internal = MountMovementComponentInternal;
        if ( moveAmount <= Internal::kMoveDeadzone )
            return MountGait::Idle;
        if ( bSprint && moveAmount >= Internal::kTrotThreshold )
            return MountGait::Gallop;
        if ( moveAmount < Internal::kTrotThreshold )
            return MountGait::Walk;
        return moveAmount < Internal::kCanterThreshold ? MountGait::Trot : MountGait::Canter;
    }

    float32 MountMovementComponent::computeGaitSpeed( MountGait gait ) const
    {
        switch ( gait )
        {
            case MountGait::Idle:
                return 0.0f;
            case MountGait::Walk:
                return _walkSpeed;
            case MountGait::Trot:
                return _trotSpeed;
            case MountGait::Canter:
                return _canterSpeed;
            case MountGait::Gallop:
                return _gallopSpeed;
        }
        return 0.0f;
    }

    float32 MountMovementComponent::computeGaitTurnRate( MountGait gait ) const
    {
        switch ( gait )
        {
            case MountGait::Idle:
                return _idleTurnRate;
            case MountGait::Walk:
                return _walkTurnRate;
            case MountGait::Trot:
                return _trotTurnRate;
            case MountGait::Canter:
                return _canterTurnRate;
            case MountGait::Gallop:
                return _gallopTurnRate;
        }
        return 0.0f;
    }

    void MountMovementComponent::onTick( float32 deltaTime )
    {
        using Internal = MountMovementComponentInternal;
        Component::onTick( deltaTime );
        GameObject*          pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr )
            return;
        const ControlIntent& intent     = pPawn->getIntent();
        const float3         worldMove  = intent.computeWorldMove();
        const float32        moveAmount = MathUtil::min( 1.0f, MathUtil::sqrt( worldMove._x * worldMove._x + worldMove._z * worldMove._z ) );

        // 걸음새 — 요청에 질주 허용 · 상한을 씌운다.
        MountGait gait = computeRequestedGait( moveAmount, intent.isDown( _sprintIndex ) );
        if ( gait == MountGait::Gallop && _bGallopAllowed == SW_FALSE )
            gait = MountGait::Canter;
        if ( static_cast<uint8>( gait ) > static_cast<uint8>( _maxGait ) )
            gait = _maxGait;
        _gait = gait;

        // 의도의 월드 방향으로 몸을 돌린다(걸음새마다의 조향 속도). 서 있으면 돌지 않는다.
        float32 turn = 0.0f;
        if ( moveAmount > Internal::kMoveDeadzone )
        {
            const float32 wantYaw = MathUtil::atan2( worldMove._x, worldMove._z );
            const float32 newYaw  = OrientationUtil::turnTowardAngle( _facingYaw, wantYaw, computeGaitTurnRate( gait ) * deltaTime );
            turn                  = MathUtil::clamp( MathUtil::wrapAngle( wantYaw - _facingYaw ), -1.0f, 1.0f );
            _facingYaw            = MathUtil::wrapAngle( newYaw );
        }
        const float32 targetSpeed = computeGaitSpeed( gait );
        const float32 rate        = targetSpeed > _forwardSpeed ? _acceleration : _deceleration;
        _forwardSpeed             = MathUtil::moveToward( _forwardSpeed, targetSpeed, rate * deltaTime );

        // 자기 요 방향으로 간다(+Z 에서 +X 쪽). 루트 모션이면 애니메이션이 옮긴다.
        const float3                  velocity{ MathUtil::sin( _facingYaw ) * _forwardSpeed, 0.0f, MathUtil::cos( _facingYaw ) * _forwardSpeed };
        CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>();
        SceneComponent*               pRoot       = pOwner->getPrimarySceneComponent();
        if ( pController != nullptr )
            pController->setMoveVelocity( _bUseRootMotion ? float3{} : velocity );
        else if ( pRoot != nullptr && _bUseRootMotion == false && _forwardSpeed != 0.0f )
            pRoot->setWorldPosition( pRoot->getWorldPosition() + velocity * deltaTime );
        SkeletalAnimatorComponent* pAnimator = pOwner->getComponent<SkeletalAnimatorComponent>();
        if ( pAnimator != nullptr )
        {
            if ( _gaitParameter.empty() == false )
                pAnimator->getParameters().setFloat( _gaitParameter, static_cast<float32>( static_cast<uint8>( gait ) ) );
            if ( _turnParameter.empty() == false )
                pAnimator->getParameters().setFloat( _turnParameter, turn );
        }
        if ( pRoot != nullptr && turn != 0.0f )
        {
            float3 rotation = pRoot->getLocalRotation();
            rotation._y     = _facingYaw;
            pRoot->setLocalRotation( rotation );
        }
    }
} // namespace sw
