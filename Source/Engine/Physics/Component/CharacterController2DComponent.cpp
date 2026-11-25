#include "pch.h"

#include "Engine/Physics/Component/CharacterController2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    CharacterController2DComponent::CharacterController2DComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Character }
        , _radius{ 0.3f }
        , _halfHeight{ 0.4f }
        , _maxSlopeAngle{ Radians::fromRadians( MathUtil::kHalfPi * 0.5f ) }
        , _gravityScale{ 1.0f }
        , _bInnerBody{ true }
        , _verticalSpeed{ 0.0f }
        , _airVelocity{ 0.0f }
        , _movementMode{ CharacterMovementMode::Walking }
        , _bGrounded{ false }
        , _character{}
        , _state{}
        , _pCommandPage{ nullptr }
        , _commandSlot{ PhysicsPoseStore::kInvalidSlot }
        , _previousPosition{}
        , _worldPosition{}
        , _worldRotation{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _frameClimbVelocity{}
        , _bFrameClimb{ SW_FALSE }
        , _reserved{ 0 }
    {
        _commandSlot = engine::getSubsystem<PhysicsPoseStore>().getCommandStore<CharacterCommand2D>().allocateSlot( this, _pCommandPage );
    }

    CharacterController2DComponent::~CharacterController2DComponent()
    {
        engine::getSubsystem<PhysicsPoseStore>().getCommandStore<CharacterCommand2D>().freeSlot( _commandSlot );
    }

    template <typename TFunction>
    void CharacterController2DComponent::editCommand( TFunction&& function ) const
    {
        _pCommandPage->editCommand( _commandSlot & CharacterCommandPage2D::kSlotMask, std::forward<TFunction>( function ) );
    }

    float32 CharacterController2DComponent::getRequestedMoveVelocity() const
    {
        float32 velocity = 0.0f;
        editCommand( [&velocity]( const CharacterCommand2D& command )
        { velocity = command._moveVelocity; } );
        return velocity;
    }

    void CharacterController2DComponent::setMoveVelocity( float32 velocity )
    {
        editCommand( [velocity]( CharacterCommand2D& command )
        { command._moveVelocity = velocity; } );
    }

    void CharacterController2DComponent::jump( float32 speed )
    {
        editCommand( [speed]( CharacterCommand2D& command )
        { command._pendingJumpSpeed = speed; } );
    }

    void CharacterController2DComponent::launch( const float2& velocity )
    {
        editCommand( [&velocity]( CharacterCommand2D& command )
        { command._pendingLaunch = velocity; } );
    }

    void CharacterController2DComponent::applyClimbVelocity( const float2& velocity )
    {
        editCommand( [&velocity]( CharacterCommand2D& command )
        {
            command._pendingClimbVelocity = velocity;
            command._bPendingClimb        = true;
        } );
    }

    void CharacterController2DComponent::setRadius( float32 radius )
    {
        _radius = radius;
        requestRebuild();
    }

    void CharacterController2DComponent::setHalfHeight( float32 halfHeight )
    {
        _halfHeight = halfHeight;
        requestRebuild();
    }

    void CharacterController2DComponent::setMaxSlopeAngle( float32 angle )
    {
        _maxSlopeAngle = Radians::fromRadians( angle );
        requestRebuild();
    }

    void CharacterController2DComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat2( getVelocity() );
        inoutBuilder.addUint8( isGrounded() ? 1 : 0 );
    }

    void CharacterController2DComponent::resetMotion()
    {
        editCommand( []( CharacterCommand2D& command )
        {
            command._bPendingStop     = true;
            command._pendingJumpSpeed = 0.0f;
            command._pendingLaunch    = float2{};
        } );
    }

    void CharacterController2DComponent::onRegister( GameObjectManager& manager )
    {
        PhysicsComponent::onRegister( manager );
        registerFixedStep<&CharacterController2DComponent::onPhysicsFrameBegin>( manager, FixedStepGroup::FrameBegin );
        registerFixedStep<&CharacterController2DComponent::onPrePhysicsStep>( manager, FixedStepGroup::PreStep );
        registerFixedStep<&CharacterController2DComponent::onPhysicsFrameEnd>( manager, FixedStepGroup::FrameEnd );
    }

    void CharacterController2DComponent::onPhysicsFrameBegin( const FixedStepContext& context )
    {
        ScenePhysics& physics = context.getPhysics();
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            // 꺼진 동안의 충격(점프 · 발사 · 오르기)은 버린다 — 켜진 첫 걸음에 몰아 나가지 않는다(3D 와 같다). 시작 전 · 스트리밍이 붙잡은 동안의 것은 첫 걸음의 것이라 남긴다.
            if ( hasBegunPlay() && isActive() == false )
                editCommand( []( CharacterCommand2D& command )
                {
                    CharacterCommand2D kept{};
                    kept._moveVelocity = command._moveVelocity;
                    command            = kept;
                } );
            return;
        }
        IPhysicsScene2D* pScene = physics.getScene2D();
        if ( pScene == nullptr )
            return;
        // 이번 게임 프레임의 오르기 요청을 이 물리 프레임의 것으로 옮긴다 — 부르지 않은 프레임은 오르지 않는다(3D 와 같다).
        float2 climbVelocity{};
        bool   bClimb = false;
        editCommand( [&climbVelocity, &bClimb]( CharacterCommand2D& command )
        {
            climbVelocity          = command._pendingClimbVelocity;
            bClimb                 = command._bPendingClimb;
            command._bPendingClimb = false;
        } );
        _frameClimbVelocity = climbVelocity;
        _bFrameClimb        = bClimb ? SW_TRUE : SW_FALSE;
        if ( consumeRebuild() )
            releasePhysics( physics );

        // 트랜스폼의 바깥 변화는 칸의 변경 번호로 안다 — 같으면 월드 행렬을 풀지 않는다(3D 컨트롤러와 같은 규칙).
        const PhysicsTransformChange change      = consumeTransformChange();
        const bool                   bCreate     = _character.isValid() == false;
        const bool                   bTeleported = consumeTeleport();
        // 배율은 끝 훅(`onPhysicsFrameEnd`)이 먼저 읽었을 수 있다 — 변경 번호만이 아니라 지은 배율과의 값 비교로도 본다(3D 컨트롤러와 같다).
        const bool bUnchanged = bCreate == false && change.hasChanged() == false && bTeleported == false && isShapeScaleStale( _worldScale ) == false;
        if ( bUnchanged )
            return;
        if ( PhysicsComponent::readWorldPose( *this, _worldPosition, _worldRotation, _worldScale ) == false )
        {
            releasePhysics( physics ); // 배율 0 — 캡슐을 지을 수 없다(`readWorldPose` 가 알렸다). 배율이 돌아오면 다시 만든다
            return;
        }
        const float2 position{ _worldPosition._x, _worldPosition._y };
        // 캡슐은 지을 때 배율을 굽는다 — 배율이 바뀌면 캐릭터는 두고 크기만 간다(이동 상태 · 세로 속도가 이어진다 — 3D 컨트롤러와 같다).
        if ( bCreate == false && isShapeScaleStale( _worldScale ) )
        {
            const PhysicsShapeDesc2D capsule = getScaledCapsule();
            if ( pScene->setCharacterSize( _character, capsule._radius, capsule._halfHeight ) == false )
            {
                releasePhysics( physics );
                return;
            }
            markShapeScale( _worldScale );
        }
        if ( bCreate )
        {
            const PhysicsShapeDesc2D capsule = getScaledCapsule();
            PhysicsCharacterDesc2D   desc;
            desc._position      = position;
            desc._userData      = getOwner()->getObjectID();
            desc._radius        = capsule._radius;
            desc._halfHeight    = capsule._halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle.radians();
            desc._layer         = physics.resolveLayer( _layer );
            desc._bInnerBody    = _bInnerBody;
            _character          = pScene->createCharacter( desc );
            if ( _character.isValid() )
                markBodyCreated();
            markShapeScale( _worldScale );
            // 이어받는 운동은 지우지 않는다 — 스냅샷에서 돌아온 값이면 잇고, 앞 캐릭터를 놓은 것이면 놓을 때 지웠다(`releasePhysics`, 3D 와 같다).
            _state            = PhysicsCharacterState2D{};
            _state._bGrounded = _bGrounded;
            _state._position  = position;
            _previousPosition = position;
            return;
        }
        // 깊이(Z) · 회전 · 배율만 바뀌었으면 자리는 시뮬레이션의 것이다 — 그린 자리로 옮기면 걸은 만큼이 되돌아간다(3D 와 같다).
        if ( bTeleported == false && change.hasPlanePosition() == false )
            return;
        pScene->setCharacterPosition( _character, position );
        _state._position  = position;
        _previousPosition = position;
    }

    void CharacterController2DComponent::onPrePhysicsStep( const FixedStepContext& context )
    {
        ScenePhysics&    physics        = context.getPhysics();
        const float32    fixedDeltaTime = context.getFixedDeltaTime();
        IPhysicsScene2D* pScene         = physics.findScene2D();
        if ( pScene == nullptr || _character.isValid() == false )
            return;
        float32 moveVelocity = 0.0f;
        float32 jumpSpeed    = 0.0f;
        float2  launch{};
        bool    bStop = false;
        editCommand( [&moveVelocity, &jumpSpeed, &launch, &bStop]( CharacterCommand2D& command )
        {
            bStop                     = command._bPendingStop;
            command._bPendingStop     = false;
            moveVelocity              = command._moveVelocity;
            jumpSpeed                 = command._pendingJumpSpeed;
            launch                    = command._pendingLaunch;
            command._pendingJumpSpeed = 0.0f;
            command._pendingLaunch    = float2{};
        } );
        if ( bStop )
        {
            _verticalSpeed = 0.0f;
            _airVelocity   = 0.0f;
        }
        const bool bLaunched = launch.getLengthSquared() > 0.0f;
        if ( _state._bGrounded && _verticalSpeed < 0.0f )
            _verticalSpeed = 0.0f;
        _verticalSpeed += pScene->getGravity()._y * _gravityScale * fixedDeltaTime;
        if ( bLaunched )
        {
            _verticalSpeed = launch._y;
            _airVelocity   = launch._x;
        }
        else if ( jumpSpeed > 0.0f )
            _verticalSpeed = jumpSpeed;
        // 오르기: 중력 · 공중 관성 없이 이동기가 준 속도만 쓴다. 이 스텝의 점프 · 발사는 매달림을 놓는 것이라 이긴다(3D 와 같다).
        const bool bClimb = _bFrameClimb == SW_TRUE && jumpSpeed <= 0.0f && bLaunched == false;
        if ( bClimb )
        {
            _airVelocity   = 0.0f;
            moveVelocity   = _frameClimbVelocity._x;
            _verticalSpeed = _frameClimbVelocity._y;
        }

        _previousPosition                   = _state._position;
        _state                              = pScene->moveCharacter( _character, float2{ moveVelocity + _airVelocity, _verticalSpeed }, fixedDeltaTime );
        _bGrounded                          = _state._bGrounded;
        const CharacterMovementMode walking = _state._bGrounded ? CharacterMovementMode::Walking : CharacterMovementMode::Falling;
        _movementMode                       = bClimb ? CharacterMovementMode::Climbing : walking;
        if ( bClimb )
            _verticalSpeed = 0.0f; // 오르던 속도는 잇지 않는다
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
        {
            _verticalSpeed = 0.0f;
            _airVelocity   = 0.0f;
        }
        else if ( _state.isRiseBlocked( _verticalSpeed ) )
            _verticalSpeed = _state._velocity._y;
        // 결정: 바닥을 떠나는 스텝에 바닥이 실어 나르던 속도를 관성으로 잇는다(3D `CharacterControllerComponent` 와 같다 — 천장 판정 뒤에 둔다).
        const bool bLeftGround = _state._bGrounded == false && _state._carriedVelocity.getLengthSquared() > 0.0f;
        if ( bLeftGround )
        {
            _airVelocity += _state._carriedVelocity._x;
            if ( _state._carriedVelocity._y > 0.0f )
                _verticalSpeed += _state._carriedVelocity._y;
        }
    }

    void CharacterController2DComponent::onPhysicsFrameEnd( const FixedStepContext& context )
    {
        ScenePhysics& physics = context.getPhysics();
        const float32 alpha   = context.getAlpha();
        if ( _character.isValid() == false )
            return;
        const float2 position = float2::lerp( _previousPosition, _state._position, alpha );
        // Z · 회전 · 배율은 트랜스폼의 것이다. 이 프레임 시작 뒤에 누가 옮겼을 때만 다시 읽는다.
        if ( consumeTransformChange().hasChanged() && PhysicsComponent::readWorldPose( *this, _worldPosition, _worldRotation, _worldScale ) == false )
            return; // 배율 0 — 트랜스폼을 덮어쓰지 않는다(다음 프레임 시작 훅이 캐릭터를 놓는다)
        // 강체와 같이 모아 한 번에 쓴다 — 적용 · 변경 번호 맞추기는 `ScenePhysics::queuePoseWrite` 가 한다.
        physics.queuePoseWrite( *this, float3{ position._x, position._y, _worldPosition._z }, _worldRotation, _worldScale );
    }

    PhysicsShapeDesc2D CharacterController2DComponent::getScaledCapsule() const
    {
        PhysicsShapeDesc2D capsule;
        capsule._type       = PhysicsShapeType2D::Capsule;
        capsule._radius     = _radius;
        capsule._halfHeight = _halfHeight;
        return PhysicsComponent::makeScaledShape( capsule, _worldScale );
    }

    void CharacterController2DComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _character.isValid() == false )
            return;
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene != nullptr )
            pScene->destroyCharacter( _character );
        _character     = PhysicsCharacterHandle{};
        _verticalSpeed = 0.0f;
        _airVelocity   = 0.0f;
        _bGrounded     = false;
    }
} // namespace sw

namespace sw
{
    void CharacterController2DComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }
} // namespace sw
