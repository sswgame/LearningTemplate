#include "pch.h"

#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "Core/Concurrency/atomic.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
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
    CharacterControllerComponent::CharacterControllerComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Character }
        , _radius{ 0.3f }
        , _halfHeight{ 0.6f }
        , _stepHeight{ 0.3f }
        , _maxSlopeAngle{ Radians::fromRadians( MathUtil::kHalfPi * 0.5f ) }
        , _gravityScale{ 1.0f }
        , _mass{ 70.0f }
        , _bInnerBody{ true }
        , _walkVelocity{}
        , _fieldVelocity{}
        , _airVelocity{}
        , _verticalSpeed{ 0.0f }
        , _movementMode{ CharacterMovementMode::Walking }
        , _bGrounded{ false }
        , _character{}
        , _state{}
        , _pCommandPage{ nullptr }
        , _commandSlot{ PhysicsPoseStore::kInvalidSlot }
        , _frameRootMotionVelocity{}
        , _frameSurfaceVelocity{}
        , _frameMoveScale{ 1.0f }
        , _reportedMoveScale{ 1.0f }
        , _frameField{}
        , _frameClimbVelocity{}
        , _queuedStepVelocity{}
        , _queuedStepSeconds{ 0.0f }
        , _previousPosition{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _worldRotation{}
        , _pendingGroundYaw{ 0.0f }
        , _requestedHeightReduction{ 0.0f }
        , _appliedHeightReduction{ 0.0f }
        , _listStepHistory{}
        , _stepHistoryCount{ 0 }
        , _motionDisplayOffset{}
        , _motionDisplayAlpha{ 1.0f }
        , _bFrameClimb{ SW_FALSE }
        , _bStepClimb{ SW_FALSE }
        , _bManualSimulation{ SW_FALSE }
        , _reserved{ 0 }
        , _suspendCount{ 0 }
        , _nextSuspensionID{ 1 }
        , _suspensionSubscriptions{ SW_DELEGATE_METHOD( SubscriptionList::ReleaseFunction, &CharacterControllerComponent::releaseSuspension, this ) }
    {
        _commandSlot = engine::getSubsystem<PhysicsPoseStore>().getCommandStore<CharacterCommand3D>().allocateSlot( this, _pCommandPage );
    }

    CharacterControllerComponent::~CharacterControllerComponent()
    {
        engine::getSubsystem<PhysicsPoseStore>().getCommandStore<CharacterCommand3D>().freeSlot( _commandSlot );
    }

    template <typename TFunction>
    void CharacterControllerComponent::editCommand( TFunction&& function ) const
    {
        _pCommandPage->editCommand( _commandSlot & CharacterCommandPage3D::kSlotMask, std::forward<TFunction>( function ) );
    }

    /** @brief 모든 캐릭터의 스텝 기록 스위치입니다(물리 워커가 읽는다 — 물리가 도는 동안 바꾸지 않는다). 켬 스위치는 전역 변수다 — 자동화 단계가 시작 값으로 되돌린다. */
    SW_GLOBAL_VARIABLE( bool, gv_movementHistory, false, "캐릭터 이동 스텝을 기록한다 (개발 명령 movement.history 와 같다)" );

    namespace
    {
        struct CharacterControllerHistoryInternal
        {
            /** @brief `movement.history on|off` — 캐릭터 이동 디버거(HUD 섹션 `movement`)의 스텝 기록. */
            static bool runHistory( const vector<string>& listArgument, string& outReply )
            {
                bool bEnabled = false;
                if ( listArgument.size() != 1 || StringUtil::tryParseBool( listArgument[0], bEnabled ) == false )
                    return false;
                CharacterControllerComponent::setStepHistoryEnabled( bEnabled );
                outReply = bEnabled ? "movement.history on" : "movement.history off";
                return true;
            }
        };
    } // namespace

    void CharacterControllerComponent::setStepHistoryEnabled( bool bEnabled )
    {
        gv_movementHistory = bEnabled;
    }

    bool CharacterControllerComponent::isStepHistoryEnabled()
    {
        return gv_movementHistory;
    }

    float3 CharacterControllerComponent::getRequestedMoveVelocity() const
    {
        float3 velocity{};
        editCommand( [&velocity]( const CharacterCommand3D& command )
        { velocity = command._moveVelocity; } );
        return velocity;
    }

    void CharacterControllerComponent::collectStepHistory( vector<StepRecord>& outListRecord ) const
    {
        outListRecord.clear();
        if ( _listStepHistory.empty() )
            return;
        const uint32 count = _stepHistoryCount < kStepHistoryCapacity ? _stepHistoryCount : kStepHistoryCapacity;
        for ( uint32 index = _stepHistoryCount - count; index < _stepHistoryCount; ++index )
        {
            outListRecord.push_back( _listStepHistory[index % kStepHistoryCapacity] );
        }
    }

    float3 CharacterControllerComponent::getRequestedStepVelocity() const
    {
        _physicsStep.assumeHeld(); // 게임 스레드, 물리 스텝 밖 — 스텝 훅과 겹치지 않는다
        return _queuedStepVelocity;
    }

    void CharacterControllerComponent::setMoveVelocity( const float3& velocity )
    {
        editCommand( [&velocity]( CharacterCommand3D& command )
        { command._moveVelocity = float3{ velocity._x, 0.0f, velocity._z }; } );
    }

    void CharacterControllerComponent::setVerticalMoveSpeed( float32 speed )
    {
        editCommand( [speed]( CharacterCommand3D& command )
        { command._verticalMoveSpeed = speed; } );
    }

    void CharacterControllerComponent::applyClimbVelocity( const float3& velocity )
    {
        editCommand( [&velocity]( CharacterCommand3D& command )
        {
            command._pendingClimbVelocity = velocity;
            command._bPendingClimb        = true;
        } );
    }

    void CharacterControllerComponent::jump( float32 speed )
    {
        editCommand( [speed]( CharacterCommand3D& command )
        { command._pendingJumpSpeed = speed; } );
    }

    void CharacterControllerComponent::addRootMotionDisplacement( const float3& worldDisplacement )
    {
        editCommand( [&worldDisplacement]( CharacterCommand3D& command )
        { command._pendingRootMotion += float3{ worldDisplacement._x, 0.0f, worldDisplacement._z }; } );
    }

    void CharacterControllerComponent::launch( const float3& velocity )
    {
        editCommand( [&velocity]( CharacterCommand3D& command )
        { command._pendingLaunch = velocity; } );
    }

    void CharacterControllerComponent::stopMovementImmediately()
    {
        // 운동 상태(`_walkVelocity` · `_airVelocity` …)는 물리 스텝만 만진다 — 대기로 넘기고, 그 전에 쌓인 충격 대기는 여기서 지운다.
        editCommand( []( CharacterCommand3D& command )
        {
            command._bPendingStop      = true;
            command._pendingJumpSpeed  = 0.0f;
            command._pendingLaunch     = float3{};
            command._pendingRootMotion = float3{};
        } );
    }

    Subscription CharacterControllerComponent::suspend()
    {
        _suspendCount.fetch_add( 1, std::memory_order_relaxed );
        return _suspensionSubscriptions.issue( _nextSuspensionID++ );
    }

    void CharacterControllerComponent::releaseSuspension( uint64 /*suspensionID*/ )
    {
        _suspendCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    void CharacterControllerComponent::discardPendingCommands()
    {
        editCommand( []( CharacterCommand3D& command )
        {
            CharacterCommand3D kept{};
            kept._moveVelocity      = command._moveVelocity;
            kept._verticalMoveSpeed = command._verticalMoveSpeed;
            command                 = kept;
        } );
    }

    void CharacterControllerComponent::addSurfaceVelocity( const float3& velocity )
    {
        editCommand( [&velocity]( CharacterCommand3D& command )
        { command._pendingSurfaceVelocity += float3{ velocity._x, 0.0f, velocity._z }; } );
    }

    void CharacterControllerComponent::applyMoveScale( float32 scale )
    {
        editCommand( [scale]( CharacterCommand3D& command )
        { command._pendingMoveScale = MathUtil::min( command._pendingMoveScale, MathUtil::max( scale, 0.0f ) ); } );
    }

    void CharacterControllerComponent::applyPhysicsField( const PhysicsFieldSample& sample )
    {
        editCommand( [&sample]( CharacterCommand3D& command )
        { command._pendingField.combine( sample ); } );
    }

    void CharacterControllerComponent::setRadius( float32 radius )
    {
        _radius = radius;
        requestRebuild();
    }

    void CharacterControllerComponent::setHalfHeight( float32 halfHeight )
    {
        _halfHeight = halfHeight;
        requestRebuild();
    }

    void CharacterControllerComponent::setStepHeight( float32 stepHeight )
    {
        _stepHeight = stepHeight;
        requestRebuild();
    }

    void CharacterControllerComponent::setMaxSlopeAngle( float32 angle )
    {
        _maxSlopeAngle = Radians::fromRadians( angle );
        requestRebuild();
    }

    void CharacterControllerComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat3( getVelocity() );
        inoutBuilder.addUint8( isGrounded() ? 1 : 0 );
    }

    void CharacterControllerComponent::onRegister( GameObjectManager& manager )
    {
        PhysicsComponent::onRegister( manager );
        registerFixedStep<&CharacterControllerComponent::onPhysicsFrameBegin>( manager, FixedStepGroup::FrameBegin );
        registerFixedStep<&CharacterControllerComponent::onPrePhysicsStep>( manager, FixedStepGroup::PreStep );
        registerFixedStep<&CharacterControllerComponent::onPostPhysicsStep>( manager, FixedStepGroup::PostStep );
        registerFixedStep<&CharacterControllerComponent::onPhysicsFrameEnd>( manager, FixedStepGroup::FrameEnd );
    }

    void CharacterControllerComponent::onWorldOriginShifted( const float3& delta )
    {
        ScenePhysics* const    pPhysics = getScenePhysics();
        IPhysicsScene3D* const pScene   = pPhysics != nullptr ? pPhysics->findScene3D() : nullptr;
        if ( pScene == nullptr || _character.isValid() == false || absorbWorldOriginShift( delta ) == false )
            return;
        _state._position += delta;
        _previousPosition += delta;
        pScene->setCharacterPosition( _character, _state._position ); // 캐릭터는 바디가 아니라 물리 씬이 옮기지 않는다
    }

    void CharacterControllerComponent::onPhysicsFrameBegin( const FixedStepContext& context )
    {
        ScenePhysics& physics = context.getPhysics();
        if ( isSimulated() == false || isSuspended() )
        {
            releasePhysics( physics );
            if ( hasBegunPlay() && ( isActive() == false || isSuspended() ) )
                discardPendingCommands(); // 시작 전 · 스트리밍이 붙잡은 동안의 요청은 첫 걸음의 것이라 남긴다
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        _physicsStep.assumeHeld(); // 프레임 시작 훅은 스텝 훅과 겹치지 않는다(`onPrePhysicsStep` 과 같은 규칙)
        // 이번 게임 프레임에 모인 필드를 이 물리 프레임의 것으로 옮긴다 — 스텝이 없는 프레임이 끼어도 한 프레임의 필드가 두 번 쌓이지 않는다.
        PhysicsFieldSample field{};
        float3             climbVelocity{};
        bool               bClimb = false;
        float3             surfaceVelocity{};
        float32            moveScale = 1.0f;
        float3             rootMotion{};
        const bool         bManual = _bManualSimulation == SW_TRUE;
        editCommand( [&field, &climbVelocity, &bClimb, &surfaceVelocity, &moveScale, &rootMotion, bManual]( CharacterCommand3D& command )
        {
            field                  = command._pendingField;
            command._pendingField  = PhysicsFieldSample{};
            climbVelocity          = command._pendingClimbVelocity;
            bClimb                 = command._bPendingClimb;
            command._bPendingClimb = false;
            if ( bManual == false )
                return;
            // 손으로 돌리는 걸음은 프레임과 걸음 크기가 다르다 — 면 속도 · 이동 계수 · 루트 모션도 필드처럼 프레임마다 옮겨 두고 걸음이 그 값을 쓴다(걸음 없는 프레임에 쌓이지 않게).
            surfaceVelocity                 = command._pendingSurfaceVelocity;
            moveScale                       = command._pendingMoveScale;
            rootMotion                      = command._pendingRootMotion;
            command._pendingSurfaceVelocity = float3{};
            command._pendingMoveScale       = 1.0f;
            command._pendingRootMotion      = float3{};
        } );
        _frameField         = field;
        _frameClimbVelocity = climbVelocity;
        _bFrameClimb        = bClimb ? SW_TRUE : SW_FALSE;
        if ( bManual )
        {
            const float32 frameSeconds = context.getFrameDeltaTime().count();
            _frameSurfaceVelocity      = surfaceVelocity;
            _frameMoveScale            = moveScale;
            _frameRootMotionVelocity   = frameSeconds > 0.0f ? rootMotion * ( 1.0f / frameSeconds ) : float3{}; // 시간이 서면 걸음도 없다
            _reportedMoveScale.store( moveScale, std::memory_order_relaxed );
        }
        if ( consumeRebuild() )
            releasePhysics( physics );

        // 트랜스폼의 바깥 변화는 칸의 변경 번호로 안다 — 같으면 월드 행렬을 풀지 않는다.
        const PhysicsTransformChange change      = consumeTransformChange();
        const bool                   bCreate     = _character.isValid() == false;
        const bool                   bTeleported = consumeTeleport();
        // 배율은 끝 훅(`onPhysicsFrameEnd`)이 먼저 읽었을 수 있다 — 변경 번호만이 아니라 지은 배율과의 값 비교로도 본다.
        const float32 requestedReduction = MathUtil::max( 0.0f, _requestedHeightReduction.load( std::memory_order_relaxed ) );
        const bool    bResize            = bCreate == false && requestedReduction != _appliedHeightReduction;
        const bool    bUnchanged         = bCreate == false && change.hasChanged() == false && bTeleported == false && bResize == false && isShapeScaleStale( _worldScale ) == false;
        if ( bUnchanged )
            return;
        float3 position{};
        if ( PhysicsComponent::readWorldPose( *this, position, _worldRotation, _worldScale ) == false )
        {
            releasePhysics( physics ); // 배율 0 — 캡슐을 지을 수 없다(`readWorldPose` 가 알렸다). 배율이 돌아오면 다시 만든다
            return;
        }
        // 캡슐은 지을 때 배율을 굽는다 — 배율이 바뀌면 캐릭터는 두고 크기만 간다(속 바디의 접촉 · 세로 속도가 이어진다 — 강체 `applyScaledShape` 와 같다, 감사 S-4).
        // 줄인 키도 같은 길이다 — 캐릭터를 두고 크기만 바꾼다(언리얼 `SetCapsuleHalfHeight`).
        _appliedHeightReduction = requestedReduction;
        if ( bCreate == false && ( isShapeScaleStale( _worldScale ) || bResize ) )
        {
            const PhysicsShapeDesc3D capsule = getScaledCapsule();
            if ( pScene->setCharacterSize( _character, capsule._radius, capsule._halfHeight ) == false )
            {
                releasePhysics( physics );
                return;
            }
            markShapeScale( _worldScale );
        }
        if ( bCreate )
        {
            const PhysicsShapeDesc3D capsule = getScaledCapsule();
            PhysicsCharacterDesc3D   desc;
            desc._position      = position;
            desc._userData      = getOwner()->getObjectID();
            desc._radius        = capsule._radius;
            desc._halfHeight    = capsule._halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle.radians();
            desc._stepHeight    = _stepHeight;
            desc._mass          = _mass;
            desc._layer         = physics.resolveLayer( _layer );
            desc._bInnerBody    = _bInnerBody;
            _character          = pScene->createCharacter( desc );
            if ( _character.isValid() )
                markBodyCreated();
            markShapeScale( _worldScale );
            // 이어받는 운동(`_verticalSpeed` · `_airVelocity` …)은 지우지 않는다 — 스냅샷에서 돌아온 값이면 그대로 잇고, 앞 캐릭터를 놓은 것이면 놓을 때 지웠다(`releasePhysics`).
            _state            = PhysicsCharacterState3D{};
            _state._bGrounded = _bGrounded;
            _state._position  = position;
            _previousPosition = position;
            return;
        }
        // 회전 · 배율만 바뀌었으면 자리는 시뮬레이션의 것이다 — 그린 자리는 두 스텝 사이(`getAlpha`)라, 그것으로 옮기면 몸을 돌리는 프레임마다 걸은 만큼이 되돌아간다.
        if ( bTeleported == false && change.hasPosition() == false )
            return;
        pScene->setCharacterPosition( _character, position );
        _state._position     = position;
        _previousPosition    = position;
        _motionDisplayOffset = float3{};
    }

    void CharacterControllerComponent::onPrePhysicsStep( const FixedStepContext& context )
    {
        ScenePhysics& physics        = context.getPhysics();
        const float32 fixedDeltaTime = context.getFixedDeltaTime().count();
        const uint32  stepIndex      = context.getStepIndex();
        const uint32  stepCount      = context.getStepCount();
        _physicsStep.assumeHeld(); // 물리 스텝은 이 컴포넌트마다 한 번에 한 스레드가 돈다
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _character.isValid() == false || _bManualSimulation == SW_TRUE )
            return;
        // 입력 칸에서 꺼낸다(잠금 안에서는 복사 · 비우기만). 루트 모션 · 면 속도 · 이동 계수는 프레임의 첫 스텝에만 꺼낸다.
        CharacterCommand3D command{};
        editCommand( [&command, stepIndex]( CharacterCommand3D& pending )
        {
            command                   = pending;
            pending._pendingJumpSpeed = 0.0f;
            pending._pendingLaunch    = float3{};
            pending._bPendingStop     = false;
            if ( stepIndex == 0 )
            {
                pending._pendingRootMotion      = float3{};
                pending._pendingSurfaceVelocity = float3{};
                pending._pendingMoveScale       = 1.0f;
            }
        } );
        // 루트 모션은 프레임의 첫 스텝에 꺼내 이 프레임의 스텝 수로 나눈다 — 프레임이 끝날 때 다 간다. 면 속도도 프레임마다 꺼낸다.
        // 멈춤 요청은 앞 프레임에서 남은 루트 모션을 지운다 — 이번에 꺼내는 것은 멈춤 뒤에 쌓인 것이다.
        if ( command._bPendingStop )
            _frameRootMotionVelocity = float3{};
        if ( stepIndex == 0 )
        {
            const float32 frameSeconds = fixedDeltaTime * static_cast<float32>( stepCount > 0 ? stepCount : 1 );
            _frameRootMotionVelocity   = command._pendingRootMotion * ( 1.0f / frameSeconds );
            _frameSurfaceVelocity      = command._pendingSurfaceVelocity;
            _frameMoveScale            = command._pendingMoveScale;
            _reportedMoveScale.store( _frameMoveScale, std::memory_order_relaxed );
        }
        _previousPosition   = _state._position;
        _queuedStepVelocity = beginStep( command, _frameMoveScale, pScene->getGravity()._y, fixedDeltaTime );
        _queuedStepSeconds  = fixedDeltaTime;
        // 옮기기는 씬이 모든 캐릭터를 모아 한 번에 한다(이 스텝의 모든 `onPrePhysicsStep` 뒤, 월드 스텝 앞) — 결과는 `onPostPhysicsStep` 이 잇는다.
        physics.queueCharacterMove( _character, _queuedStepVelocity );
    }

    float3 CharacterControllerComponent::beginStep( const CharacterCommand3D& command, float32 moveScale, float32 gravityY, float32 fixedDeltaTime )
    {
        _physicsStep.assumeHeld(); // 부르는 쪽(물리 스텝 · 게임 스레드의 손 걸음)은 서로 겹치지 않는다
        if ( command._bPendingStop )
        {
            _verticalSpeed = 0.0f;
            _walkVelocity  = float3{};
            _fieldVelocity = float3{};
            _airVelocity   = float3{};
        }
        float32 jumpSpeed = command._pendingJumpSpeed;
        bool    bLaunched = false;
        if ( command._pendingLaunch.getLengthSquared() > 0.0f )
        {
            jumpSpeed    = command._pendingLaunch._y;
            _airVelocity = float3{ command._pendingLaunch._x, 0.0f, command._pendingLaunch._z };
            bLaunched    = true;
        }
        // 이동 계수는 걷는 몫(요청 · 루트 모션)에만 든다 — 면이 나르는 속도와 공중 관성은 환경 저항과 상관없다.
        const PhysicsFieldSample& field         = _frameField;
        const float3              requestedWalk = ( command._moveVelocity + _frameRootMotionVelocity ) * moveScale;
        // 접지(빙판): 디딘 채로 접지가 보통보다 낮으면 걷는 속도가 요청으로 천천히 간다(미끄러진다). 보통 접지 · 공중은 요청 그대로다(필드가 없을 때와 같다).
        const bool bSlippery = _state._bGrounded && field._friction < 1.0f;
        _walkVelocity        = bSlippery ? float3::lerp( _walkVelocity, requestedWalk, MathUtil::saturate( field._friction * kFrictionResponse * fixedDeltaTime ) ) : requestedWalk;
        // 필드의 가속 · 흐름이 쌓는 수평 속도(당김 · 해류). 저항은 흐름으로 끌고, 디디면 접지만큼 줄어든다.
        _fieldVelocity = _fieldVelocity + float3{ field._acceleration._x, 0.0f, field._acceleration._z } * fixedDeltaTime;
        if ( field._drag > 0.0f )
            _fieldVelocity = _fieldVelocity + ( float3{ field._flowVelocity._x, 0.0f, field._flowVelocity._z } - _fieldVelocity ) * MathUtil::saturate( field._drag * fixedDeltaTime );
        if ( _state._bGrounded )
            _fieldVelocity = _fieldVelocity * ( 1.0f - MathUtil::saturate( kFieldGroundDamping * field._friction * fixedDeltaTime ) );
        // 직전 스텝을 디딘 채 끝냈으면 이번 스텝은 바닥이 실어 나른다(`moveCharacter` 가 바닥 속도를 더한다) — 떠날 때 이은 공중 관성을 함께 쓰면 두 번 실린다.
        // 흔들리는 갑판에서 떴다 내렸다 할 때마다 그렇게 앞서 나가 선원이 배보다 빨리 가 떨어졌다(boat). 이번 스텝의 발사만 남긴다.
        if ( _state._bGrounded && bLaunched == false )
            _airVelocity = float3{};
        float3 moveVelocity = _walkVelocity + _fieldVelocity + _frameSurfaceVelocity + _airVelocity;
        // 서 있으면 떨어지는 속도를 지우고, 떠 있으면 중력을 쌓는다(필드의 중력 배율 · 부력 · 가속 · 저항). 점프는 그 위에 덮는다.
        if ( _state._bGrounded && _verticalSpeed < 0.0f )
            _verticalSpeed = 0.0f;
        _verticalSpeed += gravityY * _gravityScale * field._gravityScale * fixedDeltaTime;
        _verticalSpeed += ( field.computeBuoyancy( MathUtil::abs( gravityY ), kBodyDensity ) + field._acceleration._y ) * fixedDeltaTime;
        if ( field._drag > 0.0f )
            _verticalSpeed += ( field._flowVelocity._y - _verticalSpeed ) * MathUtil::saturate( field._drag * fixedDeltaTime );
        // 헤엄: 손발이 낼 수 있는 속도까지만 끈다 — 부력 · 흐름 · 뛰어오름이 이미 더 빠르면 그대로다(걷기 요청과 같이 바로 든다).
        if ( _movementMode == CharacterMovementMode::Swimming && command._verticalMoveSpeed != 0.0f )
            _verticalSpeed = command._verticalMoveSpeed > 0.0f ? MathUtil::max( _verticalSpeed, command._verticalMoveSpeed ) : MathUtil::min( _verticalSpeed, command._verticalMoveSpeed );
        if ( jumpSpeed > 0.0f )
            _verticalSpeed = jumpSpeed;
        // 오르기: 중력 · 필드 · 공중 관성 없이 이동기가 준 속도만 쓴다. 이 스텝의 점프 · 발사는 매달림을 놓는 것이라 이긴다.
        const bool bClimb = _bFrameClimb == SW_TRUE && bLaunched == false && jumpSpeed <= 0.0f;
        _bStepClimb       = bClimb ? SW_TRUE : SW_FALSE;
        if ( bClimb )
        {
            _walkVelocity  = float3{};
            _fieldVelocity = float3{};
            _airVelocity   = float3{};
            moveVelocity   = float3{ _frameClimbVelocity._x, 0.0f, _frameClimbVelocity._z };
            _verticalSpeed = _frameClimbVelocity._y;
        }
        return float3{ moveVelocity._x, _verticalSpeed, moveVelocity._z };
    }

    void CharacterControllerComponent::onPostPhysicsStep( const FixedStepContext& context )
    {
        ScenePhysics& physics = context.getPhysics();
        _physicsStep.assumeHeld(); // `onPrePhysicsStep` 과 같은 스레드 규칙이다
        const float32 fixedDeltaTime  = _queuedStepSeconds;
        _queuedStepSeconds            = 0.0f;
        const IPhysicsScene3D* pScene = physics.findScene3D();
        if ( fixedDeltaTime <= 0.0f || pScene == nullptr || pScene->getCharacterState( _character, _state ) == false )
            return; // 이번 스텝에 넣은 이동이 없다
        finishStep( fixedDeltaTime );
    }

    void CharacterControllerComponent::finishStep( float32 fixedDeltaTime )
    {
        _physicsStep.assumeHeld(); // `beginStep` 과 같다
        if ( isStepHistoryEnabled() )
        {
            // 진단 기록 — 켰을 때만 칸을 만들고 쓴다(꺼져 있으면 이 분기 하나).
            if ( _listStepHistory.empty() )
                _listStepHistory.resize( kStepHistoryCapacity );
            _listStepHistory[_stepHistoryCount % kStepHistoryCapacity] = StepRecord{ _queuedStepVelocity, _state };
            ++_stepHistoryCount;
        }
        _movementMode = _bStepClimb == SW_TRUE ? CharacterMovementMode::Climbing : _frameField.selectMovementMode( _state._bGrounded, kBodyDensity );
        // 결정: 바닥이 돌면 위치만이 아니라 방향(yaw)도 같이 돈다 — 이번 스텝에 바닥이 돈 각을 모아 두고 트랜스폼을 쓸 때 얹는다.
        _pendingGroundYaw += _state._groundYawRate * fixedDeltaTime;
        _bGrounded = _state._bGrounded;
        if ( _bStepClimb == SW_TRUE )
            _verticalSpeed = 0.0f; // 오르던 속도는 잇지 않는다 — 구역을 벗어나면 그 자리에서 떨어지기 시작한다
        // 천장에 막혔으면 오르던 속도가 줄었다 — 실제로 난 수직 속도를 잇는다. 디디면 수직 속도는 0 이다.
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
        {
            _verticalSpeed = 0.0f;
            _airVelocity   = float3{}; // 발사가 끝났다 — 다시 디뎠다
        }
        else if ( _state.isRiseBlocked( _verticalSpeed ) )
            _verticalSpeed = _state._velocity._y;
        // 결정: 바닥을 떠나는 스텝(점프 · 걸어 내려섬)에 바닥이 실어 나르던 속도를 관성으로 잇는다 — 수평은 `_airVelocity` 로, 오르는 바닥이면 수직 속도에도.
        // 바닥 속도는 딛고 있는 스텝에만 얹히므로(`moveCharacter`), 이을 곳은 떠나는 순간을 아는 여기뿐이다. 위의 천장 판정 뒤에 둔다
        // (`_velocity` 는 바닥 몫을 뺀 값이라 앞에 두면 더한 수직 속도를 도로 깎는다).
        const bool bLeftGround = _state._bGrounded == false && _state._carriedVelocity.getLengthSquared() > 0.0f;
        if ( bLeftGround )
        {
            _airVelocity += float3{ _state._carriedVelocity._x, 0.0f, _state._carriedVelocity._z };
            if ( _state._carriedVelocity._y > 0.0f )
                _verticalSpeed += _state._carriedVelocity._y;
        }
    }

    void CharacterControllerComponent::onPhysicsFrameEnd( const FixedStepContext& context )
    {
        ScenePhysics& physics = context.getPhysics();
        const bool    bManual = _bManualSimulation == SW_TRUE;
        const float32 alpha   = bManual ? _motionDisplayAlpha : context.getAlpha();
        if ( _character.isValid() == false )
            return;
        const float3 position = float3::lerp( _previousPosition, _state._position, alpha ) + ( bManual ? _motionDisplayOffset : float3{} );
        // 회전 · 배율은 트랜스폼의 것이다. 이 프레임 시작 뒤에 누가 옮겼을 때만 다시 읽는다(자리는 아래에서 캐릭터 자리로 덮는다 — 전에도 그랬다).
        float3 current{};
        if ( consumeTransformChange().hasChanged() && PhysicsComponent::readWorldPose( *this, current, _worldRotation, _worldScale ) == false )
            return; // 배율 0 — 트랜스폼을 덮어쓰지 않는다(다음 프레임 시작 훅이 캐릭터를 놓는다)
        if ( _pendingGroundYaw != 0.0f )
        {
            _worldRotation    = quaternion::concatenate( _worldRotation, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, _pendingGroundYaw ) ).normalize();
            _pendingGroundYaw = 0.0f;
        }
        // 강체와 같이 모아 한 번에 쓴다 — 적용 · 변경 번호 맞추기는 `ScenePhysics::queuePoseWrite` 가 한다.
        physics.queuePoseWrite( *this, position, _worldRotation, _worldScale );
    }

    static_assert( static_cast<uint8>( CharacterMovementMode::Climbing ) == 4, "CharacterMotionState::_movementMode 의 Max 를 열거의 끝 값과 맞춘다" );

    void CharacterControllerComponent::setManualSimulation( bool bManual )
    {
        const uint8 bNewManual = bManual ? SW_TRUE : SW_FALSE;
        if ( _bManualSimulation == bNewManual )
            return;
        _bManualSimulation = bNewManual;
        // 걸음의 주인이 바뀐다 — 앞 주인이 쓰지 않은 한 번짜리 요청(점프 · 발사 · 루트 모션 · 멈춤 …)과 프레임 값은 다음 주인의 것이 아니다. 유지하는 이동 요청만 남긴다.
        discardPendingCommands();
        _physicsStep.assumeHeld(); // 게임 스레드, 틱 밖 — 스텝 훅과 겹치지 않는다
        _frameRootMotionVelocity = float3{};
        _frameSurfaceVelocity    = float3{};
        _frameMoveScale          = 1.0f;
        _previousPosition        = _state._position;
        _motionDisplayOffset     = float3{};
        _motionDisplayAlpha      = 1.0f;
    }

    CharacterMotionState CharacterControllerComponent::getMotionState() const
    {
        CharacterMotionState state;
        state._position      = _character.isValid() ? _state._position : getWorldPosition();
        state._walkVelocity  = _walkVelocity;
        state._fieldVelocity = _fieldVelocity;
        state._airVelocity   = _airVelocity;
        state._verticalSpeed = _verticalSpeed;
        state._movementMode  = static_cast<uint8>( _movementMode );
        state._bGrounded     = _bGrounded ? SW_TRUE : SW_FALSE;
        return state;
    }

    void CharacterControllerComponent::setMotionState( const CharacterMotionState& state )
    {
        _walkVelocity                   = state._walkVelocity;
        _fieldVelocity                  = state._fieldVelocity;
        _airVelocity                    = state._airVelocity;
        _verticalSpeed                  = state._verticalSpeed;
        _movementMode                   = state.getMovementMode();
        _bGrounded                      = state._bGrounded == SW_TRUE;
        ScenePhysics* const    pPhysics = getScenePhysics();
        IPhysicsScene3D* const pScene   = pPhysics != nullptr ? pPhysics->findScene3D() : nullptr;
        if ( pScene == nullptr || _character.isValid() == false )
        {
            teleportTo( state._position );
            return;
        }
        pScene->setCharacterPosition( _character, state._position );
        pScene->refreshCharacterContacts( _character );
        (void)pScene->getCharacterState( _character, _state ); // 핸들은 위에서 살아 있음을 봤다
        _state._bGrounded = _bGrounded;                        // 걸음 계산의 "디딤" 은 앞 걸음의 결과다(찾은 접촉은 백엔드의 붙기 · 바닥 속도만 쓴다)
    }

    CharacterMotionState CharacterControllerComponent::simulateMotion( const CharacterMotionState& state, const CharacterMotionInput& input, float32 deltaTime, CharacterMotionPass pass )
    {
        SW_ASSERT( _bManualSimulation == SW_TRUE ); // 고정 스텝이 함께 옮기면 한 프레임에 두 번 걷는다
        _physicsStep.assumeHeld();                  // 게임 스레드, 물리 스텝 밖 — 스텝 훅과 겹치지 않는다
        ScenePhysics* const    pPhysics = getScenePhysics();
        IPhysicsScene3D* const pScene   = pPhysics != nullptr ? pPhysics->findScene3D() : nullptr;
        if ( pScene == nullptr || _character.isValid() == false || deltaTime <= 0.0f )
            return state;
        setMotionState( state );
        CharacterCommand3D command{};
        const float3       frameRootMotion = _frameRootMotionVelocity; // 프레임의 값이다 — 한 프레임에 실제 걸음이 여럿이면 저마다 쓰고, 다시 돌리는 걸음은 보지 않는다
        if ( pass == CharacterMotionPass::Live )
        {
            // 밖에서 쌓은 충격은 실제 걸음이 한 번 쓴다 — 다시 돌리는 걸음은 보지 않는다.
            editCommand( [&command]( CharacterCommand3D& pending )
            {
                command._pendingJumpSpeed = pending._pendingJumpSpeed;
                command._pendingLaunch    = pending._pendingLaunch;
                command._bPendingStop     = pending._bPendingStop;
                pending._pendingJumpSpeed = 0.0f;
                pending._pendingLaunch    = float3{};
                pending._bPendingStop     = false;
            } );
            _previousPosition = state._position;
        }
        // 멈춤 요청은 이미 옮겨 둔 루트 모션도 지운다(물리 스텝과 같다).
        if ( pass != CharacterMotionPass::Live || command._bPendingStop )
            _frameRootMotionVelocity = float3{};
        command._moveVelocity      = float3{ input._moveVelocity._x, 0.0f, input._moveVelocity._z };
        command._verticalMoveSpeed = input._verticalMoveSpeed;
        if ( input._jumpSpeed > 0.0f )
            command._pendingJumpSpeed = input._jumpSpeed;
        if ( input._launchVelocity.getLengthSquared() > 0.0f )
            command._pendingLaunch = input._launchVelocity;
        _bFrameClimb             = input._bClimb;
        _frameClimbVelocity      = input._climbVelocity;
        _queuedStepVelocity      = beginStep( command, MathUtil::min( _frameMoveScale, MathUtil::max( input._moveScale, 0.0f ) ), pScene->getGravity()._y, deltaTime );
        _frameRootMotionVelocity = frameRootMotion;
        _state                   = pass == CharacterMotionPass::Live ? pScene->moveCharacter( _character, _queuedStepVelocity, deltaTime )
                                                                     : pScene->replayCharacterMove( _character, _queuedStepVelocity, deltaTime );
        finishStep( deltaTime );
        return getMotionState();
    }

    void CharacterControllerComponent::applyMotionInput( const CharacterMotionInput& input )
    {
        editCommand( [&input]( CharacterCommand3D& command )
        {
            command._moveVelocity      = float3{ input._moveVelocity._x, 0.0f, input._moveVelocity._z };
            command._verticalMoveSpeed = input._verticalMoveSpeed;
            command._pendingMoveScale  = MathUtil::min( command._pendingMoveScale, MathUtil::max( input._moveScale, 0.0f ) );
            if ( input._jumpSpeed > 0.0f )
                command._pendingJumpSpeed = input._jumpSpeed;
            if ( input._launchVelocity.getLengthSquared() > 0.0f )
                command._pendingLaunch = input._launchVelocity;
            if ( input._bClimb == SW_TRUE )
            {
                command._pendingClimbVelocity = input._climbVelocity;
                command._bPendingClimb        = true;
            }
        } );
    }

    void CharacterControllerComponent::setMotionDisplay( const float3& previousPosition, float32 alpha, const float3& offset )
    {
        _previousPosition    = previousPosition;
        _motionDisplayAlpha  = alpha;
        _motionDisplayOffset = offset;
    }

    bool CharacterControllerComponent::isSpotFree( const float3& feet ) const
    {
        const ScenePhysics* const         pPhysics = getScenePhysics();
        const IPhysicsQueryScene3D* const pScene   = pPhysics != nullptr ? pPhysics->findScene3D() : nullptr;
        if ( pScene == nullptr )
            return true;
        const PhysicsShapeDesc3D capsule = getScaledCapsule();
        const float3             center{ feet._x, feet._y + kSpotGroundClearance + capsule._radius + capsule._halfHeight, feet._z };
        PhysicsQueryFilter       filter;
        filter._ignoreUserData = getOwner()->getObjectID(); // 제 속 바디
        vector<PhysicsBodyHandle> listBody;
        return pScene->overlapShape( capsule, center, quaternion{}, filter, listBody ) == 0;
    }

    optional<float3> CharacterControllerComponent::findFreeSpot( const float3& feet, float32 searchRadius ) const
    {
        if ( isSpotFree( feet ) )
            return feet;
        const float32 spacing = 2.0f * ( getScaledCapsule()._radius + kSpotGroundClearance );
        for ( float32 distance = spacing; distance <= searchRadius; distance += spacing )
        {
            for ( int32 direction = 0; direction < kSpotDirectionCount; ++direction )
            {
                const float32 angle = MathUtil::kTwoPi * static_cast<float32>( direction ) / static_cast<float32>( kSpotDirectionCount );
                const float3  spot{ feet._x + MathUtil::sin( angle ) * distance, feet._y, feet._z + MathUtil::cos( angle ) * distance };
                if ( isSpotFree( spot ) )
                    return spot;
            }
        }
        return nullopt;
    }

    void CharacterControllerComponent::copyTuning( const CharacterControllerComponent& source )
    {
        _radius        = source._radius;
        _halfHeight    = source._halfHeight;
        _stepHeight    = source._stepHeight;
        _maxSlopeAngle = source._maxSlopeAngle;
        _gravityScale  = source._gravityScale;
        _mass          = source._mass;
        _layer         = source._layer;
        _bInnerBody    = source._bInnerBody;
        requestRebuild();
    }

    void CharacterControllerComponent::collectStreamingFloorShapes( const float3& scale, vector<PhysicsShapeDesc3D>& outListShape ) const
    {
        PhysicsShapeDesc3D capsule;
        capsule._type             = PhysicsShapeType3D::Capsule;
        capsule._radius           = _radius;
        capsule._halfHeight       = _halfHeight;
        capsule                   = PhysicsComponent::makeScaledShape( capsule, scale );
        capsule._localPosition._y = capsule._halfHeight + capsule._radius; // 자리가 발이다
        outListShape.push_back( std::move( capsule ) );
    }

    PhysicsShapeDesc3D CharacterControllerComponent::getScaledCapsule() const
    {
        PhysicsShapeDesc3D capsule;
        capsule._type       = PhysicsShapeType3D::Capsule;
        capsule._radius     = _radius;
        capsule._halfHeight = MathUtil::max( 0.0f, _halfHeight - _appliedHeightReduction * 0.5f ); // 발 기준 캡슐이라 반 높이를 줄이면 머리만 내려온다
        return PhysicsComponent::makeScaledShape( capsule, _worldScale );
    }

    void CharacterControllerComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _character.isValid() == false )
            return;
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyCharacter( _character );
        _character     = PhysicsCharacterHandle{};
        _walkVelocity  = float3{};
        _fieldVelocity = float3{};
        _airVelocity   = float3{};
        _verticalSpeed = 0.0f;
        _bGrounded     = false;
    }
} // namespace sw

namespace sw
{
    void CharacterControllerComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }
    SW_DEV_COMMAND( MovementHistory, "movement.history", "movement.history on|off",
                    "Record the last character controller steps (request, result) for the movement debugger (hud.print movement)", &CharacterControllerHistoryInternal::runHistory );
} // namespace sw
