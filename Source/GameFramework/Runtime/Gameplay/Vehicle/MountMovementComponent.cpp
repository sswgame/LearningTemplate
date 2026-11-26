#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
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
        , _netPosition{}
        , _bGallopAllowed{ SW_TRUE }
        , _bNetDriven{ SW_FALSE }
        , _reserved{ 0 }
    {
        _inertia._mass                   = 500.0f;
        _inertia._rollingResistanceScale = 0.0f; // 표면은 걷는 이동 계수가 이미 줄인다
        setCanEverTick( true );
    }

    void MountMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
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

    static_assert( static_cast<uint8>( MountGait::Gallop ) == 4, "MountMoveState 의 걸음새 Max 를 열거의 끝 값과 맞춘다" );

    void MountMovementComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bNetDriven == SW_TRUE )
            return;
        GameObject*          pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr )
            return;
        MountMoveState state;
        state._facingYaw      = _facingYaw;
        state._forwardSpeed   = _forwardSpeed;
        state._maxGait        = static_cast<uint8>( _maxGait );
        state._bGallopAllowed = _bGallopAllowed;
        // 디딘 면이 진행 방향으로 오르면 법선이 뒤로 눕는다(오르막 사인 = −법선·진행). 진행 방향은 이번 걸음이 돌린 요라 조향을 먼저 한 번 구한다. 디딘 데가 없으면 경사 없음.
        CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>();
        float32                       turn        = 0.0f;
        float32                       slopeSin    = 0.0f;
        if ( pController != nullptr && pController->isGrounded() )
        {
            MountMoveState turned = state;
            advance( turned, pPawn->getIntent(), 0.0f, context.getDeltaTime().count(), turn );
            slopeSin = -pController->getGroundNormal().dot( float3{ MathUtil::sin( turned._facingYaw ), 0.0f, MathUtil::cos( turned._facingYaw ) } );
        }
        advance( state, pPawn->getIntent(), slopeSin, context.getDeltaTime().count(), turn );
        _gait         = state.getGait();
        _facingYaw    = state._facingYaw;
        _forwardSpeed = state._forwardSpeed;

        // 자기 요 방향으로 간다. 요가 0 이면 +Z 쪽이고 +X 쪽으로 돈다. 루트 모션이면 애니메이션이 옮긴다.
        const float3    velocity          = float3{ MathUtil::sin( _facingYaw ), 0.0f, MathUtil::cos( _facingYaw ) } * _forwardSpeed;
        SceneComponent* pRoot             = pOwner->getPrimarySceneComponent();
        const bool      bMoveRootDirectly = pRoot != nullptr && _bUseRootMotion == false && _forwardSpeed != 0.0f;
        if ( pController != nullptr )
            pController->setMoveVelocity( _bUseRootMotion ? float3{} : velocity );
        else if ( bMoveRootDirectly )
            pRoot->setWorldPosition( pRoot->getWorldPosition() + velocity * context.getDeltaTime().count() );
        applyAnimatorParameters( turn );
        if ( pRoot != nullptr && turn != 0.0f )
        {
            float3 rotation = pRoot->getLocalRotation();
            rotation._y     = _facingYaw;
            pRoot->setLocalRotation( rotation );
        }
    }

    void MountMovementComponent::advance( MountMoveState& inoutState, const ControlIntent& intent, float32 slopeSin, float32 deltaTime, float32& outTurn ) const
    {
        using Internal           = MountMovementComponentInternal;
        const float3  worldMove  = intent.computeWorldMove();
        const float32 moveAmount = MathUtil::min( 1.0f, MathUtil::sqrt( worldMove._x * worldMove._x + worldMove._z * worldMove._z ) );

        // 걸음새: 요청에 질주 허용과 상한을 적용한다.
        MountGait gait = computeRequestedGait( moveAmount, intent.isDown( _sprintIndex ) );
        if ( gait == MountGait::Gallop && inoutState._bGallopAllowed == SW_FALSE )
            gait = MountGait::Canter;
        if ( static_cast<uint8>( gait ) > inoutState._maxGait )
            gait = static_cast<MountGait>( inoutState._maxGait );
        inoutState._gait = static_cast<uint8>( gait );

        // 의도의 월드 방향으로 몸을 돌린다. 걸음새마다 조향 속도가 다르다. 서 있으면 돌지 않는다.
        outTurn = 0.0f;
        if ( moveAmount > Internal::kMoveDeadzone )
        {
            const float32 wantYaw = MathUtil::atan2( worldMove._x, worldMove._z );
            const float32 newYaw  = OrientationUtil::turnTowardAngle( inoutState._facingYaw, wantYaw, computeGaitTurnRate( gait ) * deltaTime );
            outTurn               = MathUtil::clamp( MathUtil::wrapAngle( wantYaw - inoutState._facingYaw ), -1.0f, 1.0f );
            inoutState._facingYaw = MathUtil::wrapAngle( newYaw );
        }
        // 바깥 힘(경사 중력 · 공기 저항)을 먼저 더하고 걸음새 속도로 다가간다 — 다리가 미는 힘(가속 · 감속)이 당김보다 크면 걸음새를 지키고, 서 있는 말이 비탈에서 밀려 내려가지 않는다.
        inoutState._forwardSpeed += _inertia.computeAlongAcceleration( inoutState._forwardSpeed, slopeSin, 0.0f, deltaTime ) * deltaTime;
        const float32 targetSpeed = computeGaitSpeed( gait );
        // 다리가 미는 가속은 출력 한계(일률)로 자른다 — 출력이 모자라면 가파른 오르막에서 걸음새 속도를 잃는다.
        const float32 rate       = targetSpeed > inoutState._forwardSpeed ? _inertia.limitDriveAcceleration( _acceleration, inoutState._forwardSpeed ) : _deceleration;
        inoutState._forwardSpeed = MathUtil::moveToward( inoutState._forwardSpeed, targetSpeed, rate * deltaTime );
    }

    void MountMovementComponent::applyAnimatorParameters( float32 turn ) const
    {
        const GameObject*          pOwner    = getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator == nullptr )
            return;
        if ( _gaitParameter.empty() == false )
            pAnimator->getParameters().setFloat( _gaitParameter, static_cast<float32>( static_cast<uint8>( _gait ) ) );
        if ( _turnParameter.empty() == false )
            pAnimator->getParameters().setFloat( _turnParameter, turn );
    }

    void MountMovementComponent::onWorldOriginShifted( const float3& delta )
    {
        _netPosition += delta;
    }

    bool MountMovementComponent::setNetDriven( bool bNetDriven )
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( bNetDriven && ( pRoot == nullptr || _bUseRootMotion || pOwner->getComponent<CharacterControllerComponent>() != nullptr ) )
        {
            SW_LOG_ERROR_ID( "MountMovement.NotPredictable", "Mount '%#' cannot be driven by the network tick - it needs a root and no character controller or root motion",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return false;
        }
        if ( bNetDriven && _bNetDriven == SW_FALSE )
            _netPosition = pRoot->getWorldPosition();
        _bNetDriven = bNetDriven ? SW_TRUE : SW_FALSE;
        return true;
    }

    MountMoveState MountMovementComponent::getState() const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        MountMoveState        state;
        if ( _bNetDriven == SW_TRUE )
            state._position = _netPosition;
        else if ( pRoot != nullptr )
            state._position = pRoot->getWorldPosition();
        state._facingYaw      = MathUtil::wrapAngle( _facingYaw );
        state._forwardSpeed   = _forwardSpeed;
        state._gait           = static_cast<uint8>( _gait );
        state._maxGait        = static_cast<uint8>( _maxGait );
        state._bGallopAllowed = _bGallopAllowed;
        return state;
    }

    void MountMovementComponent::setState( const MountMoveState& state, const float3& displayPosition )
    {
        const float32   turn   = MathUtil::clamp( MathUtil::wrapAngle( state._facingYaw - _facingYaw ), -1.0f, 1.0f );
        GameObject*     pOwner = getOwner();
        SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        _netPosition           = state._position;
        _facingYaw             = state._facingYaw;
        _forwardSpeed          = state._forwardSpeed;
        _gait                  = state.getGait();
        _maxGait               = static_cast<MountGait>( state._maxGait );
        _bGallopAllowed        = state._bGallopAllowed;
        if ( pRoot == nullptr )
            return;
        pRoot->setWorldPosition( displayPosition );
        float3 rotation = pRoot->getLocalRotation();
        rotation._y     = _facingYaw;
        pRoot->setLocalRotation( rotation );
        applyAnimatorParameters( turn );
    }

    MountMoveState MountMovementComponent::simulate( const MountMoveState& state, const ControlIntent& intent, float32 deltaTime ) const
    {
        MountMoveState next = state;
        float32        turn = 0.0f;
        advance( next, intent, 0.0f, deltaTime, turn ); // 컨트롤러 없는 탈것이라 디딘 면을 모른다(경사 없음) — 틱과 같다
        next._position += float3{ MathUtil::sin( next._facingYaw ), 0.0f, MathUtil::cos( next._facingYaw ) } * ( next._forwardSpeed * deltaTime );
        return next;
    }
} // namespace sw
