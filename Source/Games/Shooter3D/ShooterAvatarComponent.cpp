#include "pch.h"

#include "Games/Shooter3D/ShooterAvatarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Skeletal/LocomotionSpeedUtil.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"

#include "Games/Shooter3D/ShooterAnimationParameter.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 이동 방향 · 앞 속도를 그래프의 `Move` 코드로 바꿉니다(앞은 빠르면 달리기). */
        float32 computeMoveCode( LocomotionDirection direction, float32 forwardSpeed, float32 runThreshold )
        {
            switch ( direction )
            {
                case LocomotionDirection::Idle:
                    return ShooterAnimationParameter::kMoveIdle;
                case LocomotionDirection::Forward:
                    return forwardSpeed >= runThreshold ? ShooterAnimationParameter::kMoveRun : ShooterAnimationParameter::kMoveWalk;
                case LocomotionDirection::Backward:
                    return ShooterAnimationParameter::kMoveBackward;
                case LocomotionDirection::Left:
                    return ShooterAnimationParameter::kMoveLeft;
                case LocomotionDirection::Right:
                    return ShooterAnimationParameter::kMoveRight;
                case LocomotionDirection::Airborne:
                    return ShooterAnimationParameter::kMoveAir;
            }
            return ShooterAnimationParameter::kMoveIdle;
        }
    } // namespace
} // namespace sw

namespace sw
{
    ShooterAvatarComponent::ShooterAvatarComponent()
        : _player{}
        , _walkThreshold{ 0.4f }
        , _runThreshold{ 3.0f }
        , _hitPause{ 0.45f }
        , _turnRate{ 10.0f }
        , _layerBlendRate{ 10.0f }
        , _leftFootBone{ "foot.l" }
        , _rightFootBone{ "foot.r" }
        , _minPlayRate{ 0.6f }
        , _maxPlayRate{ 1.8f }
        , _minLocomotionFootSpeed{ 0.3f }
        , _locomotion{ 0.25f }
        , _mapClipFootSpeed{}
        , _aimWeight{ 0.0f }
        , _shootWeight{ 0.0f }
        , _hitTimer{ 0.0f }
        , _bodyYaw{ 0.0f }
        , _lastHitReaction{ 0 }
        , _bYawReady{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ShooterAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 플레이어(DuringPhysics)가 움직인 뒤에 읽는다.
        setTickGroup( TickGroup::PostPhysics );
        _mapClipFootSpeed.clear(); // 클립을 다시 읽었을 수 있다(핫 리로드)
    }

