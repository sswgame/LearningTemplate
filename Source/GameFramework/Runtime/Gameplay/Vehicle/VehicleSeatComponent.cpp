#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatSystem.h"

namespace sw
{
    namespace
    {
        struct VehicleSeatComponentInternal
        {
            /** @brief 시야각 킥의 모양입니다(오르기 · 버티기 · 내리기). 크기는 좌석 반응 데이터가 정한다. */
            static constexpr float32 kKickAttack  = 0.08f;
            static constexpr float32 kKickHold    = 0.15f;
            static constexpr float32 kKickRelease = 0.5f;
            /** @brief 충격 흔들림의 길이 · 진동수 · 감쇠 · 회전 비율입니다. */
            static constexpr float32 kBumpDuration      = 0.35f;
            static constexpr float32 kBumpFrequency     = 35.0f;
            static constexpr float32 kBumpDecay         = 0.12f;
            static constexpr float32 kBumpRotationScale = 0.6f;

            /** @brief 행벡터 행렬의 행 @p row(0 오른쪽, 1 위, 2 앞)를 단위 축으로 냅니다. */
            static float3 makeAxis( const float4x4& matrix, int32 row )
            {
                const float3  axis   = row == 0 ? float3{ matrix._11, matrix._12, matrix._13 } : ( row == 1 ? float3{ matrix._21, matrix._22, matrix._23 } : float3{ matrix._31, matrix._32, matrix._33 } );
                const float32 length = MathUtil::sqrt( axis.dot( axis ) );
                return length > 1.0e-6f ? axis * ( 1.0f / length ) : float3{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float2 RideFeelSettings::computeLean( const float3& localAcceleration ) const
    {
        // 가속도를 거슬러 기운다: 앞 가속(+z)이면 뒤로(숙임 −), 오른쪽 가속(+x)이면 왼쪽(+).
        const float32 forward = MathUtil::clamp( -_leanPerAcceleration * localAcceleration._z, -_maxLean, _maxLean );
        const float32 side    = MathUtil::clamp( _leanPerAcceleration * localAcceleration._x, -_maxLean, _maxLean );
        return float2{ forward, side };
    }

    VehicleSeatComponent::VehicleSeatComponent()
        : _socketName{}
        , _riderPoseParameter{}
        , _seatOffset{ 0.0f, 1.0f, 0.0f }
        , _exitOffset{ -1.5f, 0.0f, 0.0f }
        , _enterDistance{ 3.0f }
        , _bDriverSeat{ true }
        , _rideFeel{}
        , _occupant{}
        , _occupantController{}
        , _previousVehicleController{}
        , _seatInVehicle{ float4x4::Identity }
        , _lastSeatPosition{}
        , _lastSeatVelocity{}
        , _rideAcceleration{}
        , _riderLean{}
        , _pendingFieldOfViewKick{ 0.0f }
        , _pendingBumpShake{ 0.0f }
        , _pendingFeedbackSpeed{ 0.0f }
        , _pendingFeedbackSlip{ 0.0f }
        , _rideSampleCount{ 0 }
        , _bFieldOfViewKickArmed{ 1 }
        , _bBumpArmed{ 1 }
        , _bRidePending{ 0 }
        , _reservedRideFlags{ 0 }
    {
        setCanEverTick( true );
    }

    void VehicleSeatComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<VehicleSeatComponent>( this ); // 빈 좌석 찾기와 탑승자의 좌석 찾기가 씬을 순회하지 않게 된다
        (void)VehicleSeatSystem::ensureFor( manager );
    }

    void VehicleSeatComponent::onUnregister( GameObjectManager& manager )
    {
        // 탈것이 사라지므로 앉은 쪽은 지금 위치에서 내린다. 조종 시스템이 없으면 씬을 비우는 중이다. 시스템이 먼저 사라지기 때문이다. 이때는 탑승자도 사라지므로 핸들만 버린다.
        const bool     bClearing = ControlSystem::find( manager ) == nullptr;
        GameObject*    pOccupant = _occupant.isValid() && bClearing == false ? manager.resolveGameObject( _occupant ) : nullptr;
        PawnComponent* pRider    = pOccupant != nullptr ? pOccupant->getComponent<PawnComponent>() : nullptr;
        if ( pRider != nullptr )
            (void)VehicleSeatComponent::dismount( *pRider, true );
        _occupant                  = GameObjectHandle{};
        _occupantController        = TComponentHandle<ControllerComponent>{};
        _previousVehicleController = TComponentHandle<ControllerComponent>{};
        manager.getComponentRegistry().remove<VehicleSeatComponent>( this );
        VehicleSeatSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void VehicleSeatComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostPhysics );
    }

