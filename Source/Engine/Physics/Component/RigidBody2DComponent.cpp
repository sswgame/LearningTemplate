#include "pch.h"

#include "Engine/Physics/Component/RigidBody2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    namespace
    {
        /** @brief 지금 월드 회전에서 Z 축 둘레 각만 바꾼 회전입니다(다른 축 회전은 2D 물리가 모른다 — 그대로 둔다). */
        quaternion replaceRigidBody2DAngle( const quaternion& current, float32 angle )
        {
            const float32    currentAngle = PhysicsComponent::getAngle2D( current );
            const quaternion delta        = PhysicsComponent::makeRotation2D( angle - currentAngle );
            return delta * current;
        }
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
        , _hitZone{}
        , _mass{ 0.0f }
        , _linearDamping{ 0.0f }
        , _angularDamping{ 0.0f }
        , _gravityFactor{ 1.0f }
        , _bTrigger{ false }
        , _bContinuous{ false }
        , _bLockRotation{ false }
        , _bInterpolate{ true }
        , _pPosePage{ nullptr }
        , _poseSlot{ PhysicsPoseStore::kInvalidSlot }
        , _kinematicStartPosition{}
        , _kinematicTargetPosition{}
        , _worldPosition{}
        , _worldRotation{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _kinematicStartAngle{ 0.0f }
        , _kinematicTargetAngle{ 0.0f }
        , _appliedBodyType{ PhysicsBodyType::Dynamic }
        , _bRestPoseWritten{ false }
    {
        _poseSlot = engine::getSubsystem<PhysicsPoseStore>().getStore<PhysicsDimension2D>().allocateSlot( this, _pPosePage );
    }

    RigidBody2DComponent::~RigidBody2DComponent()
    {
        engine::getSubsystem<PhysicsPoseStore>().getStore<PhysicsDimension2D>().freeSlot( _poseSlot );
    }

    PhysicsBodyHandle RigidBody2DComponent::getBodyHandle() const
    {
        return _pPosePage->_arrBody[getPoseIndex()];
    }

    float2 RigidBody2DComponent::getLinearVelocity() const
    {
        return _pPosePage->_arrLinearVelocity[getPoseIndex()];
    }

    float32 RigidBody2DComponent::getAngularVelocity() const
    {
        return _pPosePage->_arrAngularVelocity[getPoseIndex()];
    }

    uint32 RigidBody2DComponent::getPoseIndex() const
    {
        return _poseSlot & PhysicsBodyPage2D::kSlotMask;
    }

    IPhysicsBodyScene2D* RigidBody2DComponent::findScene() const
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
        _pPosePage->editCommand( getPoseIndex(), [&force]( PhysicsBodyCommand<PhysicsDimension2D>& command )
        { command._force += force; } );
    }

    void RigidBody2DComponent::addImpulse( const float2& impulse )
    {
        _pPosePage->editCommand( getPoseIndex(), [&impulse]( PhysicsBodyCommand<PhysicsDimension2D>& command )
        { command._impulse += impulse; } );
    }

    void RigidBody2DComponent::addTorque( float32 torque )
    {
        _pPosePage->editCommand( getPoseIndex(), [torque]( PhysicsBodyCommand<PhysicsDimension2D>& command )
        { command._torque += torque; } );
    }

    void RigidBody2DComponent::setLinearVelocity( const float2& velocity )
    {
        _pPosePage->editCommand( getPoseIndex(), [&velocity]( PhysicsBodyCommand<PhysicsDimension2D>& command )
        {
            command._linearVelocity     = velocity;
            command._bSetLinearVelocity = true;
        } );
    }

    void RigidBody2DComponent::setAngularVelocity( float32 velocity )
    {
        _pPosePage->editCommand( getPoseIndex(), [velocity]( PhysicsBodyCommand<PhysicsDimension2D>& command )
        {
            command._angularVelocity     = velocity;
            command._bSetAngularVelocity = true;
        } );
    }

    void RigidBody2DComponent::wake()
    {
        _pPosePage->editCommand( getPoseIndex(), []( PhysicsBodyCommand<PhysicsDimension2D>& command )
        { command._bWake = true; } );
    }

    float32 RigidBody2DComponent::getBodyMass() const
    {
        const IPhysicsBodyScene2D* pScene = findScene();
        return pScene != nullptr ? pScene->getBodyMass( getBodyHandle() ) : 0.0f;
    }

    bool RigidBody2DComponent::isSleeping() const
    {
        const IPhysicsBodyScene2D* pScene = findScene();
        return pScene != nullptr && pScene->isBodySleeping( getBodyHandle() );
    }

    void RigidBody2DComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat2( getLinearVelocity() );
        inoutBuilder.addFloat( getAngularVelocity() );
    }

    void RigidBody2DComponent::createBody( ScenePhysics& physics, IPhysicsBodyScene2D& scene, const float2& position, float32 angle, const float3& scale )
    {
        PhysicsBodyDesc2D desc;
        desc._listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc2D& shape : _listShape )
        {
            desc._listShape.push_back( PhysicsComponent::makeScaledShape( shape, scale ) );
        }
        desc._position              = position;
        desc._rotation              = angle;
        desc._userData              = getOwner()->getObjectID();
        desc._material              = _material;
        desc._mass                  = _mass;
        desc._linearDamping         = _linearDamping;
        desc._angularDamping        = _angularDamping;
        desc._gravityFactor         = _gravityFactor;
        desc._type                  = _bodyType;
        desc._layer                 = physics.resolveLayer( _layer );
        desc._bTrigger              = _bTrigger;
        desc._bContinuous           = _bContinuous;
        desc._bLockRotation         = _bLockRotation;
        desc._bAllowTypeChange      = true;
        const uint32 index          = getPoseIndex();
        _pPosePage->_arrBody[index] = scene.createBody( desc );
        _pPosePage->resetPose( index, position, angle );
        _appliedBodyType         = _bodyType;
        _kinematicStartPosition  = position;
        _kinematicTargetPosition = position;
        _kinematicStartAngle     = angle;
        _kinematicTargetAngle    = angle;
        _bRestPoseWritten        = false;
        markShapeScale( scale );
        physics.bindBodyOwner( *this );
    }

    PhysicsBodyHandle RigidBody2DComponent::applyScaledShape( ScenePhysics& physics, IPhysicsBodyScene2D& scene )
    {
        // 셰이프는 지을 때 배율을 굽는다 — 바디를 다시 지으면 핸들이 바뀌어 닿아 있던 쌍마다 끝 → 시작이 나므로 셰이프만 간다(감사 S-4).
        const PhysicsBodyHandle    body = _pPosePage->_arrBody[getPoseIndex()];
        vector<PhysicsShapeDesc2D> listShape;
        listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc2D& shape : _listShape )
        {
            listShape.push_back( PhysicsComponent::makeScaledShape( shape, _worldScale ) );
        }
        if ( scene.setBodyShape( body, span<const PhysicsShapeDesc2D>{ listShape.data(), listShape.size() }, _material, _mass ) == false )
        {
            releasePhysics( physics );
            return PhysicsBodyHandle{};
        }
        markShapeScale( _worldScale );
        return body;
    }

    bool RigidBody2DComponent::readWorldPose2D( float2& outPosition, float32& outAngle )
    {
        if ( PhysicsComponent::readWorldPose( *this, _worldPosition, _worldRotation, _worldScale ) == false )
            return false;
        outPosition = float2{ _worldPosition._x, _worldPosition._y };
        outAngle    = PhysicsComponent::getAngle2D( _worldRotation );
        return true;
    }

    void RigidBody2DComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsBodyScene2D* pScene = physics.getScene2D();
        if ( pScene == nullptr )
            return;
        if ( consumeRebuild() )
            releasePhysics( physics );

        // 트랜스폼의 바깥 변화는 칸의 변경 번호로 안다 — 같으면 월드 행렬을 풀지 않는다(3D 강체와 같은 규칙).
        const bool         bTransformChanged = consumeTransformChange();
        PhysicsBodyPage2D& page              = *_pPosePage;
        const uint32       index             = getPoseIndex();
        PhysicsBodyHandle  body              = page._arrBody[index];
        float2             position{};
        float32            angle = 0.0f;
        // 바디는 이 컴포넌트만 지운다(씬을 내릴 때는 `ScenePhysics::onSceneCleared` 가 먼저 놓게 한다) — 프레임마다 백엔드에 묻지 않고 Debug 에서만 확인한다.
        SW_ASSERT( body.isValid() == false || pScene->isBodyValid( body ) );
        if ( body.isValid() == false )
        {
            if ( readWorldPose2D( position, angle ) == false )
                return; // 배율 0 — 바디를 만들지 않는다(`readWorldPose` 가 알렸다). 배율이 돌아오면 이 길로 만든다
            createBody( physics, *pScene, position, angle, _worldScale );
            (void)consumeTeleport();
            return;
        }

        if ( _appliedBodyType != _bodyType )
        {
            pScene->setBodyType( body, _bodyType );
            _appliedBodyType = _bodyType;
        }

        const bool bTeleported = consumeTeleport();
        const bool bMoved      = bTeleported || bTransformChanged;
        if ( bMoved && readWorldPose2D( position, angle ) == false )
        {
            releasePhysics( physics ); // 배율 0 — 셰이프를 지을 수 없어 시뮬레이션에서 뺀다(3D 강체와 같다)
            return;
        }
        if ( isShapeScaleStale( _worldScale ) )
        {
            body = applyScaledShape( physics, *pScene );
            if ( body.isValid() == false )
                return;
        }
        if ( _bodyType == PhysicsBodyType::Kinematic && bTeleported == false )
        {
            _kinematicStartPosition = page._arrCurrentPosition[index];
            _kinematicStartAngle    = page._arrCurrentRotation[index];
            if ( bMoved )
            {
                _kinematicTargetPosition = position;
                _kinematicTargetAngle    = angle;
            }
        }
        else if ( bMoved )
        {
            pScene->setBodyTransform( body, position, angle );
            page.resetPose( index, position, angle );
            _kinematicStartPosition  = position;
            _kinematicTargetPosition = position;
            _kinematicStartAngle     = angle;
            _kinematicTargetAngle    = angle;
        }

        PhysicsBodyCommand<PhysicsDimension2D> command;
        page.editCommand( index, [&command]( PhysicsBodyCommand<PhysicsDimension2D>& pending )
        {
            command                      = pending;
            pending._impulse             = float2{};
            pending._bSetLinearVelocity  = false;
            pending._bSetAngularVelocity = false;
            pending._bWake               = false;
        } );
        if ( command._bSetLinearVelocity )
            pScene->setLinearVelocity( body, command._linearVelocity );
        if ( command._bSetAngularVelocity )
            pScene->setAngularVelocity( body, command._angularVelocity );
        if ( command._impulse.getLengthSquared() > 0.0f )
            pScene->addImpulse( body, command._impulse );
        if ( command._bWake )
            pScene->wakeBody( body );
    }

    void RigidBody2DComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        IPhysicsBodyScene2D*    pScene = physics.findScene2D();
        const uint32            index  = getPoseIndex();
        const PhysicsBodyHandle body   = _pPosePage->_arrBody[index];
        if ( pScene == nullptr || body.isValid() == false )
            return;
        if ( _bodyType == PhysicsBodyType::Kinematic )
        {
            // 서 있는 키네마틱 바디는 스텝마다 백엔드를 부르지 않는다(3D 강체와 같은 규칙).
            const bool bResting = _kinematicStartPosition == _kinematicTargetPosition && _kinematicStartAngle == _kinematicTargetAngle &&
                                  _pPosePage->_arrMovedStep[index] != physics.getStepCount();
            if ( bResting )
                return;
            const float32 fraction = static_cast<float32>( stepIndex + 1 ) / static_cast<float32>( stepCount );
            const float2  target   = float2::lerp( _kinematicStartPosition, _kinematicTargetPosition, fraction );
            const float32 angle    = MathUtil::lerpAngle( _kinematicStartAngle, _kinematicTargetAngle, fraction );
            pScene->moveKinematic( body, target, angle, fixedDeltaTime );
            return;
        }
        if ( _bodyType != PhysicsBodyType::Dynamic )
            return;
        float2  force{};
        float32 torque = 0.0f;
        {
            ScopedLock<SpinLock> lock{ _pPosePage->_arrCommandLock[index] };
            force  = _pPosePage->_arrCommand[index]._force;
            torque = _pPosePage->_arrCommand[index]._torque;
        }
        if ( force.getLengthSquared() > 0.0f )
            pScene->addForce( body, force );
        if ( torque != 0.0f )
            pScene->addTorque( body, torque );
    }

    void RigidBody2DComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        PhysicsBodyPage2D& page  = *_pPosePage;
        const uint32       index = getPoseIndex();
        page.editCommand( index, []( PhysicsBodyCommand<PhysicsDimension2D>& command )
        {
            command._force  = float2{};
            command._torque = 0.0f;
        } );
        if ( page._arrBody[index].isValid() == false || _bodyType != PhysicsBodyType::Dynamic )
            return;
        // 마지막 스텝에 잠들어 있었으면 보간할 사이가 없다 — 그 자세를 한 번 쓴 뒤로는 깰 때까지 쓰지 않는다(3D 강체와 같은 규칙).
        const bool bMovedLastStep = page._arrMovedStep[index] == physics.getStepCount();
        if ( bMovedLastStep == false )
        {
            page.holdPose( index );
            if ( _bRestPoseWritten )
                return;
        }
        const float32 weight   = _bInterpolate ? alpha : 1.0f;
        const float2  position = float2::lerp( page._arrPreviousPosition[index], page._arrCurrentPosition[index], weight );
        const float32 angle    = MathUtil::lerpAngle( page._arrPreviousRotation[index], page._arrCurrentRotation[index], weight );
        // Z 와 Z 둘레 밖의 회전은 트랜스폼의 것이다 — 변경 번호가 같으면 지난번에 읽은 값 그대로다.
        const float3     worldPosition{ position._x, position._y, _worldPosition._z };
        const quaternion worldRotation = replaceRigidBody2DAngle( _worldRotation, angle );
        _bRestPoseWritten              = bMovedLastStep == false;
        // 모아 한 번에 쓴다(3D 강체와 같다) — 적용 · 변경 번호 맞추기는 `ScenePhysics::queuePoseWrite` 가 한다.
        physics.queuePoseWrite( *this, worldPosition, worldRotation, _worldScale );
    }

    void RigidBody2DComponent::releasePhysics( ScenePhysics& physics )
    {
        PhysicsBodyHandle& body = _pPosePage->_arrBody[getPoseIndex()];
        if ( body.isValid() == false )
            return;
        physics.unbindBodyOwner( *this );
        IPhysicsBodyScene2D* pScene = physics.findScene2D();
        if ( pScene != nullptr )
            pScene->destroyBody( body );
        body = PhysicsBodyHandle{};
    }
} // namespace sw

namespace sw
{
    void RigidBody2DComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void RigidBody2DComponent::validateMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_material", _material );
    }
} // namespace sw
