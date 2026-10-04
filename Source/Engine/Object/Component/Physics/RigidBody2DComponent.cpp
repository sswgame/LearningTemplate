#include "pch.h"

#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    namespace
    {
        struct RigidBody2DComponentInternal
        {
            /** @brief 각 둘을 짧은 쪽으로 보간합니다. */
            static float32 lerpAngle( float32 from, float32 to, float32 weight )
            {
                float32 delta = to - from;
                while ( delta > MathUtil::Pi )
                    delta -= 2.0f * MathUtil::Pi;
                while ( delta < -MathUtil::Pi )
                    delta += 2.0f * MathUtil::Pi;
                return from + delta * weight;
            }

            /** @brief 지금 월드 회전에서 Z 축 둘레 각만 바꾼 회전입니다(다른 축 회전은 2D 물리가 모른다 — 그대로 둔다). */
            static quaternion replaceAngle( const quaternion& current, float32 angle )
            {
                const float32    currentAngle = PhysicsComponentUtil::getAngle2D( current );
                const quaternion delta        = PhysicsComponentUtil::makeRotation2D( angle - currentAngle );
                return delta * current;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RigidBody2DComponent::RigidBody2DComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _bodyType{ PhysicsBodyType::Dynamic }
        , _listShape{ PhysicsShapeDesc2D{} }
        , _layer{ "Default" }
        , _material{}
        , _mass{ 0.0f }
        , _linearDamping{ 0.0f }
        , _angularDamping{ 0.0f }
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
        , _kinematicStartPosition{}
        , _kinematicTargetPosition{}
        , _lastLinearVelocity{}
        , _writtenPosition{}
        , _writtenRotation{}
        , _previousAngle{ 0.0f }
        , _currentAngle{ 0.0f }
        , _kinematicStartAngle{ 0.0f }
        , _kinematicTargetAngle{ 0.0f }
        , _lastAngularVelocity{ 0.0f }
        , _appliedBodyType{ PhysicsBodyType::Dynamic }
        , _bHasWritten{ false }
    {
    }

    IPhysicsScene2D* RigidBody2DComponent::findScene() const
    {
        return getScenePhysics() != nullptr ? getScenePhysics()->findScene2D() : nullptr;
    }

    void RigidBody2DComponent::setBodyType( PhysicsBodyType type )
    {
        _bodyType = type;
    }

    void RigidBody2DComponent::setShapes( const vector<PhysicsShapeDesc2D>& listShape )
    {
        _listShape = listShape;
        requestRebuild();
    }

    void RigidBody2DComponent::setShape( const PhysicsShapeDesc2D& shape )
    {
        _listShape.assign( 1, shape );
        requestRebuild();
    }

    void RigidBody2DComponent::setLayer( const hashed_string& layer )
    {
        _layer = layer;
        requestRebuild();
    }

    void RigidBody2DComponent::setMaterial( const hashed_string& material )
    {
        _material = material;
        requestRebuild();
    }

    void RigidBody2DComponent::setMass( float32 mass )
    {
        _mass = mass;
        requestRebuild();
    }

    void RigidBody2DComponent::setTrigger( bool bTrigger )
    {
        _bTrigger = bTrigger;
        requestRebuild();
    }

    void RigidBody2DComponent::setContinuous( bool bContinuous )
    {
        _bContinuous = bContinuous;
        requestRebuild();
    }

    void RigidBody2DComponent::setGravityFactor( float32 factor )
    {
        _gravityFactor = factor;
        requestRebuild();
    }

    void RigidBody2DComponent::addForce( const float2& force )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._force += force;
    }

    void RigidBody2DComponent::addImpulse( const float2& impulse )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._impulse += impulse;
    }

    void RigidBody2DComponent::addTorque( float32 torque )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._torque += torque;
    }

    void RigidBody2DComponent::setLinearVelocity( const float2& velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._linearVelocity     = velocity;
        _command._bSetLinearVelocity = true;
    }

    void RigidBody2DComponent::setAngularVelocity( float32 velocity )
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._angularVelocity     = velocity;
        _command._bSetAngularVelocity = true;
    }

    void RigidBody2DComponent::wake()
    {
        std::scoped_lock<SpinLock> lock{ _commandLock };
        _command._bWake = true;
    }

    float32 RigidBody2DComponent::getBodyMass() const
    {
        const IPhysicsScene2D* pScene = findScene();
        return pScene != nullptr ? pScene->getBodyMass( _body ) : 0.0f;
    }

    bool RigidBody2DComponent::isSleeping() const
    {
        const IPhysicsScene2D* pScene = findScene();
        return pScene != nullptr && pScene->isBodySleeping( _body );
    }

    void RigidBody2DComponent::createBody( ScenePhysics& physics, IPhysicsScene2D& scene, const float2& position, float32 angle, const float3& scale )
    {
        PhysicsBodyDesc2D desc;
        desc._listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc2D& shape : _listShape )
            desc._listShape.push_back( PhysicsComponentUtil::makeScaledShape( shape, scale ) );
        desc._position           = position;
        desc._rotation           = angle;
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
        desc._bAllowTypeChange   = true;
        _body                    = scene.createBody( desc );
        _appliedBodyType         = _bodyType;
        _previousPosition        = position;
        _currentPosition         = position;
        _kinematicStartPosition  = position;
        _kinematicTargetPosition = position;
        _previousAngle           = angle;
        _currentAngle            = angle;
        _kinematicStartAngle     = angle;
        _kinematicTargetAngle    = angle;
    }

    void RigidBody2DComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene2D* pScene = physics.getScene2D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        float3     position3{};
        quaternion rotation{};
        float3     scale{};
        PhysicsComponentUtil::readWorldPose( *this, position3, rotation, scale );
        const float2  position{ position3._x, position3._y };
        const float32 angle = PhysicsComponentUtil::getAngle2D( rotation );
        if ( _body.isValid() == false || pScene->isBodyValid( _body ) == false )
        {
            createBody( physics, *pScene, position, angle, scale );
            _writtenPosition = position3;
            _writtenRotation = rotation;
            _bHasWritten     = true;
            (void)consumeTeleport();
            return;
        }

        if ( _appliedBodyType != _bodyType )
        {
            pScene->setBodyType( _body, _bodyType );
            _appliedBodyType = _bodyType;
        }

        const bool bTeleported = consumeTeleport();
        const bool bMoved      = bTeleported || PhysicsComponentUtil::hasMoved( position3, rotation, _writtenPosition, _writtenRotation );
        if ( _bodyType == PhysicsBodyType::Kinematic && bTeleported == false )
        {
            _kinematicStartPosition  = _currentPosition;
            _kinematicStartAngle     = _currentAngle;
            _kinematicTargetPosition = position;
            _kinematicTargetAngle    = angle;
        }
        else if ( bMoved )
        {
            pScene->setBodyTransform( _body, position, angle );
            _previousPosition        = position;
            _currentPosition         = position;
            _kinematicStartPosition  = position;
            _kinematicTargetPosition = position;
            _previousAngle           = angle;
            _currentAngle            = angle;
            _kinematicStartAngle     = angle;
            _kinematicTargetAngle    = angle;
        }
        _writtenPosition = position3;
        _writtenRotation = rotation;

        PhysicsBodyCommand<PhysicsDimension2D> command;
        {
            std::scoped_lock<SpinLock> lock{ _commandLock };
            command                       = _command;
            _command._impulse             = float2{};
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

    void RigidBody2DComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene == nullptr || _body.isValid() == false )
            return;
        if ( _bodyType == PhysicsBodyType::Kinematic )
        {
            const float32 fraction = static_cast<float32>( stepIndex + 1 ) / static_cast<float32>( stepCount );
            const float2  target   = float2::lerp( _kinematicStartPosition, _kinematicTargetPosition, fraction );
            const float32 angle    = RigidBody2DComponentInternal::lerpAngle( _kinematicStartAngle, _kinematicTargetAngle, fraction );
            pScene->moveKinematic( _body, target, angle, fixedDeltaTime );
            return;
        }
        if ( _bodyType != PhysicsBodyType::Dynamic )
            return;
        float2  force{};
        float32 torque = 0.0f;
        {
            std::scoped_lock<SpinLock> lock{ _commandLock };
            force  = _command._force;
            torque = _command._torque;
        }
        if ( force.getLengthSquared() > 0.0f )
            pScene->addForce( _body, force );
        if ( torque != 0.0f )
            pScene->addTorque( _body, torque );
    }

    void RigidBody2DComponent::postPhysicsStep( ScenePhysics& physics )
    {
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene == nullptr || _body.isValid() == false )
            return;
        _previousPosition = _currentPosition;
        _previousAngle    = _currentAngle;
        (void)pScene->getBodyTransform( _body, _currentPosition, _currentAngle );
        _lastLinearVelocity  = pScene->getLinearVelocity( _body );
        _lastAngularVelocity = pScene->getAngularVelocity( _body );
    }

    void RigidBody2DComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)physics;
        {
            std::scoped_lock<SpinLock> lock{ _commandLock };
            _command._force  = float2{};
            _command._torque = 0.0f;
        }
        if ( _body.isValid() == false || _bodyType != PhysicsBodyType::Dynamic )
            return;
        const float32 weight   = _bInterpolate ? alpha : 1.0f;
        const float2  position = float2::lerp( _previousPosition, _currentPosition, weight );
        const float32 angle    = RigidBody2DComponentInternal::lerpAngle( _previousAngle, _currentAngle, weight );
        float3        current{};
        quaternion    rotation{};
        float3        scale{};
        PhysicsComponentUtil::readWorldPose( *this, current, rotation, scale );
        PhysicsComponentUtil::writeWorldPose( *this, float3{ position._x, position._y, current._z }, RigidBody2DComponentInternal::replaceAngle( rotation, angle ),
                                              _writtenPosition, _writtenRotation );
        _bHasWritten = true;
    }

    void RigidBody2DComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _body.isValid() == false )
            return;
        IPhysicsScene2D* pScene = physics.findScene2D();
        if ( pScene != nullptr )
            pScene->destroyBody( _body );
        _body        = PhysicsBodyHandle{};
        _bHasWritten = false;
    }
} // namespace sw
