#include "pch.h"

#include "Engine/Object/Component/Physics/RigidBodyComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    RigidBodyComponent::RigidBodyComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _bodyType{ PhysicsBodyType::Dynamic }
        , _listShape{ PhysicsShapeDesc3D{} }
        , _layer{ "Default" }
        , _material{}
        , _mass{ 0.0f }
        , _linearDamping{ 0.05f }
        , _angularDamping{ 0.05f }
        , _gravityFactor{ 1.0f }
        , _bTrigger{ false }
        , _bContinuous{ false }
        , _bLockRotation{ false }
        , _bInterpolate{ true }
        , _body{}
        , _command{}
        , _commandLock{}
        , _previousPosition{}
        , _currentPosition{}
        , _writtenPosition{}
        , _kinematicStartPosition{}
        , _kinematicTargetPosition{}
        , _lastLinearVelocity{}
        , _lastAngularVelocity{}
        , _previousRotation{}
        , _currentRotation{}
        , _writtenRotation{}
        , _kinematicStartRotation{}
        , _kinematicTargetRotation{}
        , _appliedBodyType{ PhysicsBodyType::Dynamic }
        , _bHasWritten{ false }
    {
    }

    IPhysicsScene3D* RigidBodyComponent::findScene() const
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
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._force += force;
    }

    void RigidBodyComponent::addImpulse( const float3& impulse )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._impulse += impulse;
    }

    void RigidBodyComponent::addTorque( const float3& torque )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._torque += torque;
    }

    void RigidBodyComponent::setLinearVelocity( const float3& velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._linearVelocity     = velocity;
        _command._bSetLinearVelocity = true;
    }

    void RigidBodyComponent::setAngularVelocity( const float3& velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._angularVelocity     = velocity;
        _command._bSetAngularVelocity = true;
    }

    void RigidBodyComponent::wake()
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._bWake = true;
    }

    float32 RigidBodyComponent::getBodyMass() const
    {
        const IPhysicsScene3D* pScene = findScene();
        return pScene != nullptr ? pScene->getBodyMass( _body ) : 0.0f;
    }

    bool RigidBodyComponent::isSleeping() const
    {
        const IPhysicsScene3D* pScene = findScene();
        return pScene != nullptr && pScene->isBodySleeping( _body );
    }

    void RigidBodyComponent::createBody( ScenePhysics& physics, IPhysicsScene3D& scene, const float3& position, const quaternion& rotation, const float3& scale )
    {
        PhysicsBodyDesc3D desc;
        desc._listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc3D& shape : _listShape )
            desc._listShape.push_back( PhysicsComponentUtil::makeScaledShape( shape, scale ) );
        desc._position           = position;
        desc._rotation           = rotation;
        desc._userData           = getOwner()->getObjectId();
        desc._material           = _material;
        desc._mass               = _mass;
        desc._linearDamping      = _linearDamping;
        desc._angularDamping     = _angularDamping;
        desc._gravityFactor      = _gravityFactor;
        desc._type               = _bodyType;
        desc._layer              = physics.resolveLayer( _layer );
        desc._bTrigger           = _bTrigger;
        desc._bContinuous        = _bContinuous;
        desc._bLockRotation      = _bLockRotation;
        desc._bAllowTypeChange   = true; // 컴포넌트는 플레이 중에 종류를 바꿀 수 있다(문이 부서져 떨어진다)
        _body                    = scene.createBody( desc );
        _appliedBodyType         = _bodyType;
        _previousPosition        = position;
        _currentPosition         = position;
        _previousRotation        = rotation;
        _currentRotation         = rotation;
        _kinematicStartPosition  = position;
        _kinematicStartRotation  = rotation;
        _kinematicTargetPosition = position;
        _kinematicTargetRotation = rotation;
        _writtenPosition         = position;
        _writtenRotation         = rotation;
        _bHasWritten             = true;
    }

    void RigidBodyComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        float3     position{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position, rotation, scale );
        if ( _body.isValid() == false || pScene->isBodyValid( _body ) == false )
        {
            createBody( physics, *pScene, position, rotation, scale );
            (void)consumeTeleport();
            return;
        }

        if ( _appliedBodyType != _bodyType )
        {
            pScene->setBodyType( _body, _bodyType );
            _appliedBodyType = _bodyType;
        }

        // 트랜스폼의 바깥 변화 — 우리가 지난 프레임에 쓴 자세와 다르면 코드가 옮긴 것이다.
        const bool bTeleported = consumeTeleport();
        const bool bMoved      = bTeleported || PhysicsComponentUtil::hasMoved( position, rotation, _writtenPosition, _writtenRotation );
        if ( _bodyType == PhysicsBodyType::Kinematic && bTeleported == false )
        {
            _kinematicStartPosition  = _currentPosition;
            _kinematicStartRotation  = _currentRotation;
            _kinematicTargetPosition = position;
            _kinematicTargetRotation = rotation;
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
        _writtenPosition = position;
        _writtenRotation = rotation;

        PhysicsBodyCommand<PhysicsDimension3D> command;
        {
            std::scoped_lock<SpinLock> lock{ _commandLock };
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
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _body.isValid() == false )
            return;
        if ( _bodyType == PhysicsBodyType::Kinematic )
        {
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
            std::scoped_lock<SpinLock> lock{ _commandLock };
            force  = _command._force;
            torque = _command._torque;
        }
        if ( force.getLengthSquared() > 0.0f )
            pScene->addForce( _body, force );
        if ( torque.getLengthSquared() > 0.0f )
            pScene->addTorque( _body, torque );
    }

    void RigidBodyComponent::postPhysicsStep( ScenePhysics& physics )
    {
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _body.isValid() == false )
            return;
        _previousPosition = _currentPosition;
        _previousRotation = _currentRotation;
        (void)pScene->getBodyTransform( _body, _currentPosition, _currentRotation );
        _lastLinearVelocity  = pScene->getLinearVelocity( _body );
        _lastAngularVelocity = pScene->getAngularVelocity( _body );
    }

    void RigidBodyComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        {
            // 힘 · 토크는 한 프레임짜리다.
            std::scoped_lock<SpinLock> lock{ _commandLock };
            _command._force  = float3{};
            _command._torque = float3{};
        }
        if ( _body.isValid() == false || _bodyType != PhysicsBodyType::Dynamic )
            return;
        const float32    weight   = _bInterpolate ? alpha : 1.0f;
        const float3     position = float3::lerp( _previousPosition, _currentPosition, weight );
        const quaternion rotation = quaternion::slerp( _previousRotation, _currentRotation, weight );
        PhysicsComponentUtil::writeWorldPose( *this, position, rotation, _writtenPosition, _writtenRotation );
        _bHasWritten = true;
    }

    void RigidBodyComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _body.isValid() == false )
            return;
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyBody( _body );
        _body        = PhysicsBodyHandle{};
        _bHasWritten = false;
    }
} // namespace sw
