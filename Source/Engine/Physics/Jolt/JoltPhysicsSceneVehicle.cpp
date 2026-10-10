/**
 * @file JoltPhysicsSceneVehicle.cpp
 * @brief `JoltPhysicsScene` 의 바퀴 차입니다 — `VehicleConstraint` + `WheeledVehicleController`(엔진 · 자동 변속 · 디퍼렌셜) + 원기둥 쓸기 바퀴 충돌.
 * @details 차는 구속이면서 스텝 리스너입니다(바퀴 충돌 · 서스펜션은 스텝 앞에서 푼다) — 만들 때 둘 다 넣고 지울 때 둘 다 뺍니다. 차체 바디를 지우면
 *          그 차도 지웁니다(`destroyJointsOf`). 같은 입력 · 같은 스텝이면 같은 결과입니다(Jolt 결정성 — 같은 실행 안에서).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Physics/Jolt/JoltPhysicsScene.h"
#include "Engine/Physics/Jolt/JoltUtil.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

namespace sw
{
    SW_LOG_CALLER( "JoltPhysicsScene" );

    namespace
    {
        struct JoltPhysicsSceneVehicleInternal
        {
            /** @brief 바퀴 충돌 원기둥의 볼록 반지름 비율입니다(Jolt 권장값). */
            static constexpr float32 kCylinderConvexRadiusFraction = 0.1f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    PhysicsVehicleHandle JoltPhysicsScene::createWheeledVehicle( PhysicsBodyHandle chassis, const PhysicsWheeledVehicleDesc& desc )
    {
        using Internal                   = JoltPhysicsSceneVehicleInternal;
        const BodyRecord* pChassis       = findBody( chassis );
        const bool        bHasDrivenAxle = desc._listDrivenAxle.size() >= 2 && ( desc._listDrivenAxle.size() % 2 ) == 0;
        if ( pChassis == nullptr || pChassis->_type != PhysicsBodyType::Dynamic || desc._listWheel.empty() || bHasDrivenAxle == false )
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
            JPH::WheelSettingsWV* pWheel         = JoltUtil::createObject<JPH::WheelSettingsWV>(); // Jolt 참조 셈 객체 — Ref 가 놓는다
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

        VehicleRecord record;
        {
            JPH::BodyLockWrite lock( _system.GetBodyLockInterface(), pChassis->_bodyID );
            if ( lock.Succeeded() == false )
                return PhysicsVehicleHandle{};
            record._pConstraint = JoltUtil::createObject<JPH::VehicleConstraint>( lock.GetBody(), settings );
        }
        record._pTester = JoltUtil::createObject<JPH::VehicleCollisionTesterCastCylinder>( JoltLayerUtil::makeObjectLayer( pChassis->_layer, true ), Internal::kCylinderConvexRadiusFraction );
        record._pConstraint->SetVehicleCollisionTester( record._pTester );
        record._chassis = chassis;
        _system.AddConstraint( record._pConstraint );
        _system.AddStepListener( record._pConstraint );
        return PhysicsVehicleHandle::fromSlot( _vehicles.insert( std::move( record ) ) );
    }

    void JoltPhysicsScene::removeVehicleConstraint( VehicleRecord& record )
    {
        if ( record._pConstraint == nullptr )
            return;
        _system.RemoveStepListener( record._pConstraint );
        _system.RemoveConstraint( record._pConstraint );
        record._pConstraint = nullptr;
    }

    void JoltPhysicsScene::destroyVehicle( PhysicsVehicleHandle vehicle )
    {
        VehicleRecord record;
        if ( _vehicles.take( vehicle.getSlot(), record ) == false )
            return;
        removeVehicleConstraint( record );
        wakeBody( record._chassis );
    }

    bool JoltPhysicsScene::isVehicleValid( PhysicsVehicleHandle vehicle ) const
    {
        return _vehicles.get( vehicle.getSlot() ) != nullptr;
    }

    void JoltPhysicsScene::setVehicleInput( PhysicsVehicleHandle vehicle, float32 forward, float32 right, float32 brake, float32 handBrake )
    {
        VehicleRecord* pRecord = _vehicles.get( vehicle.getSlot() );
        if ( pRecord == nullptr || pRecord->_pConstraint == nullptr )
            return;
        JPH::WheeledVehicleController* pController = static_cast<JPH::WheeledVehicleController*>( pRecord->_pConstraint->GetController() );
        // 엔진은 요 0 에서 앞 +Z · 오른쪽 +X(왼손)다. Jolt 의 오른쪽 조향(+1)은 오른손 좌표의 오른쪽(위 +Y · 앞 +Z 에서 −X)이라 부호를 바꿔 넘긴다.
        pController->SetDriverInput( MathUtil::clamp( forward, -1.0f, 1.0f ), -MathUtil::clamp( right, -1.0f, 1.0f ), MathUtil::clamp( brake, 0.0f, 1.0f ),
                                     MathUtil::clamp( handBrake, 0.0f, 1.0f ) );
        // 입력이 있으면 자는 차체를 깨운다(자는 바디에는 구속이 돌지 않는다).
        const bool bHasInput = forward != 0.0f || right != 0.0f || brake != 0.0f || handBrake != 0.0f;
        if ( bHasInput )
            wakeBody( pRecord->_chassis );
    }

    bool JoltPhysicsScene::getVehicleState( PhysicsVehicleHandle vehicle, PhysicsVehicleState& outState ) const
    {
        outState                     = PhysicsVehicleState{};
        const VehicleRecord* pRecord = _vehicles.get( vehicle.getSlot() );
        const BodyRecord*    pBody   = pRecord != nullptr ? findBody( pRecord->_chassis ) : nullptr;
        if ( pBody == nullptr || pRecord->_pConstraint == nullptr )
            return false;
        {
            JPH::BodyLockRead lock( _system.GetBodyLockInterface(), pBody->_bodyID );
            if ( lock.Succeeded() )
            {
                const JPH::Body& body    = lock.GetBody();
                const JPH::Vec3  forward = body.GetRotation() * JPH::Vec3::sAxisZ();
                outState._forwardSpeed   = body.GetLinearVelocity().Dot( forward );
            }
        }
        const JPH::WheeledVehicleController* pController = static_cast<const JPH::WheeledVehicleController*>( pRecord->_pConstraint->GetController() );
        outState._engineRpm                              = pController->GetEngine().GetCurrentRPM();
        outState._gear                                   = pController->GetTransmission().GetCurrentGear();
        for ( const JPH::Wheel* pWheel : pRecord->_pConstraint->GetWheels() )
        {
            outState._groundedWheelCount += pWheel->HasContact() ? 1u : 0u;
        }
        return true;
    }
} // namespace sw
