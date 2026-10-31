#include "pch.h"

#include "Engine/Physics/Jolt/JoltVehicleSet.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Physics/Jolt/JoltUtil.h"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

namespace sw
{
    SW_LOG_CALLER( "JoltPhysicsScene" );

    namespace
    {
        struct JoltVehicleSetInternal
        {
            /** @brief 바퀴 충돌 원기둥의 볼록 반지름 비율입니다(Jolt 권장값). */
            static constexpr float32 kCylinderConvexRadiusFraction = 0.1f;
        };
    } // namespace

    PhysicsVehicleHandle JoltVehicleSet::create( PhysicsBodyHandle chassis, const JPH::BodyID& chassisBodyID, JPH::ObjectLayer testerLayer, const PhysicsWheeledVehicleDesc& desc )
    {
        const bool bHasDrivenAxle = desc._listDrivenAxle.size() >= 2 && ( desc._listDrivenAxle.size() % 2 ) == 0;
        if ( desc._listWheel.empty() || bHasDrivenAxle == false )
        {
            SW_LOG_ERROR( "Wheeled vehicle needs a dynamic chassis body, wheels and driven axle pairs" );
            return PhysicsVehicleHandle{};
        }
        const int32 wheelCount = static_cast<int32>( desc._listWheel.size() );
        for ( const int32 wheelIndex : desc._listDrivenAxle )
        {
            if ( wheelIndex < 0 || wheelCount <= wheelIndex )
            {
                SW_LOG_ERROR( "Wheeled vehicle driven axle names wheel %# of %#", wheelIndex, wheelCount );
                return PhysicsVehicleHandle{};
            }
        }

        JPH::VehicleConstraintSettings settings;
        settings.mUp                = JPH::Vec3::sAxisY();
        settings.mForward           = JPH::Vec3::sAxisZ();
        settings.mMaxPitchRollAngle = desc._maxPitchRollAngle;
        settings.mWheels.reserve( desc._listWheel.size() );
        for ( const PhysicsWheelDesc& wheel : desc._listWheel )
        {
            JPH::WheelSettingsWV* pWheel         = JoltUtil::createObject<JPH::WheelSettingsWV>(); // Jolt 참조 카운트 객체다. Ref 가 해제한다.
            pWheel->mPosition                    = JoltUtil::toJolt( wheel._position );
            pWheel->mSuspensionDirection         = JPH::Vec3( 0.0f, -1.0f, 0.0f );
            pWheel->mSteeringAxis                = JPH::Vec3::sAxisY();
            pWheel->mWheelUp                     = JPH::Vec3::sAxisY();
            pWheel->mWheelForward                = JPH::Vec3::sAxisZ();
            pWheel->mRadius                      = wheel._radius;
            pWheel->mWidth                       = wheel._width;
            pWheel->mSuspensionMinLength         = wheel._suspensionMinLength;
            pWheel->mSuspensionMaxLength         = wheel._suspensionMaxLength;
            pWheel->mSuspensionSpring.mFrequency = wheel._suspensionFrequency;
            pWheel->mSuspensionSpring.mDamping   = wheel._suspensionDamping;
            pWheel->mMaxSteerAngle               = wheel._maxSteerAngle;
            pWheel->mMaxBrakeTorque              = wheel._maxBrakeTorque;
            pWheel->mMaxHandBrakeTorque          = wheel._maxHandBrakeTorque;
            settings.mWheels.push_back( pWheel );
        }
        JPH::WheeledVehicleControllerSettings* pController = JoltUtil::createObject<JPH::WheeledVehicleControllerSettings>();
        pController->mEngine.mMaxTorque                    = desc._engineMaxTorque;
        pController->mEngine.mMinRPM                       = desc._engineMinRpm;
        pController->mEngine.mMaxRPM                       = desc._engineMaxRpm;
        pController->mDifferentials.resize( desc._listDrivenAxle.size() / 2 );
        for ( size_t axleIndex = 0; axleIndex < pController->mDifferentials.size(); ++axleIndex )
        {
            pController->mDifferentials[axleIndex].mLeftWheel  = desc._listDrivenAxle[axleIndex * 2];
            pController->mDifferentials[axleIndex].mRightWheel = desc._listDrivenAxle[axleIndex * 2 + 1];
        }
        // 구동 토크는 차축마다 고르게.
        for ( JPH::VehicleDifferentialSettings& differential : pController->mDifferentials )
        {
            differential.mEngineTorqueRatio = 1.0f / static_cast<float32>( pController->mDifferentials.size() );
        }
        settings.mController = pController;

        Record record;
        {
            JPH::BodyLockWrite lock( _system.GetBodyLockInterface(), chassisBodyID );
            if ( lock.Succeeded() == false )
                return PhysicsVehicleHandle{};
            record._pConstraint = JoltUtil::createObject<JPH::VehicleConstraint>( lock.GetBody(), settings );
        }
        record._pTester = JoltUtil::createObject<JPH::VehicleCollisionTesterCastCylinder>( testerLayer, JoltVehicleSetInternal::kCylinderConvexRadiusFraction );
        record._pConstraint->SetVehicleCollisionTester( record._pTester );
        record._chassis       = chassis;
        record._chassisBodyID = chassisBodyID;
        _system.AddConstraint( record._pConstraint );
        _system.AddStepListener( record._pConstraint );
        return PhysicsVehicleHandle::fromSlot( _vehicles.insert( std::move( record ) ) );
    }