    void VehicleSeatComponent::onTick( float32 deltaTime )
    {
        using Internal = VehicleSeatComponentInternal;
        Component::onTick( deltaTime );
        GameObject*           pVehicle = getOwner();
        const SceneComponent* pRoot    = pVehicle != nullptr ? pVehicle->getPrimarySceneComponent() : nullptr;
        GameObjectManager*    pManager = pVehicle != nullptr ? pVehicle->getManager() : nullptr;
        if ( isFree() || pRoot == nullptr || pManager == nullptr || deltaTime <= 0.0f )
        {
            _rideSampleCount  = 0;
            _rideAcceleration = float3{};
            _riderLean        = float2{};
            _bRidePending     = 0;
            return;
        }

        // 좌석 월드 위치의 2 차 차분 — 탈것이 바디로 움직이든(바퀴 차 · 배) 직접 옮기든(바디 없는 탈것 · 말) 같은 값이다.
        const float4x4 world    = pRoot->getWorldMatrix();
        const float3   position = float3::transform( _seatInVehicle.getTranslation(), world );
        const float3   velocity = _rideSampleCount >= 1 ? ( position - _lastSeatPosition ) * ( 1.0f / deltaTime ) : float3{};
        float3         rawLocal{};
        if ( _rideSampleCount >= 2 )
        {
            const float3 acceleration = ( velocity - _lastSeatVelocity ) * ( 1.0f / deltaTime );
            rawLocal                  = float3{ acceleration.dot( Internal::makeAxis( world, 0 ) ), acceleration.dot( Internal::makeAxis( world, 1 ) ), acceleration.dot( Internal::makeAxis( world, 2 ) ) };
            const float32 blend       = _rideFeel._smoothingTime > 0.0f ? deltaTime / ( _rideFeel._smoothingTime + deltaTime ) : 1.0f;
            _rideAcceleration         = _rideAcceleration + ( rawLocal - _rideAcceleration ) * blend;
        }
        _lastSeatPosition = position;
        _lastSeatVelocity = velocity;
        _rideSampleCount  = MathUtil::min( _rideSampleCount + 1, 2 );

        _riderLean = _rideFeel.computeLean( _rideAcceleration );

        // 카메라는 문턱을 넘는 순간 한 번씩(다시 내려가야 다음) — 매끄럽게 한 앞 가속(부스트 · 급가속)과 날것의 수직 가속(착지 · 턱).
        const bool bKick = _rideFeel._fieldOfViewKickAcceleration > 0.0f && _rideAcceleration._z >= _rideFeel._fieldOfViewKickAcceleration;
        if ( bKick && _bFieldOfViewKickArmed )
            _pendingFieldOfViewKick = _rideFeel._fieldOfViewKickAmount;
        _bFieldOfViewKickArmed = bKick ? 0 : 1;
        const float32 vertical = MathUtil::abs( rawLocal._y );
        const bool    bBump    = _rideFeel._bumpAcceleration > 0.0f && vertical >= _rideFeel._bumpAcceleration;
        if ( bBump && _bBumpArmed )
            _pendingBumpShake = ( vertical - _rideFeel._bumpAcceleration ) * _rideFeel._bumpShakePerAcceleration;
        _bBumpArmed = bBump ? 0 : 1;

        _pendingFeedbackSpeed = MathUtil::sqrt( velocity.dot( velocity ) );
        _pendingFeedbackSlip  = MathUtil::abs( _rideAcceleration._x );
        // 탑승자 기울임 · 카메라 · 소리는 남의 오브젝트 · 게임 스레드 일이다 — 틱은 깃발만 세우고 `VehicleSeatSystem` 이 다음 `ApplyPrePhysics` 맨 앞에서 건다.
        _bRidePending = 1;
    }

