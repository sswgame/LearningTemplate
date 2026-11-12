#include "pch.h"

#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct CharacterControllerComponentInternal
        {
            /** @brief `character.jump <object> [speed]` — 오브젝트의 캐릭터 컨트롤러가 위로 뜁니다(기본 5 m/s). 시나리오가 점프 · 탈출 입력을 흉내 낸다. */
            static bool runJump( const vector<string>& listArgument, string& outReply )
            {
                float32    speed   = 5.0f;
                const bool bParsed = ( listArgument.size() == 1 || listArgument.size() == 2 ) && ( listArgument.size() == 1 || StringUtil::parseFloat( listArgument[1], speed ) );
                if ( bParsed == false || speed <= 0.0f )
                    return false;
                GameObject*                   pObject     = engine::findActiveObject( hashed_string( listArgument[0] ) );
                CharacterControllerComponent* pController = pObject != nullptr ? pObject->getComponent<CharacterControllerComponent>() : nullptr;
                if ( pController == nullptr )
                {
                    outReply = "character.jump: no object named '" + listArgument[0] + "' with a CharacterControllerComponent in the active scene";
                    return true;
                }
                pController->jump( speed );
                outReply = "character.jump " + listArgument[0];
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( CharacterJump, "character.jump", "character.jump <object> [speed]", "Make an object's character controller jump (speed in m/s, default 5)", &CharacterControllerComponentInternal::runJump );
} // namespace sw

namespace sw
{
    CharacterControllerComponent::CharacterControllerComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Character }
        , _radius{ 0.3f }
        , _halfHeight{ 0.6f }
        , _stepHeight{ 0.3f }
        , _maxSlopeAngle{ MathUtil::kHalfPi * 0.5f }
        , _gravityScale{ 1.0f }
        , _mass{ 70.0f }
        , _layer{ "Character" }
        , _bInnerBody{ false }
        , _character{}
        , _state{}
        , _pCommandPage{ nullptr }
        , _commandSlot{ PhysicsPoseStore::kInvalidSlot }
        , _frameRootMotionVelocity{}
        , _frameSurfaceVelocity{}
        , _frameMoveScale{ 1.0f }
        , _reportedMoveScale{ 1.0f }
        , _airVelocity{}
        , _frameField{}
        , _walkVelocity{}
        , _fieldVelocity{}
        , _movementMode{ CharacterMovementMode::Walking }
        , _previousPosition{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _worldRotation{}
        , _verticalSpeed{ 0.0f }
        , _pendingGroundYaw{ 0.0f }
        , _listStepHistory{}
        , _stepHistoryCount{ 0 }
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

    namespace
    {
        struct CharacterControllerHistoryInternal
        {
            /** @brief 상태입니다 — 엔진 범위의 `CharacterStepHistorySwitch` 가 든다. */
            static atomic<bool>& getEnabled();

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

    /**
     * @class CharacterStepHistorySwitch
     * @brief 모든 캐릭터의 스텝 기록 스위치(물리 워커가 읽는다) — 예전 함수 정적(감춘 싱글턴)을 엔진 범위 서브시스템이 든다. 엔진을 다시 세우면 새로 선다.
     */
    class CharacterStepHistorySwitch final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "CharacterStepHistorySwitch";

        CharacterStepHistorySwitch()
            : _state{ false }
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 든 상태입니다. */
        atomic<bool>& getState() { return _state; }

    private:
        atomic<bool> _state;
    };

    SW_SERVICE_SLOT( CharacterStepHistorySwitch, Required );

    atomic<bool>& CharacterControllerHistoryInternal::getEnabled()
    {
        return engine::getSubsystem<CharacterStepHistorySwitch>().getState();
    }

    void CharacterControllerComponent::setStepHistoryEnabled( bool bEnabled )
    {
        CharacterControllerHistoryInternal::getEnabled().store( bEnabled, std::memory_order_relaxed );
    }

    bool CharacterControllerComponent::isStepHistoryEnabled()
    {
        return CharacterControllerHistoryInternal::getEnabled().load( std::memory_order_relaxed );
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

    void CharacterControllerComponent::setMoveVelocity( const float3& velocity )
    {
        editCommand( [&velocity]( CharacterCommand3D& command )
        { command._moveVelocity = float3{ velocity._x, 0.0f, velocity._z }; } );
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
        _maxSlopeAngle = angle;
        requestRebuild();
    }

    void CharacterControllerComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat3( getVelocity() );
        inoutBuilder.addUint8( isGrounded() ? 1 : 0 );
    }

    void CharacterControllerComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        // 이번 게임 프레임에 모인 필드를 이 물리 프레임의 것으로 옮긴다 — 스텝이 없는 프레임이 끼어도 한 프레임의 필드가 두 번 쌓이지 않는다.
        PhysicsFieldSample field{};
        editCommand( [&field]( CharacterCommand3D& command )
        {
            field                 = command._pendingField;
            command._pendingField = PhysicsFieldSample{};
        } );
        _frameField = field;
        if ( consumeRebuild() )
            releasePhysics( physics );

        // 트랜스폼의 바깥 변화는 칸의 변경 번호로 안다 — 같으면 월드 행렬을 풀지 않는다.
        const bool bTransformChanged = consumeTransformChange();
        const bool bCreate           = _character.isValid() == false;
        const bool bTeleported       = consumeTeleport();
        // 배율은 끝 훅(`endPhysicsFrame`)이 먼저 읽었을 수 있다 — 변경 번호만이 아니라 지은 배율과의 값 비교로도 본다.
        const bool bUnchanged = bCreate == false && bTransformChanged == false && bTeleported == false && isShapeScaleStale( _worldScale ) == false;
        if ( bUnchanged )
            return;
        float3 position{};
        if ( PhysicsComponent::readWorldPose( *this, position, _worldRotation, _worldScale ) == false )
        {
            releasePhysics( physics ); // 배율 0 — 캡슐을 지을 수 없다(`readWorldPose` 가 알렸다). 배율이 돌아오면 다시 만든다
            return;
        }
        // 캡슐은 지을 때 배율을 굽는다 — 배율이 바뀌면 지금 자리에서 다시 짓고 세로 속도 · 이동 상태는 잇는다(강체 `rebuildScaledBody` 와 같다).
        const bool bRescale = bCreate == false && isShapeScaleStale( _worldScale );
        if ( bRescale )
            releasePhysics( physics );
        if ( bCreate || bRescale )
        {
            const PhysicsShapeDesc3D capsule = getScaledCapsule();
            PhysicsCharacterDesc3D   desc;
            desc._position      = position;
            desc._userData      = getOwner()->getObjectID();
            desc._radius        = capsule._radius;
            desc._halfHeight    = capsule._halfHeight;
            desc._maxSlopeAngle = _maxSlopeAngle;
            desc._stepHeight    = _stepHeight;
            desc._mass          = _mass;
            desc._layer         = physics.resolveLayer( _layer );
            desc._bInnerBody    = _bInnerBody;
            _character          = pScene->createCharacter( desc );
            markShapeScale( _worldScale );
            if ( bCreate )
            {
                _state         = PhysicsCharacterState3D{};
                _verticalSpeed = 0.0f;
            }
            _state._position  = position;
            _previousPosition = position;
            return;
        }
        pScene->setCharacterPosition( _character, position );
        _state._position  = position;
        _previousPosition = position;
        if ( bTeleported )
            _verticalSpeed = 0.0f;
    }

    void CharacterControllerComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        _physicsStep.assumeHeld(); // 물리 스텝은 이 컴포넌트마다 한 번에 한 스레드가 돈다
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _character.isValid() == false )
            return;
        // 입력 칸에서 꺼낸다(잠금 안에서는 복사 · 비우기만). 루트 모션 · 면 속도 · 이동 계수는 프레임의 첫 스텝에만 꺼낸다.
        CharacterCommand3D command{};
        editCommand( [&command, stepIndex]( CharacterCommand3D& pending )
        {
            command                   = pending;
            pending._pendingJumpSpeed = 0.0f;
            pending._pendingLaunch    = float3{};
            if ( stepIndex == 0 )
            {
                pending._pendingRootMotion      = float3{};
                pending._pendingSurfaceVelocity = float3{};
                pending._pendingMoveScale       = 1.0f;
            }
        } );
        float3  moveVelocity = command._moveVelocity;
        float32 jumpSpeed    = command._pendingJumpSpeed;
        bool    bLaunched    = false;
        // 루트 모션은 프레임의 첫 스텝에 꺼내 이 프레임의 스텝 수로 나눈다 — 프레임이 끝날 때 다 간다. 면 속도도 프레임마다 꺼낸다.
        if ( stepIndex == 0 )
        {
            const float32 frameSeconds = fixedDeltaTime * static_cast<float32>( stepCount > 0 ? stepCount : 1 );
            _frameRootMotionVelocity   = command._pendingRootMotion * ( 1.0f / frameSeconds );
            _frameSurfaceVelocity      = command._pendingSurfaceVelocity;
            _frameMoveScale            = command._pendingMoveScale;
            _reportedMoveScale.store( _frameMoveScale, std::memory_order_relaxed );
        }
        if ( command._pendingLaunch.getLengthSquared() > 0.0f )
        {
            jumpSpeed    = command._pendingLaunch._y;
            _airVelocity = float3{ command._pendingLaunch._x, 0.0f, command._pendingLaunch._z };
            bLaunched    = true;
        }
        // 이동 계수는 걷는 몫(요청 · 루트 모션)에만 든다 — 면이 나르는 속도와 공중 관성은 환경 저항과 상관없다.
        const PhysicsFieldSample& field         = _frameField;
        const float3              requestedWalk = ( moveVelocity + _frameRootMotionVelocity ) * _frameMoveScale;
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
        moveVelocity = _walkVelocity + _fieldVelocity + _frameSurfaceVelocity + _airVelocity;
        // 서 있으면 떨어지는 속도를 지우고, 떠 있으면 중력을 쌓는다(필드의 중력 배율 · 부력 · 가속 · 저항). 점프는 그 위에 덮는다.
        if ( _state._bGrounded && _verticalSpeed < 0.0f )
            _verticalSpeed = 0.0f;
        const float32 gravityY = pScene->getGravity()._y;
        _verticalSpeed += gravityY * _gravityScale * field._gravityScale * fixedDeltaTime;
        _verticalSpeed += ( field.computeBuoyancy( MathUtil::abs( gravityY ), kBodyDensity ) + field._acceleration._y ) * fixedDeltaTime;
        if ( field._drag > 0.0f )
            _verticalSpeed += ( field._flowVelocity._y - _verticalSpeed ) * MathUtil::saturate( field._drag * fixedDeltaTime );
        if ( jumpSpeed > 0.0f )
            _verticalSpeed = jumpSpeed;

        _previousPosition = _state._position;
        _state            = pScene->moveCharacter( _character, float3{ moveVelocity._x, _verticalSpeed, moveVelocity._z }, fixedDeltaTime );
        if ( isStepHistoryEnabled() )
        {
            // 진단 기록 — 켰을 때만 칸을 만들고 쓴다(꺼져 있으면 이 분기 하나).
            if ( _listStepHistory.empty() )
                _listStepHistory.resize( kStepHistoryCapacity );
            _listStepHistory[_stepHistoryCount % kStepHistoryCapacity] = StepRecord{
                float3{ moveVelocity._x, _verticalSpeed, moveVelocity._z },
                _state
            };
            ++_stepHistoryCount;
        }
        _movementMode = field.selectMovementMode( _state._bGrounded, kBodyDensity );
        // 결정: 바닥이 돌면 위치만이 아니라 방향(yaw)도 같이 돈다 — 이번 스텝에 바닥이 돈 각을 모아 두고 트랜스폼을 쓸 때 얹는다.
        _pendingGroundYaw += _state._groundYawRate * fixedDeltaTime;
        // 천장에 막혔으면 오르던 속도가 줄었다 — 실제로 난 수직 속도를 잇는다. 디디면 수직 속도는 0 이다.
        if ( _state._bGrounded && _verticalSpeed <= 0.0f )
        {
            _verticalSpeed = 0.0f;
            _airVelocity   = float3{}; // 발사가 끝났다 — 다시 디뎠다
        }
        else if ( _verticalSpeed > 0.0f && _state._velocity._y < _verticalSpeed )
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

    void CharacterControllerComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        if ( _character.isValid() == false )
            return;
        const float3 position = float3::lerp( _previousPosition, _state._position, alpha );
        // 회전 · 배율은 트랜스폼의 것이다. 이 프레임 시작 뒤에 누가 옮겼을 때만 다시 읽는다(자리는 아래에서 캐릭터 자리로 덮는다 — 전에도 그랬다).
        float3 current{};
        if ( consumeTransformChange() && PhysicsComponent::readWorldPose( *this, current, _worldRotation, _worldScale ) == false )
            return; // 배율 0 — 트랜스폼을 덮어쓰지 않는다(다음 프레임 시작 훅이 캐릭터를 놓는다)
        if ( _pendingGroundYaw != 0.0f )
        {
            _worldRotation    = quaternion::concatenate( _worldRotation, quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, _pendingGroundYaw ) ).normalize();
            _pendingGroundYaw = 0.0f;
        }
        // 강체와 같이 모아 한 번에 쓴다 — 적용 · 변경 번호 맞추기는 `ScenePhysics::queuePoseWrite` 가 한다.
        physics.queuePoseWrite( *this, position, _worldRotation, _worldScale );
    }

    PhysicsShapeDesc3D CharacterControllerComponent::getScaledCapsule() const
    {
        PhysicsShapeDesc3D capsule;
        capsule._type       = PhysicsShapeType3D::Capsule;
        capsule._radius     = _radius;
        capsule._halfHeight = _halfHeight;
        return PhysicsComponent::makeScaledShape( capsule, _worldScale );
    }

    void CharacterControllerComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _character.isValid() == false )
            return;
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyCharacter( _character );
        _character = PhysicsCharacterHandle{};
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

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( CharacterStepHistorySwitch, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( CharacterStepHistorySwitch ) );
} // namespace sw