    void JoltVehicleSet::remove( Record& record )
    {
        if ( record._pConstraint == nullptr )
            return;
        _system.RemoveStepListener( record._pConstraint );
        _system.RemoveConstraint( record._pConstraint );
        record._pConstraint = nullptr;
    }

    PhysicsBodyHandle JoltVehicleSet::destroy( PhysicsVehicleHandle vehicle )
    {
        Record record;
        if ( _vehicles.take( vehicle.getSlot(), record ) == false )
            return PhysicsBodyHandle{};
        remove( record );
        return record._chassis;
    }

    bool JoltVehicleSet::isValid( PhysicsVehicleHandle vehicle ) const
    {
        return _vehicles.get( vehicle.getSlot() ) != nullptr;
    }

    PhysicsBodyHandle JoltVehicleSet::setInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake )
    {
        Record* pRecord = _vehicles.get( vehicle.getSlot() );
        if ( pRecord == nullptr || pRecord->_pConstraint == nullptr )
            return PhysicsBodyHandle{};
        JPH::WheeledVehicleController* pController = static_cast<JPH::WheeledVehicleController*>( pRecord->_pConstraint->GetController() );
        // 엔진은 요 0 에서 앞 +Z, 오른쪽 +X(왼손)다. Jolt 의 오른쪽 조향(+1)은 오른손 좌표의 오른쪽(위 +Y, 앞 +Z 에서 -X)이라 부호를 바꿔서 넘긴다.
        pController->SetDriverInput( MathUtil::clamp( forward, -1.0f, 1.0f ), -MathUtil::clamp( right, -1.0f, 1.0f ), MathUtil::clamp( brake, 0.0f, 1.0f ),
                                     MathUtil::clamp( handBrake, 0.0f, 1.0f ) );
        const bool bHasInput = forward != 0.0f || right != 0.0f || brake != 0.0f || handBrake != 0.0f;
        return bHasInput ? pRecord->_chassis : PhysicsBodyHandle{};
    }

    bool JoltVehicleSet::getState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const
    {
        outState              = PhysicsVehicleState{};
        const Record* pRecord = _vehicles.get( vehicle.getSlot() );
        if ( pRecord == nullptr || pRecord->_pConstraint == nullptr )
            return false;
        {
            JPH::BodyLockRead lock( _system.GetBodyLockInterface(), pRecord->_chassisBodyID );
            if ( lock.Succeeded() == false )
                return false;
            const JPH::Body& body    = lock.GetBody();
            const JPH::Vec3  forward = body.GetRotation() * JPH::Vec3::sAxisZ();
            outState._forwardSpeed   = body.GetLinearVelocity().Dot( forward );
        }
        const JPH::WheeledVehicleController* pController = static_cast<const JPH::WheeledVehicleController*>( pRecord->_pConstraint->GetController() );
        outState._engineRpm                              = pController->GetEngine().GetCurrentRPM();
        outState._gear                                   = pController->GetTransmission().GetCurrentGear();
        const JPH::Array<JPH::Wheel*>& listWheel         = pRecord->_pConstraint->GetWheels();
        for ( uint32 wheelIndex = 0; wheelIndex < listWheel.size(); ++wheelIndex )
        {
            outState._groundedWheelCount += listWheel[wheelIndex]->HasContact() ? 1u : 0u;
            if ( wheelIndex >= kPhysicsMaxWheelPoseCount )
                continue;
            // 바퀴 로컬 변환은 차체 기준이다. 바퀴 축은 X(바퀴 오른쪽), 위는 Y 로 만들었다(`create` 의 `mWheelUp` · `mWheelForward`).
            const JPH::Mat44 wheelTransform              = pRecord->_pConstraint->GetWheelLocalTransform( wheelIndex, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY() );
            outState._arrWheelPose[wheelIndex]._position = JoltUtil::toEngine( wheelTransform.GetTranslation() );
            outState._arrWheelPose[wheelIndex]._rotation = JoltUtil::toEngine( wheelTransform.GetQuaternion() );
            outState._wheelCount                         = wheelIndex + 1;
        }
        return true;
    }

    void JoltVehicleSet::destroyAttachedTo( span<const PhysicsBodyHandle> listBody )
    {
        vector<PhysicsVehicleHandle> listDoomed;
        _vehicles.forEachHandle( [&]( SlotHandle slot, const Record& record )
        {
            for ( const PhysicsBodyHandle& body : listBody )
            {
                if ( record._chassis == body )
                {
                    listDoomed.push_back( PhysicsVehicleHandle::fromSlot( slot ) );
                    return;
                }
            }
        } );
        for ( const PhysicsVehicleHandle& vehicle : listDoomed )
        {
            (void)destroy( vehicle ); // 차체도 지워지므로 깨우지 않는다
        }
    }

    void JoltVehicleSet::clear()
    {
        _vehicles.forEach( [this]( Record& record )
        { remove( record ); } );
        _vehicles.clear();
    }
} // namespace sw
