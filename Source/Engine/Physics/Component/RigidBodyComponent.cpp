#include "pch.h"

#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "Engine/Common/StateHashBuilder.h"
#include "Engine/Object/Component/ComponentNameValidation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    RigidBodySocketPhysics::RigidBodySocketPhysics( RigidBodyComponent& owner )
        : _owner{ owner }
    {
    }

    void RigidBodySocketPhysics::beginPhysics( const float4x4& worldTransform, const float3& linearVelocity )
    {
        // 부착이 이미 그 자리에 놓았다(월드 자리를 지켜 뗐다) — 바디는 순간이동 없이 동적으로 바뀌고 시작 속도를 받는다.
        (void)worldTransform;
        _owner.setBodyType( PhysicsBodyType::Dynamic );
        _owner.setLinearVelocity( linearVelocity );
        _owner.wake();
    }

    void RigidBodySocketPhysics::endPhysics()
    {
        _owner.setLinearVelocity( float3{} );
        _owner.setAngularVelocity( float3{} );
        _owner.setBodyType( PhysicsBodyType::Kinematic );
    }

    bool RigidBodySocketPhysics::findBodyWorldTransform( float4x4& outWorldTransform ) const
    {
        if ( _owner.getBodyHandle().isValid() == false )
            return false;
        outWorldTransform = _owner.getWorldMatrix();
        return true;
    }

    RigidBodyComponent::RigidBodyComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _bodyType{ PhysicsBodyType::Dynamic }
        , _listShape{ PhysicsShapeDesc3D{} }
        , _layer{ "Default" }
        , _material{}
        , _hitZone{}
        , _mass{ 0.0f }
        , _linearDamping{ 0.05f }
        , _angularDamping{ 0.05f }
        , _gravityFactor{ 1.0f }
        , _bTrigger{ false }
        , _bContinuous{ false }
        , _bLockRotation{ false }
        , _bInterpolate{ true }
        , _socketPhysics{ *this }
        , _body{}
        , _command{}
        , _commandLock{}
        , _movedStep{ 0 }
        , _previousPosition{}
        , _currentPosition{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _kinematicStartPosition{}
        , _kinematicTargetPosition{}
        , _lastLinearVelocity{}
        , _lastAngularVelocity{}
        , _previousRotation{}
        , _currentRotation{}
        , _kinematicStartRotation{}
        , _kinematicTargetRotation{}
        , _appliedBodyType{ PhysicsBodyType::Dynamic }
        , _bRestPoseWritten{ false }
    {
    }

    IPhysicsBodyScene3D* RigidBodyComponent::findScene() const
    {
        return getScenePhysics() != nullptr ? getScenePhysics()->findScene3D() : nullptr;
    }

    void RigidBodyComponent::setBodyType( PhysicsBodyType type )
    {
        _bodyType = type;
    }

    void RigidBodyComponent::setShapes( const vector<PhysicsShapeDesc3D>& listShape )
    {
        _listShape = listShape;
        requestRebuild();
    }

    void RigidBodyComponent::setShape( const PhysicsShapeDesc3D& shape )
    {
        _listShape.assign( 1, shape );
        requestRebuild();
    }

    void RigidBodyComponent::setLayer( const hashed_string& layer )
    {
        _layer = layer;
        requestRebuild();
    }

    void RigidBodyComponent::setMaterial( const hashed_string& material )
    {
        _material = material;
        requestRebuild();
    }

    void RigidBodyComponent::setMass( float32 mass )
    {
        _mass = mass;
        requestRebuild();
    }

    void RigidBodyComponent::setTrigger( bool bTrigger )
    {
        _bTrigger = bTrigger;
        requestRebuild();
    }

    void RigidBodyComponent::setContinuous( bool bContinuous )
    {
        _bContinuous = bContinuous;
        requestRebuild();
    }

    void RigidBodyComponent::setGravityFactor( float32 factor )
    {
        _gravityFactor = factor;
        requestRebuild();
    }

    void RigidBodyComponent::addForce( const float3& force )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _command._force += force;
    }

    void RigidBodyComponent::addImpulse( const float3& impulse )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _command._impulse += impulse;
    }

    void RigidBodyComponent::addTorque( const float3& torque )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _command._torque += torque;
    }

    void RigidBodyComponent::setLinearVelocity( const float3& velocity )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _command._linearVelocity     = velocity;
        _command._bSetLinearVelocity = true;
    }

    void RigidBodyComponent::setAngularVelocity( const float3& velocity )
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _command._angularVelocity     = velocity;
        _command._bSetAngularVelocity = true;
    }

    void RigidBodyComponent::wake()
    {
        ScopedLock<SpinLock> lock{ _commandLock };
        _command._bWake = true;
    }

    float32 RigidBodyComponent::getBodyMass() const
    {
        const IPhysicsBodyScene3D* pScene = findScene();
        return pScene != nullptr ? pScene->getBodyMass( _body ) : 0.0f;
    }

    bool RigidBodyComponent::isSleeping() const
    {
        const IPhysicsBodyScene3D* pScene = findScene();
        return pScene != nullptr && pScene->isBodySleeping( _body );
    }

    void RigidBodyComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat3( _lastLinearVelocity );
        inoutBuilder.addFloat3( _lastAngularVelocity );
    }

    PhysicsBodyDesc3D RigidBodyComponent::makeBodyDesc( const ScenePhysics& physics, const float3& position, const quaternion& rotation, const float3& scale ) const
    {
        PhysicsBodyDesc3D desc;
        desc._listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc3D& shape : _listShape )
        {
            desc._listShape.push_back( PhysicsComponentUtil::makeScaledShape( shape, scale ) );
        }
        desc._position         = position;
        desc._rotation         = rotation;
        desc._userData         = getOwner()->getObjectID();
        desc._material         = _material;
        desc._mass             = _mass;
        desc._linearDamping    = _linearDamping;
        desc._angularDamping   = _angularDamping;
        desc._gravityFactor    = _gravityFactor;
        desc._type             = _bodyType;
        desc._layer            = physics.resolveLayer( _layer );
        desc._bTrigger         = _bTrigger;
        desc._bContinuous      = _bContinuous;
        desc._bLockRotation    = _bLockRotation;
        desc._bAllowTypeChange = true; // 컴포넌트는 플레이 중에 종류를 바꿀 수 있다(문이 부서져 떨어진다)
        return desc;
    }

    void RigidBodyComponent::appendStaticBodies3D( const ScenePhysics& physics, vector<PhysicsBodyDesc3D>& outListBody ) const
    {
        const bool bStaticBlocker = _bodyType == PhysicsBodyType::Static && _bTrigger == false && _listShape.empty() == false && getOwner() != nullptr;
        if ( bStaticBlocker == false )
            return;
        float3     position{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position, rotation, scale );
        outListBody.push_back( makeBodyDesc( physics, position, rotation, scale ) );
    }

    void RigidBodyComponent::createBody( ScenePhysics& physics, IPhysicsBodyScene3D& scene, const float3& position, const quaternion& rotation, const float3& scale )
    {
        _body                    = scene.createBody( makeBodyDesc( physics, position, rotation, scale ) );
        _appliedBodyType         = _bodyType;
        _previousPosition        = position;
        _currentPosition         = position;
        _previousRotation        = rotation;
        _currentRotation         = rotation;
        _kinematicStartPosition  = position;
        _kinematicStartRotation  = rotation;
        _kinematicTargetPosition = position;
        _kinematicTargetRotation = rotation;
        _bRestPoseWritten        = false;
        physics.bindBodyOwner( *this );
    }

    void RigidBodyComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsBodyScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        // 트랜스폼의 바깥 변화(코드 · 부모가 옮김)는 칸의 변경 번호로 안다 — 같으면 월드 행렬을 풀지 않는다(정적 · 잠든 바디 대부분).
        const bool bTransformChanged = consumeTransformChange();
        float3     position{};
        quaternion rotation{};
        // 바디는 이 컴포넌트만 지운다(씬을 내릴 때는 `ScenePhysics::onSceneCleared` 가 먼저 놓게 한다) — 프레임마다 백엔드에 묻지 않고 Debug 에서만 확인한다.
        SW_ASSERT( _body.isValid() == false || pScene->isBodyValid( _body ) );
        if ( _body.isValid() == false )
        {
            PhysicsComponentUtil::readWorldPose( *this, position, rotation, _worldScale );
            createBody( physics, *pScene, position, rotation, _worldScale );
            (void)consumeTeleport();
            return;
        }

        if ( _appliedBodyType != _bodyType )
        {
            pScene->setBodyType( _body, _bodyType );
            _appliedBodyType = _bodyType;
        }

        const bool bTeleported = consumeTeleport();
        const bool bMoved      = bTeleported || bTransformChanged;
        if ( bMoved )
            PhysicsComponentUtil::readWorldPose( *this, position, rotation, _worldScale );
        if ( _bodyType == PhysicsBodyType::Kinematic && bTeleported == false )
        {
            // 목표는 트랜스폼이다 — 옮겨지지 않았으면 지난 목표가 곧 트랜스폼이다.
            _kinematicStartPosition = _currentPosition;
            _kinematicStartRotation = _currentRotation;
            if ( bMoved )
            {
                _kinematicTargetPosition = position;
                _kinematicTargetRotation = rotation;
            }
        }
        else if ( bMoved )
        {
            pScene->setBodyTransform( _body, position, rotation );
            _previousPosition        = position;
            _currentPosition         = position;
            _previousRotation        = rotation;
            _currentRotation         = rotation;
            _kinematicStartPosition  = position;
            _kinematicStartRotation  = rotation;
            _kinematicTargetPosition = position;
            _kinematicTargetRotation = rotation;
        }

        PhysicsBodyCommand<PhysicsDimension3D> command;
        {
            ScopedLock<SpinLock> lock{ _commandLock };
            command                       = _command;
            _command._impulse             = float3{};
            _command._bSetLinearVelocity  = false;
            _command._bSetAngularVelocity = false;
            _command._bWake               = false;
        }
        if ( command._bSetLinearVelocity )
            pScene->setLinearVelocity( _body, command._linearVelocity );
        if ( command._bSetAngularVelocity )
            pScene->setAngularVelocity( _body, command._angularVelocity );
        if ( command._impulse.getLengthSquared() > 0.0f )
            pScene->addImpulse( _body, command._impulse );
        if ( command._bWake )
            pScene->wakeBody( _body );
    }

    void RigidBodyComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        IPhysicsBodyScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _body.isValid() == false )
            return;
        if ( _bodyType == PhysicsBodyType::Kinematic )
        {
            // 서 있는 키네마틱 바디(목표가 지금 자리이고 지난 스텝에 잠들어 있었다)는 스텝마다 백엔드를 부르지 않는다. 지난 스텝에 깨어 있었으면
            // 남은 속도를 0 으로 돌리려고 한 번 더 부른다.
            const bool bResting = _kinematicStartPosition == _kinematicTargetPosition && _kinematicStartRotation == _kinematicTargetRotation &&
                                  _movedStep != physics.getStepCount();
            if ( bResting )
                return;
            // 프레임의 새 자세까지를 스텝 수로 나눠 간다 — 스텝마다 같은 목표로 가면 첫 스텝에 다 가고 나머지는 서 있다.
            const float32    fraction = static_cast<float32>( stepIndex + 1 ) / static_cast<float32>( stepCount );
            const float3     target   = float3::lerp( _kinematicStartPosition, _kinematicTargetPosition, fraction );
            const quaternion rotation = quaternion::slerp( _kinematicStartRotation, _kinematicTargetRotation, fraction );
            pScene->moveKinematic( _body, target, rotation, fixedDeltaTime );
            return;
        }
        if ( _bodyType != PhysicsBodyType::Dynamic )
            return;
        float3 force{};
        float3 torque{};
        {
            ScopedLock<SpinLock> lock{ _commandLock };
            force  = _command._force;
            torque = _command._torque;
        }
        if ( force.getLengthSquared() > 0.0f )
            pScene->addForce( _body, force );
        if ( torque.getLengthSquared() > 0.0f )
            pScene->addTorque( _body, torque );
    }

    void RigidBodyComponent::readSteppedPose( const IPhysicsBodyScene3D& scene, uint64 stepCount )
    {
        _previousPosition = _currentPosition;
        _previousRotation = _currentRotation;
        (void)scene.getBodyTransform( _body, _currentPosition, _currentRotation );
        _lastLinearVelocity  = scene.getLinearVelocity( _body );
        _lastAngularVelocity = scene.getAngularVelocity( _body );
        _movedStep           = stepCount;
    }

    void RigidBodyComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        {
            // 힘 · 토크는 한 프레임짜리다.
            ScopedLock<SpinLock> lock{ _commandLock };
            _command._force  = float3{};
            _command._torque = float3{};
        }
        if ( _body.isValid() == false || _bodyType != PhysicsBodyType::Dynamic )
            return;
        // 마지막 스텝에 잠들어 있었으면(움직인 바디 목록에 없었다) 보간할 사이가 없다. 그 자세를 한 번 쓴 뒤로는 깰 때까지 트랜스폼을 건드리지 않는다.
        const bool bMovedLastStep = _movedStep == physics.getStepCount();
        if ( bMovedLastStep == false )
        {
            _previousPosition = _currentPosition;
            _previousRotation = _currentRotation;
            if ( _bRestPoseWritten )
                return;
        }
        const float32    weight   = _bInterpolate ? alpha : 1.0f;
        const float3     position = float3::lerp( _previousPosition, _currentPosition, weight );
        const quaternion rotation = quaternion::slerp( _previousRotation, _currentRotation, weight );
        PhysicsComponentUtil::writeWorldPose( *this, position, rotation, _worldScale );
        markTransformSynced();
        _bRestPoseWritten = bMovedLastStep == false;
    }

    void RigidBodyComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _body.isValid() == false )
            return;
        physics.unbindBodyOwner( *this );
        IPhysicsBodyScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyBody( _body );
        _body = PhysicsBodyHandle{};
    }
} // namespace sw

namespace sw
{
    void RigidBodyComponent::validateLayer( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsLayer( context, "_layer", _layer );
    }

    void RigidBodyComponent::validateMaterial( ValidationContext& context ) const
    {
        ComponentNameValidation::validatePhysicsMaterial( context, "_material", _material );
    }
} // namespace sw
