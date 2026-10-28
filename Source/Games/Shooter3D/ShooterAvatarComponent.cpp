#include "pch.h"

#include "Games/Shooter3D/ShooterAvatarComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/LocomotionSpeedUtil.h"
#include "Engine/Automation/Probe/AutomationProbe.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"

#include "Games/Shooter3D/ShooterAnimationParameter.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterAvatar" );

    namespace
    {
        struct ShooterAvatarComponentInternal
        {
            /** @brief 이동 방향 · 앞 속도를 그래프의 `Move` 코드로 바꿉니다(앞은 빠르면 달리기). */
            static float32 computeMoveCode( LocomotionDirection direction, float32 forwardSpeed, float32 runThreshold )
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

            /** @brief 시나리오 프로브가 보는 첫 몸입니다(드물게 불리므로 씬을 훑습니다). */
            static const ShooterAvatarComponent* findFirstAvatar( const GameObjectManager* pManager )
            {
                const ShooterAvatarComponent* pFound = nullptr;
                if ( pManager == nullptr )
                    return pFound;
                pManager->forEachComponentOfType<ShooterAvatarComponent>( [&pFound]( ShooterAvatarComponent* pAvatar )
                {
                    if ( pFound == nullptr )
                        pFound = pAvatar;
                } );
                return pFound;
            }

            /** @brief 첫 몸이 보폭 워핑 노드에 마지막으로 넣은 배율입니다. 리그가 아직 묶이지 않았으면 프로브가 없는 것으로 보고합니다. */
            [[nodiscard]] static bool readStrideScale( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterAvatarComponent* pAvatar = findFirstAvatar( pManager );
                if ( pAvatar == nullptr || pAvatar->getStrideAppliedCount() == 0 )
                    return false;
                outValue = static_cast<float64>( pAvatar->getLastStrideScale() );
                return true;
            }

            /** @brief 첫 몸이 리그 준비 뒤 보폭 워핑 값을 넣은 프레임 수입니다. */
            [[nodiscard]] static bool readStrideAppliedCount( const GameObjectManager* pManager, float64& outValue )
            {
                const ShooterAvatarComponent* pAvatar = findFirstAvatar( pManager );
                if ( pAvatar == nullptr )
                    return false;
                outValue = static_cast<float64>( pAvatar->getStrideAppliedCount() );
                return true;
            }
        };
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
        , _firstPersonHeadSlot{}
        , _listFirstPersonHiddenSlot{}
        , _strideNode{ "Stride" }
        , _locomotion{ 0.25f }
        , _mapClipFootSpeed{}
        , _aimWeight{ 0.0f }
        , _shootWeight{ 0.0f }
        , _hitTimer{ 0.0f }
        , _bodyYaw{ 0.0f }
        , _lastStrideScale{ 1.0f }
        , _strideAppliedCount{ 0 }
        , _lastHitReaction{ 0 }
        , _bYawReady{ SW_FALSE }
        , _bStrideErrorShown{ SW_FALSE }
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

    void ShooterAvatarComponent::setFirstPerson( bool bFirstPerson )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        PoseModifierComponent* pRig = pOwner->getComponent<PoseModifierComponent>();
        if ( pRig == nullptr || _firstPersonHeadSlot.empty() )
        {
            SW_LOG_ERROR( "[Shooter] body '%#' needs a PoseModifierComponent and _firstPersonHeadSlot to hide the head in first person", pOwner->getName().c_str() );
        }
        else
        {
            // 묶인 리그에 그 슬롯을 쓰는 노드가 없으면 데이터 오류다 — 슬롯 가중치는 아무 노드에도 닿지 않고 머리가 보인다.
            const RigInstance& instance = pRig->getRigInstance();
            bool               bFound   = instance.isInitialized() == false;
            for ( uint32 nodeIndex = 0; bFound == false && nodeIndex < instance.getNodeCount(); ++nodeIndex )
            {
                bFound = instance.getNode( nodeIndex ).getWeightSlot() == _firstPersonHeadSlot;
            }
            if ( bFound == false )
                SW_LOG_ERROR( "[Shooter] rig '%#' has no node with weight_slot '%#' - the head stays visible in first person", pRig->getRigPath().c_str(),
                              _firstPersonHeadSlot.c_str() );
            // 슬롯 가중치 1 = ScaleBone 이 머리를 0 으로 모은다, 0 = 그대로.
            pRig->setSlotWeight( _firstPersonHeadSlot, bFirstPerson ? 1.0f : 0.0f );
        }
        CharacterAppearanceComponent* pAppearance = pOwner->getComponent<CharacterAppearanceComponent>();
        if ( pAppearance != nullptr )
        {
            pAppearance->setPartsVisible( true );
            pAppearance->setHiddenSlots( bFirstPerson ? _listFirstPersonHiddenSlot : vector<hashed_string>{} );
        }
    }

    float32 ShooterAvatarComponent::computeLocomotionFootSpeed( const SkeletalAnimatorComponent& animator, LocomotionDirection direction )
    {
        const bool bLocomotion = direction != LocomotionDirection::Idle && direction != LocomotionDirection::Airborne;
        if ( bLocomotion == false )
            return 0.0f;
        const AnimationClip*         pClip  = animator.findClip( animator.getCurrentStateName() );
        const GameObject*            pOwner = getOwner();
        const SkeletalMeshComponent* pUnit  = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pClip == nullptr || pUnit == nullptr )
            return 0.0f;
        auto found = _mapClipFootSpeed.find( pClip );
        if ( found == _mapClipFootSpeed.end() )
        {
            // 클립마다 한 번만 측정한다(표본 수만큼 디코드한다). 상태가 처음 바뀌는 프레임에만 든다.
            const float32 footSpeed = LocomotionSpeedUtil::computeFootSpeed( *pClip, pUnit->getSkeleton(), hashed_string( _leftFootBone ), hashed_string( _rightFootBone ),
                                                                             LocomotionSpeedUtil::kDefaultSampleCount );
            found                   = _mapClipFootSpeed.emplace( pClip, footSpeed ).first;
        }
        if ( found->second < _minLocomotionFootSpeed )
            return 0.0f;
        return found->second;
    }

    void ShooterAvatarComponent::applyStrideWarping( float32 strideScale, const float2& localVelocity )
    {
        GameObject*            pOwner    = getOwner();
        PoseModifierComponent* pModifier = pOwner != nullptr ? pOwner->getComponent<PoseModifierComponent>() : nullptr;
        // 리그는 메시(외형)가 묶인 뒤에 준비된다 — 그 전 프레임은 넣을 곳이 없다.
        if ( pModifier == nullptr || pModifier->isRigReady() == false )
            return;
        const hashed_string node( _strideNode );
        const bool          bScale     = pModifier->setNodeControl( node, hashed_string( "stride_scale" ), strideScale );
        const bool          bDirection = pModifier->setNodeControl( node, hashed_string( "direction_x" ), localVelocity._y ) && pModifier->setNodeControl( node, hashed_string( "direction_z" ), localVelocity._x );
        const bool          bMissing   = bScale == false || bDirection == false;
        _lastStrideScale               = strideScale;
        ++_strideAppliedCount;
        if ( bMissing && _bStrideErrorShown == SW_FALSE )
        {
            _bStrideErrorShown = SW_TRUE;
            SW_LOG_ERROR( "[Shooter] pose modifier rig '%#' has no StrideWarping node '%#' - foot sliding beyond the play rate range stays", pModifier->getRigPath(), _strideNode );
        }
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
        const LocomotionDirection candidate     = LocomotionMath::classifyFrom( _locomotion.getDirection(), pPlayer->getMoveVelocity(), _bodyYaw,
                                                                                pPlayer->isOnGround(), _walkThreshold );
        const LocomotionDirection direction     = _locomotion.update( candidate, step );
        const float2              localVelocity = LocomotionMath::computeLocalVelocity( pPlayer->getMoveVelocity(), _bodyYaw );
        parameters.setFloat( hashed_string( ShooterAnimationParameter::kMove ), ShooterAvatarComponentInternal::computeMoveCode( direction, localVelocity._x, _runThreshold ) );
        parameters.setBool( hashed_string( ShooterAnimationParameter::kDead ), bAlive == false );
        // 발 미끄러짐 — 이동 클립의 발 속도에 실제 수평 속도를 맞추고(언리얼 재생 속도 매칭), 범위에 막힌 몫은 보폭 워핑이 맡는다. 쓰러지면 클립 그대로.
        const float3& velocity    = pPlayer->getMoveVelocity();
        const float32 moveSpeed   = MathUtil::sqrt( velocity._x * velocity._x + velocity._z * velocity._z );
        const float32 footSpeed   = bAlive ? computeLocomotionFootSpeed( *pAnimator, direction ) : 0.0f;
        const float32 playRate    = LocomotionSpeedUtil::computePlayRate( moveSpeed, footSpeed, _minPlayRate, _maxPlayRate );
        const float32 strideScale = LocomotionSpeedUtil::computeStrideScale( moveSpeed, footSpeed, playRate );
        pAnimator->setPlayRate( playRate );
        applyStrideWarping( strideScale, localVelocity );

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

namespace sw
{
    SW_AUTOMATION_PROBE( shooterStrideScale, "Shooter3D.StrideScale", "Stride scale the first shooter body last gave its StrideWarping node (1 = clip as is); absent until the rig is bound",
                         &ShooterAvatarComponentInternal::readStrideScale );
    SW_AUTOMATION_PROBE( shooterStrideAppliedCount, "Shooter3D.StrideAppliedCount", "Frames the first shooter body gave its StrideWarping node a value (0 while the rig is not bound to the mesh)",
                         &ShooterAvatarComponentInternal::readStrideAppliedCount );
} // namespace sw
