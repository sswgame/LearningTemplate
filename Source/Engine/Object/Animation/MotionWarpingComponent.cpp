#include "pch.h"

#include "Engine/Object/Animation/MotionWarpingComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct MotionWarpingComponentInternal
        {

            /** @brief 회전이 +Z 를 돌린 방향의 요(라디안)입니다. */
            static float32 computeYaw( const quaternion& rotation )
            {
                const float3 forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation );
                return MathUtil::atan2( forward._x, forward._z );
            }

            /** @brief 월드 행렬의 앞(+Z)의 요입니다. */
            static float32 computeYaw( const float4x4& world )
            {
                const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                return MathUtil::atan2( forward._x, forward._z );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MotionWarpingBinding::MotionWarpingBinding( MotionWarpingComponent& owner )
        : _owner{ owner }
    {
    }

    void MotionWarpingBinding::modifyRootMotion( const SkeletalAnimatorComponent& animator, RootMotionFrame& inoutFrame )
    {
        _owner.warpRootMotion( animator, inoutFrame );
    }

    MotionWarpingComponent::MotionWarpingComponent()
        : _binding{ *this }
        , _listTarget{}
        , _listWindow{}
        , _animator{}
    {
        setCanEverTick( false );
    }

    MotionWarpingComponent::~MotionWarpingComponent()
    {
        unbindFromAnimator();
    }

    void MotionWarpingComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        bindToAnimator();
    }

    void MotionWarpingComponent::onEndPlay()
    {
        _listWindow.clear();
        unbindFromAnimator();
        Component::onEndPlay();
    }

    void MotionWarpingComponent::bindToAnimator()
    {
        GameObject*                pOwner    = getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator == nullptr )
            return;
        pAnimator->addRootMotionModifier( &_binding );
        _animator = pAnimator->getHandle();
    }

    void MotionWarpingComponent::unbindFromAnimator()
    {
        const GameObject*          pOwner   = getOwner();
        GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        SkeletalAnimatorComponent* pAnimator =
            ( pManager != nullptr && _animator.isValid() ) ? castTo<SkeletalAnimatorComponent>( pManager->resolveComponent( _animator ) ) : nullptr;
        _animator = ComponentHandle{};
        if ( pAnimator != nullptr )
            pAnimator->removeRootMotionModifier( &_binding );
    }

    void MotionWarpingComponent::setWarpTarget( const hashed_string& name, const float3& position, float32 yaw )
    {
        for ( MotionWarpTarget& target : _listTarget )
        {
            if ( target._name == name )
            {
                target._position = position;
                target._yaw      = yaw;
                return;
            }
        }
        _listTarget.push_back( MotionWarpTarget{ name, position, yaw } );
    }

    void MotionWarpingComponent::setWarpTargetFromTransform( const hashed_string& name, const float4x4& worldTransform )
    {
        setWarpTarget( name, worldTransform.getTranslation(), MotionWarpingComponentInternal::computeYaw( worldTransform ) );
    }

    void MotionWarpingComponent::clearWarpTarget( const hashed_string& name )
    {
        for ( size_t targetIndex = 0; targetIndex < _listTarget.size(); ++targetIndex )
        {
            if ( _listTarget[targetIndex]._name == name )
            {
                _listTarget.erase( _listTarget.begin() + static_cast<ptrdiff_t>( targetIndex ) );
                return;
            }
        }
    }

    const MotionWarpTarget* MotionWarpingComponent::findWarpTarget( const hashed_string& name ) const
    {
        for ( const MotionWarpTarget& target : _listTarget )
        {
            if ( target._name == name )
                return &target;
        }
        return nullptr;
    }

    void MotionWarpingComponent::beginWindow( const MotionWarpWindow& window )
    {
        for ( MotionWarpWindow& open : _listWindow )
        {
            if ( open._target == window._target && open._pClip == window._pClip )
            {
                open = window;
                return;
            }
        }
        _listWindow.push_back( window );
    }

    void MotionWarpingComponent::endWindow( const hashed_string& target, const IAnimPlayable* pClip )
    {
        for ( size_t windowIndex = 0; windowIndex < _listWindow.size(); ++windowIndex )
        {
            if ( _listWindow[windowIndex]._target == target && _listWindow[windowIndex]._pClip == pClip )
            {
                _listWindow[windowIndex]._bEnding = true;
                return;
            }
        }
    }

    void MotionWarpingComponent::warpRootMotion( const SkeletalAnimatorComponent& animator, RootMotionFrame& inoutFrame )
    {
        (void)animator;
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool            bWarp  = pRoot != nullptr && inoutFrame._pClip != nullptr && inoutFrame._bClipWrapped == SW_FALSE;
        for ( size_t windowIndex = 0; bWarp && windowIndex < _listWindow.size(); ++windowIndex )
        {
            const MotionWarpWindow& window  = _listWindow[windowIndex];
            const MotionWarpTarget* pTarget = findWarpTarget( window._target );
            if ( pTarget == nullptr || window._pClip != inoutFrame._pClip )
                continue;
            // 이번 프레임 앞에서 창 끝까지 남은 시간 — 이번 프레임이 그 몫만큼 고침을 맡는다(끝 프레임에 1).
            const float32 remainingBefore = window._endTime - inoutFrame._previousClipTime;
            if ( remainingBefore <= 1.0e-5f )
                continue;
            const float32   frameTime = inoutFrame._clipTime - inoutFrame._previousClipTime;
            const float32   fraction  = MathUtil::clamp( frameTime / remainingBefore, 0.0f, 1.0f );
            const AnimClip& clip      = *static_cast<const AnimClip*>( inoutFrame._pClip );
            // 이번 프레임 뒤에 클립이 창 끝까지 더 갈 루트 모션(캐릭터 공간 → 지금 월드 회전).
            BoneTransform remaining{};
            if ( inoutFrame._clipTime < window._endTime )
            {
                AnimTimeStep rest{};
                rest._previousTime = inoutFrame._clipTime;
                rest._currentTime  = window._endTime;
                remaining          = clip.computeRootMotionDelta( rest );
            }
            const float4x4 world = pRoot->getWorldMatrix();
            if ( window._bTranslation )
            {
                const float3 remainingWorld = float3::transformVector( remaining._translation, world );
                float3       correction     = pTarget->_position - pRoot->getWorldPosition() - ( inoutFrame._worldTranslation + remainingWorld );
                if ( window._bVertical == false )
                    correction._y = 0.0f;
                inoutFrame._worldTranslation = inoutFrame._worldTranslation + correction * fraction;
            }
            if ( window._bRotation )
            {
                const float32    currentYaw   = MotionWarpingComponentInternal::computeYaw( world );
                const float32    frameYaw     = MotionWarpingComponentInternal::computeYaw( inoutFrame._rotation );
                const float32    remainingYaw = MotionWarpingComponentInternal::computeYaw( remaining._rotation );
                const float32    needed       = MathUtil::wrapAngle( pTarget->_yaw - currentYaw - frameYaw - remainingYaw );
                const quaternion turn         = quaternion::createFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, needed * fraction );
                inoutFrame._rotation          = ( inoutFrame._rotation * turn ).normalize();
            }
        }
        // 끝이 울린 창은 이번 프레임까지 휘고 닫는다.
        for ( size_t windowIndex = _listWindow.size(); windowIndex > 0; --windowIndex )
        {
            if ( _listWindow[windowIndex - 1]._bEnding )
                _listWindow.erase( _listWindow.begin() + static_cast<ptrdiff_t>( windowIndex - 1 ) );
        }
    }
} // namespace sw
