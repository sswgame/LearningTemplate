#include "pch.h"

#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/PhysicsPoseStore.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
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
        , _pPosePage{ nullptr }
        , _poseSlot{ PhysicsPoseStore::kInvalidSlot }
        , _pendingField{}
        , _frameField{}
        , _worldScale{ 1.0f, 1.0f, 1.0f }
        , _kinematicStartPosition{}
        , _kinematicTargetPosition{}
        , _kinematicStartRotation{}
        , _kinematicTargetRotation{}
        , _appliedBodyType{ PhysicsBodyType::Dynamic }
        , _bRestPoseWritten{ false }
    {
        _poseSlot = engine::getSubsystem<PhysicsPoseStore>().getStore<PhysicsDimension3D>().allocateSlot( this, _pPosePage );
    }

    RigidBodyComponent::~RigidBodyComponent()
    {
        engine::getSubsystem<PhysicsPoseStore>().getStore<PhysicsDimension3D>().freeSlot( _poseSlot );
    }

    PhysicsBodyHandle RigidBodyComponent::getBodyHandle() const
    {
        return _pPosePage->_arrBody[getPoseIndex()];
    }

    float3 RigidBodyComponent::getLinearVelocity() const
    {
        return _pPosePage->_arrLinearVelocity[getPoseIndex()];
    }

    float3 RigidBodyComponent::getAngularVelocity() const
    {
        return _pPosePage->_arrAngularVelocity[getPoseIndex()];
    }

    uint32 RigidBodyComponent::getPoseIndex() const
    {
        return _poseSlot & PhysicsBodyPage3D::kSlotMask;
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
        _pPosePage->editCommand( getPoseIndex(), [&force]( PhysicsBodyCommand<PhysicsDimension3D>& command )
        { command._force += force; } );
    }

    void RigidBodyComponent::addImpulse( const float3& impulse )
    {
        _pPosePage->editCommand( getPoseIndex(), [&impulse]( PhysicsBodyCommand<PhysicsDimension3D>& command )
        { command._impulse += impulse; } );
    }

    void RigidBodyComponent::addTorque( const float3& torque )
    {
        _pPosePage->editCommand( getPoseIndex(), [&torque]( PhysicsBodyCommand<PhysicsDimension3D>& command )
        { command._torque += torque; } );
    }

    void RigidBodyComponent::setLinearVelocity( const float3& velocity )
    {
        _pPosePage->editCommand( getPoseIndex(), [&velocity]( PhysicsBodyCommand<PhysicsDimension3D>& command )
        {
            command._linearVelocity     = velocity;
            command._bSetLinearVelocity = true;
        } );
    }

    void RigidBodyComponent::setAngularVelocity( const float3& velocity )
    {
        _pPosePage->editCommand( getPoseIndex(), [&velocity]( PhysicsBodyCommand<PhysicsDimension3D>& command )
        {
            command._angularVelocity     = velocity;
            command._bSetAngularVelocity = true;
        } );
    }

    void RigidBodyComponent::wake()
    {
        _pPosePage->editCommand( getPoseIndex(), []( PhysicsBodyCommand<PhysicsDimension3D>& command )
        { command._bWake = true; } );
    }

    float32 RigidBodyComponent::getBodyMass() const
    {
        const IPhysicsBodyScene3D* pScene = findScene();
        return pScene != nullptr ? pScene->getBodyMass( getBodyHandle() ) : 0.0f;
    }

    void RigidBodyComponent::applyPhysicsField( const PhysicsFieldSample& sample )
    {
        // 필드도 명령과 같은 칸 잠금 아래다(`_pendingField` 설명).
        ScopedLock<SpinLock> lock{ _pPosePage->_arrCommandLock[getPoseIndex()] };
        _pendingField.combine( sample );
    }

    float32 RigidBodyComponent::computeRelativeDensity( const ScenePhysics& physics ) const
    {
        const PhysicsSettings* pSettings = physics.findSettings();
        if ( pSettings == nullptr || pSettings->_listMaterial.empty() )
            return 1.0f;
        const PhysicsMaterialDef* pMaterial = _material.empty() ? &pSettings->_listMaterial.front() : pSettings->findMaterial( _material );
        return pMaterial != nullptr ? pMaterial->_density / PhysicsMaterialDef::kWaterDensity : 1.0f;
    }

    bool RigidBodyComponent::isSleeping() const
    {
        const IPhysicsBodyScene3D* pScene = findScene();
        return pScene != nullptr && pScene->isBodySleeping( getBodyHandle() );
    }

    void RigidBodyComponent::foldStateHash( StateHashBuilder& inoutBuilder ) const
    {
        PhysicsComponent::foldStateHash( inoutBuilder );
        inoutBuilder.addFloat3( getLinearVelocity() );
        inoutBuilder.addFloat3( getAngularVelocity() );
    }

    PhysicsBodyDesc3D RigidBodyComponent::makeBodyDesc( const ScenePhysics& physics, const float3& position, const quaternion& rotation, const float3& scale ) const
    {
        PhysicsBodyDesc3D desc;
        desc._listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc3D& shape : _listShape )
        {
            desc._listShape.push_back( PhysicsComponent::makeScaledShape( shape, scale ) );
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
        PhysicsComponent::readWorldPose( *this, position, rotation, scale );
        outListBody.push_back( makeBodyDesc( physics, position, rotation, scale ) );
    }

    void RigidBodyComponent::createBody( ScenePhysics& physics, IPhysicsBodyScene3D& scene, const float3& position, const quaternion& rotation, const float3& scale )
    {
        const uint32 index          = getPoseIndex();
        _pPosePage->_arrBody[index] = scene.createBody( makeBodyDesc( physics, position, rotation, scale ) );
        _pPosePage->resetPose( index, position, rotation );
        _appliedBodyType         = _bodyType;
        _kinematicStartPosition  = position;
        _kinematicStartRotation  = rotation;
        _kinematicTargetPosition = position;
        _kinematicTargetRotation = rotation;
        _bRestPoseWritten        = false;
        markShapeScale( scale );
        physics.bindBodyOwner( *this );
    }

    PhysicsBodyHandle RigidBodyComponent::applyScaledShape( ScenePhysics& physics, IPhysicsBodyScene3D& scene )
    {
        // 셰이프는 지을 때 배율을 굽는다 — 바디를 다시 지으면 핸들이 바뀌어 닿아 있던 쌍마다 끝 → 시작이 나므로 셰이프만 간다(감사 S-4).
        const PhysicsBodyHandle    body = _pPosePage->_arrBody[getPoseIndex()];
        vector<PhysicsShapeDesc3D> listShape;
        listShape.reserve( _listShape.size() );
        for ( const PhysicsShapeDesc3D& shape : _listShape )
        {
            listShape.push_back( PhysicsComponent::makeScaledShape( shape, _worldScale ) );
        }
        if ( scene.setBodyShape( body, span<const PhysicsShapeDesc3D>{ listShape.data(), listShape.size() }, _material, _mass ) == false )
        {
            releasePhysics( physics );
            return PhysicsBodyHandle{};
        }
        markShapeScale( _worldScale );
        return body;
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
        const bool         bTransformChanged = consumeTransformChange();
        PhysicsBodyPage3D& page              = *_pPosePage;
        const uint32       index             = getPoseIndex();
        PhysicsBodyHandle  body              = page._arrBody[index];
        float3             position{};
        quaternion         rotation{};
        // 바디는 이 컴포넌트만 지운다(씬을 내릴 때는 `ScenePhysics::onSceneCleared` 가 먼저 놓게 한다) — 프레임마다 백엔드에 묻지 않고 Debug 에서만 확인한다.
        SW_ASSERT( body.isValid() == false || pScene->isBodyValid( body ) );
        if ( body.isValid() == false )
        {
            PhysicsComponent::readWorldPose( *this, position, rotation, _worldScale );
            createBody( physics, *pScene, position, rotation, _worldScale );
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
        if ( bMoved )
            PhysicsComponent::readWorldPose( *this, position, rotation, _worldScale );
        if ( isShapeScaleStale( _worldScale ) )
        {
            body = applyScaledShape( physics, *pScene );
            if ( body.isValid() == false )
                return;
        }
        if ( _bodyType == PhysicsBodyType::Kinematic && bTeleported == false )
        {
            // 목표는 트랜스폼이다 — 옮겨지지 않았으면 지난 목표가 곧 트랜스폼이다.
            _kinematicStartPosition = page._arrCurrentPosition[index];
            _kinematicStartRotation = page._arrCurrentRotation[index];
            if ( bMoved )
            {
                _kinematicTargetPosition = position;
                _kinematicTargetRotation = rotation;
            }
        }
        else if ( bMoved )
        {
            pScene->setBodyTransform( body, position, rotation );
            page.resetPose( index, position, rotation );
            _kinematicStartPosition  = position;
            _kinematicStartRotation  = rotation;
            _kinematicTargetPosition = position;
            _kinematicTargetRotation = rotation;
        }

        PhysicsBodyCommand<PhysicsDimension3D> command;
        {
            ScopedLock<SpinLock>                    lock{ page._arrCommandLock[index] };
            PhysicsBodyCommand<PhysicsDimension3D>& pending = page._arrCommand[index];
            _frameField                                     = _pendingField; // 이번 게임 프레임의 필드 — 스텝이 없는 프레임이 끼어도 두 번 쌓이지 않는다
            _pendingField                                   = PhysicsFieldSample{};
            command                                         = pending;
            pending._impulse                                = float3{};
            pending._bSetLinearVelocity                     = false;
            pending._bSetAngularVelocity                    = false;
            pending._bWake                                  = false;
        }
        if ( command._bSetLinearVelocity )
            pScene->setLinearVelocity( body, command._linearVelocity );
        if ( command._bSetAngularVelocity )
            pScene->setAngularVelocity( body, command._angularVelocity );
        if ( command._impulse.getLengthSquared() > 0.0f )
            pScene->addImpulse( body, command._impulse );
        if ( command._bWake || ( _bodyType == PhysicsBodyType::Dynamic && _frameField.hasEffect() ) )
            pScene->wakeBody( body ); // 잠든 바디는 힘을 받지 않는다 — 필드 안에서는 깨어 있어야 뜨고 흐른다
    }

    void RigidBodyComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        IPhysicsBodyScene3D*    pScene = physics.findScene3D();
        const uint32            index  = getPoseIndex();
        const PhysicsBodyHandle body   = _pPosePage->_arrBody[index];
        if ( pScene == nullptr || body.isValid() == false )
            return;
        if ( _bodyType == PhysicsBodyType::Kinematic )
        {
            // 서 있는 키네마틱 바디(목표가 지금 자리이고 지난 스텝에 잠들어 있었다)는 스텝마다 백엔드를 부르지 않는다. 지난 스텝에 깨어 있었으면
            // 남은 속도를 0 으로 돌리려고 한 번 더 부른다.
            const bool bResting = _kinematicStartPosition == _kinematicTargetPosition && _kinematicStartRotation == _kinematicTargetRotation &&
                                  _pPosePage->_arrMovedStep[index] != physics.getStepCount();
            if ( bResting )
                return;
            // 프레임의 새 자세까지를 스텝 수로 나눠 간다 — 스텝마다 같은 목표로 가면 첫 스텝에 다 가고 나머지는 서 있다.
            const float32    fraction = static_cast<float32>( stepIndex + 1 ) / static_cast<float32>( stepCount );
            const float3     target   = float3::lerp( _kinematicStartPosition, _kinematicTargetPosition, fraction );
            const quaternion rotation = quaternion::slerp( _kinematicStartRotation, _kinematicTargetRotation, fraction );
            pScene->moveKinematic( body, target, rotation, fixedDeltaTime );
            return;
        }
        if ( _bodyType != PhysicsBodyType::Dynamic )
            return;
        float3 force{};
        float3 torque{};
        {
            ScopedLock<SpinLock> lock{ _pPosePage->_arrCommandLock[index] };
            force  = _pPosePage->_arrCommand[index]._force;
            torque = _pPosePage->_arrCommand[index]._torque;
        }
        if ( _frameField.hasEffect() )
            force = force + computeFieldForce( physics, *pScene, fixedDeltaTime );
        if ( force.getLengthSquared() > 0.0f )
            pScene->addForce( body, force );
        if ( torque.getLengthSquared() > 0.0f )
            pScene->addTorque( body, torque );
    }

    float3 RigidBodyComponent::computeFieldForce( const ScenePhysics& physics, IPhysicsBodyScene3D& scene, float32 fixedDeltaTime ) const
    {
        const IPhysicsScene3D* pWorld = physics.findScene3D();
        if ( pWorld == nullptr )
            return float3{};
        const PhysicsFieldSample& field         = _frameField;
        const float3              gravity       = pWorld->getGravity();
        const float32             gravityLength = gravity.getLength();
        const float3              up            = gravityLength > 0.0f ? gravity * ( -1.0f / gravityLength ) : float3{ 0.0f, 1.0f, 0.0f };
        // 백엔드가 이미 중력 × `_gravityFactor` 를 준다 — 필드 배율의 차이만 더한다. 부력은 잠긴 비율 × 유체 밀도 ÷ 바디 밀도만큼 위로 민다.
        float3 acceleration = gravity * ( _gravityFactor * ( field._gravityScale - 1.0f ) ) + field._acceleration;
        acceleration        = acceleration + up * field.computeBuoyancy( gravityLength, computeRelativeDensity( physics ) );
        // 저항은 흐름과의 속도 차를 줄인다. 한 스텝에 차이를 넘겨 줄이지 않게 1/dt 로 자른다(진동하지 않는다).
        if ( field._drag > 0.0f && fixedDeltaTime > 0.0f )
            acceleration = acceleration + ( field._flowVelocity - scene.getLinearVelocity( getBodyHandle() ) ) * MathUtil::min( field._drag, 1.0f / fixedDeltaTime );
        return acceleration * scene.getBodyMass( getBodyHandle() );
    }

    void RigidBodyComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        PhysicsBodyPage3D& page  = *_pPosePage;
        const uint32       index = getPoseIndex();
        // 힘 · 토크는 한 프레임짜리다.
        page.editCommand( index, []( PhysicsBodyCommand<PhysicsDimension3D>& command )
        {
            command._force  = float3{};
            command._torque = float3{};
        } );
        if ( page._arrBody[index].isValid() == false || _bodyType != PhysicsBodyType::Dynamic )
            return;
        // 마지막 스텝에 잠들어 있었으면(움직인 바디 목록에 없었다) 보간할 사이가 없다. 그 자세를 한 번 쓴 뒤로는 깰 때까지 트랜스폼을 건드리지 않는다.
        const bool bMovedLastStep = page._arrMovedStep[index] == physics.getStepCount();
        if ( bMovedLastStep == false )
        {
            page.holdPose( index );
            if ( _bRestPoseWritten )
                return;
        }
        const float32    weight   = _bInterpolate ? alpha : 1.0f;
        const float3     position = float3::lerp( page._arrPreviousPosition[index], page._arrCurrentPosition[index], weight );
        const quaternion rotation = quaternion::slerp( page._arrPreviousRotation[index], page._arrCurrentRotation[index], weight );
        _bRestPoseWritten         = bMovedLastStep == false;
        // 모아 한 번에 쓴다 — 적용 · 변경 번호 맞추기는 `ScenePhysics::queuePoseWrite` 가 한다.
        physics.queuePoseWrite( *this, position, rotation, _worldScale );
    }

    void RigidBodyComponent::releasePhysics( ScenePhysics& physics )
    {
        PhysicsBodyHandle& body = _pPosePage->_arrBody[getPoseIndex()];
        if ( body.isValid() == false )
            return;
        physics.unbindBodyOwner( *this );
        IPhysicsBodyScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr )
            pScene->destroyBody( body );
        body = PhysicsBodyHandle{};
    }
} // namespace sw

namespace sw
{
    void RigidBodyComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void RigidBodyComponent::validateMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_material", _material );
    }
} // namespace sw
