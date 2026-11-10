#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        struct MountMovementComponentInternal
        {
            /** @brief 이동 크기가 이 값 안이면 서 있는 것으로 봅니다. 스틱 떨림을 무시하기 위한 값입니다. */
            static constexpr float32 kMoveDeadzone = 0.05f;
            /** @brief 이동 크기가 이 값 아래이면 걷기이고, 이 값 위이면 속보입니다. */
            static constexpr float32 kTrotThreshold = 0.5f;
            /** @brief 이동 크기가 이 값 위이면 구보입니다. */
            static constexpr float32 kCanterThreshold = 0.95f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    MountMovementComponent::MountMovementComponent()
        : _sprintButton{}
        , _gaitParameter{}
        , _turnParameter{}
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
        , _inertia{}
        , _forwardSpeed{ 0.0f }
        , _facingYaw{ 0.0f }
        , _sprintIndex{}
        , _gait{ MountGait::Idle }
        , _maxGait{ MountGait::Gallop }
        , _bGallopAllowed{ SW_TRUE }
        , _reserved{ 0 }
    {
        _inertia._mass                   = 500.0f;
        _inertia._rollingResistanceScale = 0.0f; // 표면은 걷는 이동 계수가 이미 줄인다
        setCanEverTick( true );
    }

    void MountMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        const GameObject*    pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _sprintIndex                = pPawn != nullptr && _sprintButton.empty() == false ? pPawn->findButton( _sprintButton ) : nullopt;
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

        // 걸음새: 요청에 질주 허용과 상한을 적용한다.
        MountGait gait = computeRequestedGait( moveAmount, intent.isDown( _sprintIndex ) );
        if ( gait == MountGait::Gallop && _bGallopAllowed == SW_FALSE )
            gait = MountGait::Canter;
        if ( static_cast<uint8>( gait ) > static_cast<uint8>( _maxGait ) )
            gait = _maxGait;
        _gait = gait;

        // 의도의 월드 방향으로 몸을 돌린다. 걸음새마다 조향 속도가 다르다. 서 있으면 돌지 않는다.
        float32 turn = 0.0f;
        if ( moveAmount > Internal::kMoveDeadzone )
        {
            const float32 wantYaw = MathUtil::atan2( worldMove._x, worldMove._z );
            const float32 newYaw  = OrientationUtil::turnTowardAngle( _facingYaw, wantYaw, computeGaitTurnRate( gait ) * deltaTime );
            turn                  = MathUtil::clamp( MathUtil::wrapAngle( wantYaw - _facingYaw ), -1.0f, 1.0f );
            _facingYaw            = MathUtil::wrapAngle( newYaw );
        }
        // 바깥 힘(경사 중력 · 공기 저항)을 먼저 더하고 걸음새 속도로 다가간다 — 다리가 미는 힘(가속 · 감속)이 당김보다 크면 걸음새를 지키고, 서 있는 말이 비탈에서 밀려 내려가지 않는다.
        // 디딘 면이 진행 방향으로 오르면 법선이 뒤로 눕는다(오르막 사인 = −법선·진행). 디딘 데가 없으면 경사 없음.
        const float3                  facing{ MathUtil::sin( _facingYaw ), 0.0f, MathUtil::cos( _facingYaw ) };
        CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>();
        const bool                    bGrounded   = pController != nullptr && pController->isGrounded();
        const float32                 slopeSin    = bGrounded ? -pController->getGroundNormal().dot( facing ) : 0.0f;
        _forwardSpeed += _inertia.computeAlongAcceleration( _forwardSpeed, slopeSin, 0.0f, deltaTime ) * deltaTime;
        const float32 targetSpeed = computeGaitSpeed( gait );
        // 다리가 미는 가속은 출력 한계(일률)로 자른다 — 출력이 모자라면 가파른 오르막에서 걸음새 속도를 잃는다.
        const float32 rate = targetSpeed > _forwardSpeed ? _inertia.limitDriveAcceleration( _acceleration, _forwardSpeed ) : _deceleration;
        _forwardSpeed      = MathUtil::moveToward( _forwardSpeed, targetSpeed, rate * deltaTime );

        // 자기 요 방향으로 간다. 요가 0 이면 +Z 쪽이고 +X 쪽으로 돈다. 루트 모션이면 애니메이션이 옮긴다.
        const float3    velocity          = facing * _forwardSpeed;
        SceneComponent* pRoot             = pOwner->getPrimarySceneComponent();
        const bool      bMoveRootDirectly = pRoot != nullptr && _bUseRootMotion == false && _forwardSpeed != 0.0f;
        if ( pController != nullptr )
            pController->setMoveVelocity( _bUseRootMotion ? float3{} : velocity );
        else if ( bMoveRootDirectly )
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