    float32 ShooterAvatarComponent::computeLocomotionPlayRate( const SkeletalAnimatorComponent& animator, LocomotionDirection direction, float32 moveSpeed )
    {
        const bool bLocomotion = direction != LocomotionDirection::Idle && direction != LocomotionDirection::Airborne;
        if ( bLocomotion == false )
            return 1.0f;
        const AnimationClip*         pClip  = animator.findClip( animator.getCurrentStateName() );
        const GameObject*            pOwner = getOwner();
        const SkeletalMeshComponent* pUnit  = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pClip == nullptr || pUnit == nullptr )
            return 1.0f;
        auto found = _mapClipFootSpeed.find( pClip );
        if ( found == _mapClipFootSpeed.end() )
        {
            // 클립마다 한 번만 잰다(표본 수만큼 디코드한다). 상태가 처음 바뀌는 프레임에만 든다.
            const float32 footSpeed = LocomotionSpeedUtil::computeFootSpeed( *pClip, pUnit->getSkeleton(), hashed_string( _leftFootBone ), hashed_string( _rightFootBone ),
                                                                             LocomotionSpeedUtil::kDefaultSampleCount );
            found                   = _mapClipFootSpeed.emplace( pClip, footSpeed ).first;
        }
        if ( found->second < _minLocomotionFootSpeed )
            return 1.0f;
        return LocomotionSpeedUtil::computePlayRate( moveSpeed, found->second, _minPlayRate, _maxPlayRate );
    }

    void ShooterAvatarComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                   pOwner              = getOwner();
        GameObjectManager*            pManager            = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*             pObject             = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer             = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        SkeletalAnimatorComponent*    pAnimator           = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        SceneComponent*               pScene              = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool                    bMissingAvatarParts = pPlayer == nullptr || pScene == nullptr || deltaTime <= 0.0f;
        if ( bMissingAvatarParts )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        // 몸은 발 자리에 서서 보는 쪽을 본다(KayKit 은 +Z 가 앞).
        // 시점이 한 프레임에 크게 돌아도(마우스 휙 · 표적 바꾸기) 몸은 각속도 상한으로 따라간다.
        _bodyYaw   = _bYawReady == SW_TRUE ? OrientationUtil::turnTowardAngle( _bodyYaw, pPlayer->getLookYaw(), _turnRate * step ) : pPlayer->getLookYaw();
        _bYawReady = SW_TRUE;
        pScene->setLocalPosition( pPlayer->getDisplayFeetPosition() );
        pScene->setLocalRotation( float3{ 0.0f, _bodyYaw, 0.0f } );
        if ( pAnimator == nullptr )
            return;

        const bool bAlive = pPlayer->isAlive();
        if ( pPlayer->getHitReactionCount() != _lastHitReaction )
        {
            _lastHitReaction = pPlayer->getHitReactionCount();
            if ( bAlive )
            {
                _hitTimer = _hitPause;
                pAnimator->getParameters().setTrigger( hashed_string( ShooterAnimationParameter::kHit ) );
            }
        }
        _hitTimer -= step;
        AnimationParameterSet& parameters = pAnimator->getParameters();
        // 대각선 · 임계값 근처에서 상태가 오가면 클립이 매번 처음부터 다시 돈다 — 히스테리시스로 고르고 바뀐 것은 잠깐 이어져야 받아들인다.
        const LocomotionDirection candidate = LocomotionMath::classifyFrom( _locomotion.getDirection(), pPlayer->getMoveVelocity(), _bodyYaw,
                                                                            pPlayer->isOnGround(), _walkThreshold );
        const LocomotionDirection direction = _locomotion.update( candidate, step );
        const float32             forward   = LocomotionMath::computeLocalVelocity( pPlayer->getMoveVelocity(), _bodyYaw )._x;
        parameters.setFloat( hashed_string( ShooterAnimationParameter::kMove ), computeMoveCode( direction, forward, _runThreshold ) );
        parameters.setBool( hashed_string( ShooterAnimationParameter::kDead ), bAlive == false );
        // 발 미끄러짐 — 이동 클립의 발 속도에 실제 수평 속도를 맞춘다(언리얼 재생 속도 매칭). 쓰러지면 클립 속도 그대로.
        const float3& velocity  = pPlayer->getMoveVelocity();
        const float32 moveSpeed = MathUtil::sqrt( velocity._x * velocity._x + velocity._z * velocity._z );
        pAnimator->setPlayRate( bAlive ? computeLocomotionPlayRate( *pAnimator, direction, moveSpeed ) : 1.0f );

        // 상체 — 겨눈 자세는 늘, 쏘는 동작은 막 쏜 동안. 맞음 · 쓰러짐 동안은 비켜선다.
        const bool    bUpperBodyFree = bAlive && _hitTimer <= 0.0f;
        const float32 aimTarget      = bUpperBodyFree ? 1.0f : 0.0f;
        const float32 shootTarget    = bUpperBodyFree && pPlayer->getTimeSinceShot() < 0.2f ? 1.0f : 0.0f;
        _aimWeight                   = MathUtil::moveToward( _aimWeight, aimTarget, _layerBlendRate * step );
        _shootWeight                 = MathUtil::moveToward( _shootWeight, shootTarget, _layerBlendRate * 2.0f * step );
        // 레이어(클립 · 상체 마스크)는 그래프 데이터("layers" · "bone_masks")가 갖는다 — 코드는 가중치 파라미터만 쓴다.
        parameters.setFloat( hashed_string( ShooterAnimationParameter::kAimWeight ), _aimWeight );
        parameters.setFloat( hashed_string( ShooterAnimationParameter::kShootWeight ), _shootWeight );
    }
} // namespace sw
