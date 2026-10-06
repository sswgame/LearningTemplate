#include "pch.h"

#include "Engine/Object/Component/Physics/WheeledVehicleComponent.h"

#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"

namespace sw
{
    WheeledVehicleComponent::WheeledVehicleComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint }
        , _wheelRadius{ 0.35f }
        , _wheelWidth{ 0.25f }
        , _halfTrack{ 0.85f }
        , _frontAxle{ 1.3f }
        , _rearAxle{ -1.3f }
        , _wheelMountHeight{ -0.2f }
        , _suspensionMinLength{ 0.1f }
        , _suspensionMaxLength{ 0.35f }
        , _suspensionFrequency{ 1.5f }
        , _suspensionDamping{ 0.5f }
        , _maxSteerAngle{ 0.52f }
        , _maxBrakeTorque{ 1500.0f }
        , _maxHandBrakeTorque{ 4000.0f }
        , _engineMaxTorque{ 500.0f }
        , _engineMaxRpm{ 6000.0f }
        , _drive{ WheelDrive::Rear }
        , _inputLock{}
        , _inputForward{ 0.0f }
        , _inputRight{ 0.0f }
        , _inputBrake{ 0.0f }
        , _inputHandBrake{ 0.0f }
        , _state{}
        , _vehicle{}
        , _vehicleChassis{}
    {
    }

    void WheeledVehicleComponent::setDriverInput( float32 forward, float32 right, float32 brake, float32 handBrake )
    {
        std::scoped_lock<SpinLock> lock{ _inputLock };
        _inputForward   = forward;
        _inputRight     = right;
        _inputBrake     = brake;
        _inputHandBrake = handBrake;
    }

    PhysicsVehicleState WheeledVehicleComponent::getVehicleState() const
    {
        return _state;
    }

    PhysicsWheeledVehicleDesc WheeledVehicleComponent::makeVehicleDesc() const
    {
        PhysicsWheeledVehicleDesc desc;
        desc._engineMaxTorque = _engineMaxTorque;
        desc._engineMaxRpm    = _engineMaxRpm;
        // 앞 왼쪽 · 앞 오른쪽 · 뒤 왼쪽 · 뒤 오른쪽(왼쪽이 −X).
        const float32 arrAxle[2] = { _frontAxle, _rearAxle };
        const float32 arrSide[2] = { -_halfTrack, _halfTrack };
        for ( uint32 axleIndex = 0; axleIndex < 2; ++axleIndex )
        {
            for ( const float32 side : arrSide )
            {
                PhysicsWheelDesc wheel;
                wheel._position            = float3{ side, _wheelMountHeight, arrAxle[axleIndex] };
                wheel._radius              = _wheelRadius;
                wheel._width               = _wheelWidth;
                wheel._suspensionMinLength = _suspensionMinLength;
                wheel._suspensionMaxLength = _suspensionMaxLength;
                wheel._suspensionFrequency = _suspensionFrequency;
                wheel._suspensionDamping   = _suspensionDamping;
                wheel._maxSteerAngle       = axleIndex == 0 ? _maxSteerAngle : 0.0f;
                wheel._maxBrakeTorque      = _maxBrakeTorque;
                wheel._maxHandBrakeTorque  = axleIndex == 0 ? 0.0f : _maxHandBrakeTorque;
                desc._listWheel.push_back( wheel );
            }
        }
        if ( _drive != WheelDrive::Rear )
            desc._listDrivenAxle.insert( desc._listDrivenAxle.end(), { 0, 1 } );
        if ( _drive != WheelDrive::Front )
            desc._listDrivenAxle.insert( desc._listDrivenAxle.end(), { 2, 3 } );
        return desc;
    }

    void WheeledVehicleComponent::beginPhysicsFrame( ScenePhysics& physics )
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
        const RigidBodyComponent* pBody   = getOwner()->getComponent<RigidBodyComponent>();
        const PhysicsBodyHandle   chassis = pBody != nullptr ? pBody->getBodyHandle() : PhysicsBodyHandle{};
        // 차체가 다시 지어졌거나 사라졌으면 차를 다시 짓는다(차체를 지우면 백엔드가 차도 지운다).
        if ( _vehicle.isValid() && ( chassis != _vehicleChassis || pScene->isVehicleValid( _vehicle ) == false ) )
            releasePhysics( physics );
        if ( _vehicle.isValid() == false && pScene->isBodyValid( chassis ) )
        {
            _vehicle        = pScene->createWheeledVehicle( chassis, makeVehicleDesc() );
            _vehicleChassis = chassis;
        }
        if ( _vehicle.isValid() == false )
            return;
        float32 forward   = 0.0f;
        float32 right     = 0.0f;
        float32 brake     = 0.0f;
        float32 handBrake = 0.0f;
        {
            std::scoped_lock<SpinLock> lock{ _inputLock };
            forward   = _inputForward;
            right     = _inputRight;
            brake     = _inputBrake;
            handBrake = _inputHandBrake;
        }
        pScene->setVehicleInput( _vehicle, forward, right, brake, handBrake );
    }

    void WheeledVehicleComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)alpha;
        const IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr && _vehicle.isValid() )
            (void)pScene->getVehicleState( _vehicle, _state );
    }

    void WheeledVehicleComponent::releasePhysics( ScenePhysics& physics )
    {
        if ( _vehicle.isValid() )
        {
            IPhysicsScene3D* pScene = physics.findScene3D();
            if ( pScene != nullptr )
                pScene->destroyVehicle( _vehicle );
        }
        _vehicle        = PhysicsVehicleHandle{};
        _vehicleChassis = PhysicsBodyHandle{};
        _state          = PhysicsVehicleState{};
    }
} // namespace sw
