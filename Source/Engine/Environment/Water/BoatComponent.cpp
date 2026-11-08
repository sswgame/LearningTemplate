#include "pch.h"

#include "Engine/Environment/Water/BoatComponent.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    BoatComponent::BoatComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Joint }
        , _maxRudderAngle{ 0.6f }
        , _rudderForce{ 150.0f }
        , _rudderPosition{ 0.0f, -0.3f, -2.8f }
        , _engineThrust{ 0.0f }
        , _sailArea{ 0.0f }
        , _sailEfficiency{}
        , _waterDrag{ 3000.0f, 3000.0f, 150.0f }
        , _yawDrag{ 2000.0f }
        , _input{}
        , _state{}
        , _arrRollPeak{ 0.0f, 0.0f }
        , _arrPitchPeak{ 0.0f, 0.0f }
        , _peakWindowElapsed{ 0.0f }
    {
    }

    void BoatComponent::onBeginPlay()
    {
        PhysicsComponent::onBeginPlay();
        if ( _sailArea > 0.0f && _sailEfficiency._listKey.empty() )
            SW_LOG_ERROR_ID( "Boat.SailCurveEmpty", "'%#' has a sail (area %# m2) without Sail Efficiency keys - the sail gives no force", getOwner()->getName().c_str(), _sailArea );
    }

    void BoatComponent::setHelmInput( const BoatHelmInput& input )
    {
        _input._throttle = MathUtil::clamp( input._throttle, -1.0f, 1.0f );
        _input._rudder   = MathUtil::clamp( input._rudder, -1.0f, 1.0f );
    }

    void BoatComponent::setSail( float32 area, const FloatCurve& efficiency )
    {
        _sailArea       = area;
        _sailEfficiency = efficiency;
    }

    void BoatComponent::setRudder( float32 maxAngle, float32 force, const float3& localPosition )
    {
        _maxRudderAngle = maxAngle;
        _rudderForce    = force;
        _rudderPosition = localPosition;
    }

    void BoatComponent::setWaterDrag( const float3& drag, float32 yawDrag )
    {
        _waterDrag = drag;
        _yawDrag   = yawDrag;
    }

    float32 BoatComponent::computeRudderForce( float32 forceRate, float32 forwardWaterSpeed, float32 rudderAngle )
    {
        return forceRate * forwardWaterSpeed * MathUtil::sin( rudderAngle );
    }

    bool BoatComponent::computeSailAngle( const float3& boatForward, const float3& apparentWind, float32& outAngle )
    {
        const float2  wind{ apparentWind._x, apparentWind._z };
        const float2  forward{ boatForward._x, boatForward._z };
        const float32 windLength    = wind.getLength();
        const float32 forwardLength = forward.getLength();
        if ( windLength <= MathUtil::kEpsilon || forwardLength <= MathUtil::kEpsilon )
            return false;
        outAngle = MathUtil::acos( MathUtil::clamp( wind.dot( forward ) / ( windLength * forwardLength ), -1.0f, 1.0f ) );
        return true;
    }

    float32 BoatComponent::computeSailForce( float32 sailArea, float32 apparentWindSpeed, float32 efficiency )
    {
        return 0.5f * kAirDensity * apparentWindSpeed * apparentWindSpeed * sailArea * efficiency;
    }

    void BoatComponent::recordTilt( float32 roll, float32 pitch, float32 deltaTime )
    {
        _peakWindowElapsed += deltaTime;
        if ( _peakWindowElapsed >= kPeakWindowSeconds )
        {
            _peakWindowElapsed = 0.0f;
            _arrRollPeak[0]    = _arrRollPeak[1];
            _arrPitchPeak[0]   = _arrPitchPeak[1];
            _arrRollPeak[1]    = 0.0f;
            _arrPitchPeak[1]   = 0.0f;
        }
        _arrRollPeak[1]  = MathUtil::max( _arrRollPeak[1], MathUtil::abs( roll ) );
        _arrPitchPeak[1] = MathUtil::max( _arrPitchPeak[1], MathUtil::abs( pitch ) );
    }

    void BoatComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        (void)stepIndex;
        (void)stepCount;
        _state = BoatState{};
        if ( isSimulated() == false )
            return;
        IPhysicsBodyScene3D*      pScene    = physics.findScene3D();
        const RigidBodyComponent* pRigid    = getOwner()->getComponent<RigidBodyComponent>();
        const GameObjectManager*  pManager  = getOwner()->getManager();
        const bool                bHasInput = pScene != nullptr && pRigid != nullptr && pManager != nullptr;
        if ( bHasInput == false || pRigid->getBodyType() != PhysicsBodyType::Dynamic )
            return;
        const PhysicsBodyHandle body = pRigid->getBodyHandle();
        if ( body.isValid() == false || pScene->isBodyValid( body ) == false )
            return;
        float3     bodyPosition{};
        quaternion bodyRotation{};
        if ( pScene->getBodyTransform( body, bodyPosition, bodyRotation ) == false )
            return;

        const float3 right           = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, bodyRotation );
        const float3 up              = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, bodyRotation );
        const float3 forward         = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, bodyRotation );
        const float3 linearVelocity  = pScene->getLinearVelocity( body );
        const float3 angularVelocity = pScene->getAngularVelocity( body );
        _state._forwardSpeed         = linearVelocity.dot( forward );
        _state._yawRate              = angularVelocity._y;
        _state._roll                 = MathUtil::asin( MathUtil::clamp( right._y, -1.0f, 1.0f ) );
        _state._pitch                = MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
        recordTilt( _state._roll, _state._pitch, fixedDeltaTime );

        // 잠긴 비율은 같은 오브젝트의 폰툰이 정한다(뜨기의 정본). 물 밖이면 키 · 노 · 저항이 없다.
        const BuoyancyComponent* pFloat       = getOwner()->getComponent<BuoyancyComponent>();
        const size_t             pontoonCount = pFloat != nullptr ? pFloat->getPontoons().size() : 0;
        if ( pontoonCount == 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "Boat.NoPontoons", "'%#' has a BoatComponent without a BuoyancyComponent with pontoons - it never touches the water", getOwner()->getName().c_str() );
            return;
        }
        const float32 submergedRatio = static_cast<float32>( pFloat->getSubmergedPontoonCount() ) / static_cast<float32>( pontoonCount );
        _state._submergedRatio       = submergedRatio;
        if ( submergedRatio <= 0.0f )
            return;

        // 물에 대한 상대 속도 — 흐르는 물에서는 흐름을 탄다(해류에 밀린다).
        const WaterBodyComponent* pWater = WaterBodyComponent::findWaterAt( *pManager, bodyPosition._x, bodyPosition._z );
        float3                    flowVelocity{};
        if ( pWater != nullptr )
            (void)pWater->computeFlowVelocity( bodyPosition._x, bodyPosition._z, flowVelocity ); // 같은 자리를 덮는 물이라 늘 참이다
        const float3  waterVelocity = linearVelocity - flowVelocity;
        const float32 sideSpeed     = waterVelocity.dot( right );
        const float32 upSpeed       = waterVelocity.dot( up );
        const float32 forwardSpeed  = waterVelocity.dot( forward );
        const float3  drag          = ( right * ( _waterDrag._x * sideSpeed ) + up * ( _waterDrag._y * upSpeed ) + forward * ( _waterDrag._z * forwardSpeed ) ) * ( -submergedRatio );

        // 추진: 노 · 엔진은 조타 입력 그대로, 돛은 편 만큼(양수만) 겉보기 바람을 받는다.
        float32 thrust = _engineThrust * _input._throttle * submergedRatio;
        if ( _sailArea > 0.0f && _input._throttle > 0.0f && _sailEfficiency._listKey.empty() == false )
        {
            WindSettings wind;
            (void)WindComponent::findWind( *pManager, wind ); // 바람 컴포넌트가 없으면 식생과 같은 엔진 기본 바람이다(WindSettings)
            const float3 windVelocity = float3{ wind._direction._x, 0.0f, wind._direction._y } * wind._speed;
            const float3 apparentWind = windVelocity - linearVelocity;
            float32      sailAngle    = 0.0f;
            if ( computeSailAngle( forward, apparentWind, sailAngle ) )
            {
                const std::optional<float32> efficiency = _sailEfficiency.evaluate( sailAngle ); // 키가 있는 곡선만 여기에 온다
                if ( efficiency.has_value() )
                {
                    _state._sailForce = computeSailForce( _sailArea, float2{ apparentWind._x, apparentWind._z }.getLength(), *efficiency ) * _input._throttle;
                    thrust += _state._sailForce;
                }
            }
        }
        pScene->addImpulse( body, ( drag + forward * thrust ) * fixedDeltaTime );

        // 키: 선미를 옆으로 민다. 위 축 회전 저항이 선회를 멈춘다.
        const float32 rudderForce = computeRudderForce( _rudderForce, forwardSpeed, _input._rudder * _maxRudderAngle ) * submergedRatio;
        const float3  rudderPoint = bodyPosition + float3::transform( _rudderPosition, bodyRotation );
        pScene->addImpulseAtPoint( body, right * ( -rudderForce * fixedDeltaTime ), rudderPoint );
        pScene->addTorque( body, float3{ 0.0f, -_yawDrag * angularVelocity._y * submergedRatio, 0.0f } );
    }

    void BoatComponent::releasePhysics( ScenePhysics& physics )
    {
        (void)physics;
        _state = BoatState{};
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct BoatWatchesInternal
        {
            /** @brief 질의 대상의 배입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const BoatComponent* findTargetBoat( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*    pTarget = query.findTargetObject();
                const BoatComponent* pBoat   = pTarget != nullptr ? pTarget->getComponent<BoatComponent>() : nullptr;
                if ( pBoat == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "Boat" ) );
                return pBoat;
            }

            /** @brief 감시 값 `Boat.ForwardSpeed` 입니다. 선수 방향 속도(m/s, 땅 기준)입니다. */
            [[nodiscard]] static bool readForwardSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getState()._forwardSpeed ) );
                return true;
            }

            /** @brief 감시 값 `Boat.TurnRadius` 입니다. 선수 방향 속도 / 위 축 회전 속도(m)이고, 거의 돌지 않으면 글 `straight` 입니다. */
            [[nodiscard]] static bool readTurnRadius( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                const BoatState& state = pBoat->getState();
                if ( MathUtil::abs( state._yawRate ) < 1.0e-3f )
                    outValue.setText( "straight" );
                else
                    outValue.setNumber( static_cast<float64>( MathUtil::abs( state._forwardSpeed / state._yawRate ) ) );
                return true;
            }

            /** @brief 감시 값 `Boat.RollPeak` 입니다. 최근 4 ~ 8 초의 옆 기울기 크기 최댓값(rad)입니다. */
            [[nodiscard]] static bool readRollPeak( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getRollPeak() ) );
                return true;
            }

            /** @brief 감시 값 `Boat.PitchPeak` 입니다. 최근 4 ~ 8 초의 앞뒤 기울기 크기 최댓값(rad)입니다. */
            [[nodiscard]] static bool readPitchPeak( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getPitchPeak() ) );
                return true;
            }

            /** @brief 감시 값 `Boat.SailForce` 입니다. 마지막 스텝에 돛이 낸 앞 힘(N)입니다. */
            [[nodiscard]] static bool readSailForce( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getState()._sailForce ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( boatForwardSpeed, "Boat.ForwardSpeed", "Forward speed (m/s, over ground) of the boat on target", &BoatWatchesInternal::readForwardSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatTurnRadius, "Boat.TurnRadius", "Turning radius (m) of the boat on target: forward speed / yaw rate (text straight when not turning)",
                               &BoatWatchesInternal::readTurnRadius, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatRollPeak, "Boat.RollPeak", "Largest roll (rad) of the boat on target over the last 4-8 seconds", &BoatWatchesInternal::readRollPeak, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatPitchPeak, "Boat.PitchPeak", "Largest pitch (rad) of the boat on target over the last 4-8 seconds", &BoatWatchesInternal::readPitchPeak, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatSailForce, "Boat.SailForce", "Forward force (N) the sail of the boat on target gave in the last physics step", &BoatWatchesInternal::readSailForce, true, false );
} // namespace sw
