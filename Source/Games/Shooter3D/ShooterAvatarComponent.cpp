#include "pch.h"

#include "Games/Shooter3D/ShooterAvatarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Utility/OrientationUtil.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterAvatarComponentInternal
        {
            static constexpr float32 kMoveIdle     = 0.0f;
            static constexpr float32 kMoveWalk     = 1.0f;
            static constexpr float32 kMoveRun      = 2.0f;
            static constexpr float32 kMoveBackward = 3.0f;
            static constexpr float32 kMoveLeft     = 4.0f;
            static constexpr float32 kMoveRight    = 5.0f;
            static constexpr float32 kMoveAir      = 6.0f;
            /** @brief 레이어 가중치가 목표로 가는 빠르기(1/s)입니다. */
            static constexpr float32 kLayerBlendRate = 10.0f;

            static float32 approach( float32 current, float32 target, float32 maxStep )
            {
                return current + MathUtil::clamp( target - current, -maxStep, maxStep );
            }

            /** @brief 이동 방향 · 앞 속도를 그래프의 `Move` 코드로 바꿉니다(앞은 빠르면 달리기). */
            static float32 computeMoveCode( LocomotionDirection direction, float32 forwardSpeed, float32 runThreshold )
            {
                switch ( direction )
                {
                    case LocomotionDirection::Idle:
                        return kMoveIdle;
                    case LocomotionDirection::Forward:
                        return forwardSpeed >= runThreshold ? kMoveRun : kMoveWalk;
                    case LocomotionDirection::Backward:
                        return kMoveBackward;
                    case LocomotionDirection::Left:
                        return kMoveLeft;
                    case LocomotionDirection::Right:
                        return kMoveRight;
                    case LocomotionDirection::Airborne:
                        return kMoveAir;
                }
                return kMoveIdle;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterAvatarComponent::ShooterAvatarComponent()
        : _player{}
        , _aimLayerClip{ "1H_Ranged_Aiming" }
        , _shootLayerClip{ "1H_Ranged_Shooting" }
        , _upperBodyBone{ "spine" }
        , _walkThreshold{ 0.4f }
        , _runThreshold{ 3.0f }
        , _hitPause{ 0.45f }
        , _turnRate{ 10.0f }
        , _locomotion{ 0.25f }
        , _aimWeight{ 0.0f }
        , _shootWeight{ 0.0f }
        , _hitTimer{ 0.0f }
        , _bodyYaw{ 0.0f }
        , _lastHitReaction{ 0 }
        , _aimLayer{ -1 }
        , _shootLayer{ -1 }
        , _bLayersReady{ SW_FALSE }
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
    }

    void ShooterAvatarComponent::ensureLayers()
    {
        GameObject*                pOwner    = getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator == nullptr || _bLayersReady == SW_TRUE )
            return;
        _bLayersReady = SW_TRUE;
        AnimLayerDesc aim;
        aim._clipName     = hashed_string( _aimLayerClip );
        aim._maskRootBone = hashed_string( _upperBodyBone );
        aim._weight       = 0.0f;
        aim._bLoop        = SW_FALSE; // 겨누기 클립은 총을 올려 겨눈 자세로 끝난다 — 돌리면 끝 → 처음에서 팔이 튄다. 끝 자세를 쥔다
        _aimLayer         = _aimLayerClip.empty() ? -1 : pAnimator->addLayer( aim );
        AnimLayerDesc shoot;
        shoot._clipName     = hashed_string( _shootLayerClip );
        shoot._maskRootBone = hashed_string( _upperBodyBone );
        shoot._weight       = 0.0f;
        _shootLayer         = _shootLayerClip.empty() ? -1 : pAnimator->addLayer( shoot );
    }

    void ShooterAvatarComponent::onTick( float32 deltaTime )
    {
        using Internal = ShooterAvatarComponentInternal;
        Component::onTick( deltaTime );
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*             pObject   = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const ShooterPlayerComponent* pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        SkeletalAnimatorComponent*    pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        SceneComponent*               pScene    = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pPlayer == nullptr || pScene == nullptr || deltaTime <= 0.0f )
            return;
        const float32 step = MathUtil::min( deltaTime, 0.1f );

        // 몸은 발 자리에 서서 보는 쪽을 본다(KayKit 은 +Z 가 앞).
        // 시점이 한 프레임에 크게 돌아도(마우스 휙 · 표적 바꾸기) 몸은 각속도 상한으로 따라간다.
        _bodyYaw   = _bYawReady == SW_TRUE ? OrientationUtil::turnTowardAngle( _bodyYaw, pPlayer->getLookYaw(), _turnRate * step ) : pPlayer->getLookYaw();
        _bYawReady = SW_TRUE;
        pScene->setLocalPosition( pPlayer->getFeetPosition() );
        pScene->setLocalRotation( float3{ 0.0f, _bodyYaw, 0.0f } );
        if ( pAnimator == nullptr )
            return;
        ensureLayers();

        const bool bAlive = pPlayer->isAlive();
        if ( pPlayer->getHitReactionCount() != _lastHitReaction )
        {
            _lastHitReaction = pPlayer->getHitReactionCount();
            if ( bAlive )
            {
                _hitTimer = _hitPause;
                pAnimator->getParameters().setTrigger( hashed_string( "Hit" ) );
            }
        }
        _hitTimer -= step;
        AnimParameterSet& parameters = pAnimator->getParameters();
        // 대각선 · 문턱 근처에서 상태가 오가면 클립이 매번 처음부터 다시 돈다 — 히스테리시스로 고르고 바뀐 것은 잠깐 이어져야 받아들인다.
        const LocomotionDirection candidate = LocomotionMath::classifyFrom( _locomotion.getDirection(), pPlayer->getMoveVelocity(), _bodyYaw,
                                                                            pPlayer->isOnGround(), _walkThreshold );
        const LocomotionDirection direction = _locomotion.update( candidate, step );
        const float32             forward   = LocomotionMath::computeLocalVelocity( pPlayer->getMoveVelocity(), _bodyYaw )._x;
        parameters.setFloat( hashed_string( "Move" ), Internal::computeMoveCode( direction, forward, _runThreshold ) );
        parameters.setBool( hashed_string( "Dead" ), bAlive == false );

        // 상체 — 겨눈 자세는 늘, 쏘는 동작은 막 쏜 동안. 맞음 · 쓰러짐 동안은 비켜선다.
        const bool    bUpperBodyFree = bAlive && _hitTimer <= 0.0f;
        const float32 aimTarget      = bUpperBodyFree ? 1.0f : 0.0f;
        const float32 shootTarget    = bUpperBodyFree && pPlayer->getTimeSinceShot() < 0.2f ? 1.0f : 0.0f;
        _aimWeight                   = Internal::approach( _aimWeight, aimTarget, Internal::kLayerBlendRate * step );
        _shootWeight                 = Internal::approach( _shootWeight, shootTarget, Internal::kLayerBlendRate * 2.0f * step );
        if ( _aimLayer >= 0 )
            pAnimator->setLayerWeight( static_cast<uint32>( _aimLayer ), _aimWeight );
        if ( _shootLayer >= 0 )
            pAnimator->setLayerWeight( static_cast<uint32>( _shootLayer ), _shootWeight );
    }
} // namespace sw