    void VehicleSeatComponent::applyPendingRide( GameObjectManager& manager )
    {
        if ( _bRidePending == 0 )
            return;
        _bRidePending = 0;
        applyRiderLean( manager );
        applyRideFeedback();
    }

    void VehicleSeatComponent::applyRiderLean( GameObjectManager& manager )
    {
        GameObject* pRider = manager.resolveGameObject( _occupant );
        if ( pRider == nullptr )
            return;
        // 기울임은 좌석 소켓 변환에 얹는 회전이다(행벡터 — 먼저 탑승자를 기울이고 소켓으로 옮긴다). 피치 +는 머리가 앞(+Z)으로, 롤 +는 머리가 왼쪽(−X)으로 간다.
        SocketBindingComponent* pBinding = pRider->getComponent<SocketBindingComponent>();
        if ( pBinding != nullptr && pBinding->getState() == SocketBindingState::Bound )
            pBinding->updateSocketTransform( float4x4::makeRotationZ( _riderLean._y ) * float4x4::makeRotationX( _riderLean._x ) * _seatInVehicle );
        SkeletalAnimatorComponent* pAnimator = pRider->getComponent<SkeletalAnimatorComponent>();
        if ( pAnimator == nullptr || _rideFeel._maxLean <= 0.0f )
            return;
        if ( _rideFeel._leanForwardParameter.empty() == false )
            pAnimator->getParameters().setFloat( _rideFeel._leanForwardParameter, _riderLean._x / _rideFeel._maxLean );
        if ( _rideFeel._leanSideParameter.empty() == false )
            pAnimator->getParameters().setFloat( _rideFeel._leanSideParameter, -_riderLean._y / _rideFeel._maxLean );
    }

    void VehicleSeatComponent::applyRideFeedback()
    {
        using Internal              = VehicleSeatComponentInternal;
        GameObject*        pVehicle = getOwner();
        GameObjectManager* pManager = pVehicle != nullptr ? pVehicle->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        if ( _rideFeel._speedSoundParameter.empty() == false )
            GameSound::setObjectParameter( *pVehicle, _rideFeel._speedSoundParameter, _pendingFeedbackSpeed );
        if ( _rideFeel._slipSoundParameter.empty() == false )
            GameSound::setObjectParameter( *pVehicle, _rideFeel._slipSoundParameter, _pendingFeedbackSlip );
        if ( _pendingFieldOfViewKick == 0.0f && _pendingBumpShake <= 0.0f )
            return;

        // 탑승자나 탈것을 따라가는 카메라 디렉터에 건다(디렉터 목록 — 씬을 훑지 않는다).
        const GameObjectHandle vehicle = pVehicle->getHandle();
        for ( CameraDirectorComponent* pDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
        {
            const GameObjectHandle& target = pDirector->getTarget();
            if ( target != vehicle && target != _occupant )
                continue;
            if ( _pendingFieldOfViewKick != 0.0f )
            {
                CameraFieldOfViewKickDef kick;
                kick._amount      = _pendingFieldOfViewKick;
                kick._attackTime  = Internal::kKickAttack;
                kick._holdTime    = Internal::kKickHold;
                kick._releaseTime = Internal::kKickRelease;
                pDirector->addFieldOfViewKick( kick );
            }
            if ( _pendingBumpShake > 0.0f )
            {
                CameraImpulseDef impulse;
                impulse._amplitude     = _pendingBumpShake;
                impulse._duration      = Internal::kBumpDuration;
                impulse._frequency     = Internal::kBumpFrequency;
                impulse._decayTime     = Internal::kBumpDecay;
                impulse._rotationScale = Internal::kBumpRotationScale;
                pDirector->addImpulse( impulse, _lastSeatPosition );
            }
        }
        _pendingFieldOfViewKick = 0.0f;
        _pendingBumpShake       = 0.0f;
    }
} // namespace sw
