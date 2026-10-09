#include "pch.h"

#include "Engine/Object/Animation/LocomotionWarpingComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Pose.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    namespace
    {
        struct LocomotionWarpingComponentInternal
        {
            static constexpr float32 kMinMovingSpeed   = 0.1f;  ///< 이보다 느리면 서 있는 것으로 본다(m/s)
            static constexpr float32 kMinAuthoredSpeed = 0.05f; ///< 클립 속도가 이보다 작으면 보폭을 맞추지 않는다

            static float32 wrapAngle( float32 angle )
            {
                while ( angle > MathUtil::kPi )
                {
                    angle -= MathUtil::kPi * 2.0f;
                }
                while ( angle <= -MathUtil::kPi )
                {
                    angle += MathUtil::kPi * 2.0f;
                }
                return angle;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LocomotionWarpingBinding::LocomotionWarpingBinding( LocomotionWarpingComponent& owner )
        : _owner{ owner }
    {
    }

    void LocomotionWarpingBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        if ( phase == AnimationPhase::Time )
            _owner._lastDeltaSeconds = context._deltaSeconds;
        else if ( phase == AnimationPhase::PostProcess )
            _owner.applyOrientation( unit );
    }

    void LocomotionWarpingBinding::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        _owner.updateFromMovement( unit );
    }

    void LocomotionWarpingBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    LocomotionWarpingComponent::LocomotionWarpingComponent()
        : _bStrideWarping{ true }
        , _speedCurve{ "Speed" }
        , _minPlayRate{ 0.6f }
        , _maxPlayRate{ 1.6f }
        , _bOrientationWarping{ true }
        , _maxOrientationAngle{ 1.0f }
        , _listOrientationBone{}
        , _binding{ *this }
        , _pUnit{ nullptr }
        , _previousPosition{}
        , _measuredVelocity{}
        , _orientationAngle{ 0.0f }
        , _strideScale{ 1.0f }
        , _lastDeltaSeconds{ 0.0f }
        , _bHasPrevious{ false }
    {
        setCanEverTick( false );
    }

    LocomotionWarpingComponent::~LocomotionWarpingComponent()
    {
        unbindFromUnit();
    }

    void LocomotionWarpingComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        bindToUnit();
    }

    void LocomotionWarpingComponent::onEndPlay()
    {
        unbindFromUnit();
        Component::onEndPlay();
    }

    void LocomotionWarpingComponent::bindToUnit()
    {
        GameObject*            pOwner = getOwner();
        SkeletalMeshComponent* pUnit  = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pUnit == _pUnit )
            return;
        unbindFromUnit();
        _pUnit = pUnit;
        if ( _pUnit != nullptr )
            _pUnit->addAnimationPhaseTask( &_binding );
        _bHasPrevious = false;
    }

    void LocomotionWarpingComponent::unbindFromUnit()
    {
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = nullptr;
    }

    void LocomotionWarpingComponent::updateFromMovement( SkeletalMeshComponent& unit )
    {
        GameObject*     pOwner = unit.getOwner();
        SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr )
            return;
        // 속도 — 캐릭터 컨트롤러가 있으면 그 마지막 스텝의 속도, 없으면 트랜스폼 변화 ÷ 프레임 시간.
        const float3                        position    = pRoot->getWorldPosition();
        const CharacterControllerComponent* pController = pOwner->getComponent<CharacterControllerComponent>();
        if ( pController != nullptr && pController->hasBegunPlay() )
            _measuredVelocity = pController->getVelocity();
        else if ( _bHasPrevious && _lastDeltaSeconds > 0.0f )
            _measuredVelocity = ( position - _previousPosition ) * ( 1.0f / _lastDeltaSeconds );
        _measuredVelocity._y = 0.0f;
        _previousPosition    = position;
        _bHasPrevious        = true;
        const float32 speed  = _measuredVelocity.getLength();

        SkeletalAnimatorComponent* pAnimator = pOwner->getComponent<SkeletalAnimatorComponent>();
        _strideScale                         = 1.0f;
        if ( _bStrideWarping && pAnimator != nullptr )
        {
            const float32 authored = pAnimator->getCurveValue( _speedCurve );
            if ( authored > LocomotionWarpingComponentInternal::kMinAuthoredSpeed )
            {
                const float32 rate = MathUtil::clamp( speed / authored, _minPlayRate, _maxPlayRate );
                pAnimator->setPlayRate( rate );
                _strideScale = speed / ( authored * rate );
            }
        }

        _orientationAngle = 0.0f;
        if ( _bOrientationWarping && speed >= LocomotionWarpingComponentInternal::kMinMovingSpeed )
        {
            const float3  forward    = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pRoot->getWorldMatrix() );
            const float32 facingYaw  = MathUtil::atan2( forward._x, forward._z );
            const float32 movingYaw  = MathUtil::atan2( _measuredVelocity._x, _measuredVelocity._z );
            float32       difference = LocomotionWarpingComponentInternal::wrapAngle( movingYaw - facingYaw );
            // 뒤로 가는 이동은 뒷걸음 클립이 맡는다 — 하체를 반 바퀴 돌리지 않고 반대쪽으로 접는다.
            if ( difference > MathUtil::kHalfPi )
                difference -= MathUtil::kPi;
            else if ( difference < -MathUtil::kHalfPi )
                difference += MathUtil::kPi;
            _orientationAngle = MathUtil::clamp( difference, -_maxOrientationAngle, _maxOrientationAngle );
        }
    }

    void LocomotionWarpingComponent::applyOrientation( SkeletalMeshComponent& unit ) const
    {
        if ( _bOrientationWarping == false || _orientationAngle == 0.0f )
            return;
        const Skeleton&         skeleton  = unit.getSkeleton();
        const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
        Pose&                   pose      = unit.getLocalPose();
        const float3            characterUp{ 0.0f, 1.0f, 0.0f };
        for ( const LocomotionOrientationBone& entry : _listOrientationBone )
        {
            const int32 boneIndex = skeleton.findBoneIndex( entry._bone );
            if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= listModel.size() )
                continue;
            // 캐릭터 위 축을 부모 공간으로 옮겨 그 축으로 돌린다(뼈 자리는 그대로) — 위 축 둘레 회전은 위 축을 바꾸지 않아 사슬 아래 뼈도 같은 축을 쓴다.
            const int32      parentIndex = skeleton.getBone( static_cast<uint32>( boneIndex ) )._parentIndex;
            const float4x4   parentModel = parentIndex >= 0 ? listModel[static_cast<size_t>( parentIndex )] : float4x4::Identity;
            const float3     axis        = float3::transformVector( characterUp, parentModel.invert() ).normalize();
            const quaternion turn        = quaternion::createFromAxisAngle( axis, _orientationAngle * entry._weight );
            BoneTransform    local       = pose.getBoneTransform( static_cast<uint32>( boneIndex ) );
            float4x4         rotation    = float4x4::createTrs( float3{}, local._rotation, float3{ 1.0f, 1.0f, 1.0f } ) *
                                float4x4::createTrs( float3{}, turn, float3{ 1.0f, 1.0f, 1.0f } );
            float3     scale{};
            quaternion turned{};
            float3     translation{};
            if ( rotation.decompose( scale, turned, translation ) == false )
                continue;
            local._rotation = turned.normalize();
            pose.setBoneTransform( static_cast<uint32>( boneIndex ), local );
        }
    }
} // namespace sw
