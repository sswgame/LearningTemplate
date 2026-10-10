#include "pch.h"

#include "Engine/Character/Hit/RagdollComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"

namespace sw
{
    SW_LOG_CALLER( "Ragdoll" );

    namespace
    {
        struct RagdollComponentInternal
        {
            static void decompose( const float4x4& matrix, float3& outPosition, quaternion& outRotation )
            {
                float3 scale{};
                if ( matrix.decompose( scale, outRotation, outPosition ) == false )
                {
                    outPosition = matrix.getTranslation();
                    outRotation = quaternion::Identity;
                }
                outRotation.normalize();
            }

            /** @brief XZ 평면 방향의 요(라디안)입니다. 길이가 거의 0 이면 false 입니다. */
            static bool computePlanarYaw( const float3& direction, float32& outYaw )
            {
                if ( direction._x * direction._x + direction._z * direction._z <= 1.0e-8f )
                    return false;
                outYaw = MathUtil::atan2( direction._x, direction._z );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RagdollAnimationBinding::RagdollAnimationBinding( RagdollComponent& owner )
        : _owner{ owner }
    {
    }

    bool RagdollAnimationBinding::isAnimationActive() const
    {
        if ( _owner._state != RagdollState::Animated || _owner._flinchLayer >= 0 )
            return true;
        for ( const float32 reactionTime : _owner._listReactionTime )
        {
            if ( reactionTime > 0.0f )
                return true;
        }
        return false;
    }

    void RagdollAnimationBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        if ( phase == AnimationPhase::Time )
            _owner._lastDeltaSeconds = context._deltaSeconds;
        else if ( phase == AnimationPhase::PostProcess )
            _owner.blendPhysicsPose( unit );
    }

    void RagdollAnimationBinding::finishAnimationFrame( SkeletalMeshComponent& unit )
    {
        _owner.updateWeights( unit, _owner._lastDeltaSeconds );
    }

    void RagdollAnimationBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    RagdollComponent::RagdollComponent()
        : PhysicsComponent{ PhysicsComponentPhase::Body }
        , _physicsAssetPath{}
        , _partialRootBone{ "spine" }
        , _getUpClipFaceUp{}
        , _getUpClipFaceDown{}
        , _getUpExitState{}
        , _flinchClip{}
        , _blendBackSeconds{ 0.5f }
        , _settleSpeed{ 0.15f }
        , _settleSeconds{ 0.5f }
        , _hitReactionSeconds{ 0.35f }
        , _hitReactionWeight{ 0.6f }
        , _flinchSeconds{ 0.4f }
        , _state{ RagdollState::Animated }
        , _bRagdollOnFatalHit{ true }
        , _bAutoGetUp{ false }
        , _binding{ *this }
        , _asset{}
        , _ragdoll{}
        , _listBoneName{}
        , _listBodyWeight{}
        , _listReactionTime{}
        , _listBodyDynamic{}
        , _listAppliedDynamic{}
        , _listPartialMask{}
        , _listBlendStartWeight{}
        , _listBoneWeight{}
        , _listPhysicsModel{}
        , _listKinematicStartPosition{}
        , _listKinematicTargetPosition{}
        , _listKinematicStartRotation{}
        , _listKinematicTargetRotation{}
        , _listPendingImpulse{}
        , _physicsPose{}
        , _pBoneNameSkeleton{ nullptr }
        , _pUnit{ nullptr }
        , _seenAssetReloadCount{ 0 }
        , _flinchLayer{ -1 }
        , _partialWeight{ 1.0f }
        , _settleElapsed{ 0.0f }
        , _blendBackElapsed{ 0.0f }
        , _flinchElapsed{ 0.0f }
        , _lastDeltaSeconds{ 0.0f }
        , _bHasPhysicsPose{ false }
        , _bSettled{ false }
        , _bRebuild{ false }
        , _bGettingUp{ false }
    {
    }

    RagdollComponent::~RagdollComponent()
    {
        unbindFromUnit();
    }

    void RagdollComponent::onBeginPlay()
    {
        PhysicsComponent::onBeginPlay();
        loadAsset();
        bindToUnit();
    }

    void RagdollComponent::onEndPlay()
    {
        unbindFromUnit();
        PhysicsComponent::onEndPlay();
    }

    void RagdollComponent::onPropertyChanged( hashed_string propertyName )
    {
        PhysicsComponent::onPropertyChanged( propertyName );
        static const hashed_string s_pathName( "_physicsAssetPath" );
        if ( propertyName == s_pathName )
            loadAsset();
    }

    void RagdollComponent::setPhysicsAssetPath( string_view path )
    {
        _physicsAssetPath = string{ path };
        loadAsset();
    }

    void RagdollComponent::loadAsset()
    {
        _seenAssetReloadCount = PhysicsAssetCache::getReloadCount();
        _asset                = _physicsAssetPath.empty() ? nullptr : PhysicsAssetCache::acquire( _physicsAssetPath );
        if ( _physicsAssetPath.empty() == false && _asset == nullptr )
            SW_LOG_ERROR( "'%#': physics asset '%#' could not be loaded", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)", _physicsAssetPath.c_str() );
        _bRebuild = true;
    }

    void RagdollComponent::bindToUnit()
    {
        GameObject*            pOwner = getOwner();
        SkeletalMeshComponent* pUnit  = pOwner != nullptr ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pUnit == _pUnit )
            return;
        unbindFromUnit();
        _pUnit = pUnit;
        if ( _pUnit != nullptr )
            _pUnit->addAnimationPhaseTask( &_binding );
        else
            SW_LOG_WARNING( "'%#': ragdoll has no SkeletalMeshComponent on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
    }

    void RagdollComponent::unbindFromUnit()
    {
        if ( _pUnit != nullptr )
            _pUnit->removeAnimationPhaseTask( &_binding );
        _pUnit = nullptr;
    }

    void RagdollComponent::refreshBoneNames( const Skeleton& skeleton )
    {
        if ( _pBoneNameSkeleton == &skeleton && _listBoneName.size() == skeleton.getBoneCount() )
            return;
        _pBoneNameSkeleton = &skeleton;
        _listBoneName.resize( skeleton.getBoneCount() );
        for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
        {
            _listBoneName[boneIndex] = skeleton.getBone( boneIndex )._name;
        }
    }

    PhysicsSkeletonView RagdollComponent::makeSkeletonView( const SkeletalMeshComponent& unit ) const
    {
        const vector<int32>&    listParent = unit.getSkeleton().getParentIndices();
        const vector<float4x4>& listModel  = unit.getModelSpaceTransforms();
        PhysicsSkeletonView     view;
        view._listBoneName       = span<const hashed_string>{ _listBoneName.data(), _listBoneName.size() };
        view._listParentIndex    = span<const int32>{ listParent.data(), listParent.size() };
        view._listModelSpaceBone = span<const float4x4>{ listModel.data(), listModel.size() };
        return view;
    }

    void RagdollComponent::setGetUpClips( const hashed_string& faceUpClip, const hashed_string& faceDownClip, const hashed_string& exitState )
    {
        _getUpClipFaceUp   = faceUpClip;
        _getUpClipFaceDown = faceDownClip;
        _getUpExitState    = exitState;
    }

    float32 RagdollComponent::getBodyWeight( uint32 bodyIndex ) const
    {
        return bodyIndex < _listBodyWeight.size() ? _listBodyWeight[bodyIndex] : 0.0f;
    }

    bool RagdollComponent::isBodyDynamic( uint32 bodyIndex ) const
    {
        return bodyIndex < _listAppliedDynamic.size() && _listAppliedDynamic[bodyIndex] == SW_TRUE;
    }

    void RagdollComponent::markSubtree( int32 bodyIndex, vector<uint8>& inoutListMask ) const
    {
        const size_t bodyCount = _ragdoll._listBody.size();
        inoutListMask.resize( bodyCount, SW_FALSE );
        if ( bodyIndex < 0 || static_cast<size_t>( bodyIndex ) >= bodyCount )
            return;
        // 부모가 뒤에 올 수도 있다(에셋 순서) — 바뀌지 않을 때까지 내려간다.
        inoutListMask[static_cast<size_t>( bodyIndex )] = SW_TRUE;
        bool bChanged                                   = true;
        while ( bChanged )
        {
            bChanged = false;
            for ( size_t body = 0; body < bodyCount; ++body )
            {
                const int32 parent = _ragdoll._listParentBody[body];
                if ( inoutListMask[body] == SW_FALSE && parent >= 0 && inoutListMask[static_cast<size_t>( parent )] == SW_TRUE )
                {
                    inoutListMask[body] = SW_TRUE;
                    bChanged            = true;
                }
            }
        }
    }

    void RagdollComponent::detachBoneBodies( const vector<hashed_string>& listBone )
    {
        ScenePhysics*    pPhysics = getScenePhysics();
        IPhysicsScene3D* pScene   = pPhysics != nullptr ? pPhysics->findScene3D() : nullptr;
        if ( pScene == nullptr || _asset == nullptr )
            return;
        const size_t  bodyCount = _ragdoll._listBody.size();
        vector<uint8> listDetached( bodyCount, SW_FALSE );
        for ( size_t body = 0; body < bodyCount && body < _asset->_listBody.size(); ++body )
        {
            if ( std::find( listBone.begin(), listBone.end(), _asset->_listBody[body]._bone ) != listBone.end() )
                listDetached[body] = SW_TRUE;
        }
        // 관절을 먼저 — 빠진 바디를 잇는 관절이 남으면 솔버가 넓은 단계 밖의 바디를 만진다.
        for ( size_t body = 0; body < bodyCount; ++body )
        {
            const int32 parent           = _ragdoll._listParentBody[body];
            const bool  bTouchesDetached = listDetached[body] == SW_TRUE || ( parent >= 0 && listDetached[static_cast<size_t>( parent )] == SW_TRUE );
            if ( bTouchesDetached && _ragdoll._listJoint[body].isValid() )
            {
                pScene->destroyJoint( _ragdoll._listJoint[body] );
                _ragdoll._listJoint[body] = PhysicsJointHandle{};
            }
        }
        for ( size_t body = 0; body < bodyCount; ++body )
        {
            if ( listDetached[body] == SW_TRUE )
                pScene->setBodyEnabled( _ragdoll._listBody[body], false );
        }
    }

    const PhysicsHitZoneDef* RagdollComponent::findHitZone( PhysicsBodyHandle body, int32& outBodyIndex ) const
    {
        outBodyIndex = _ragdoll.findBodyIndex( body );
        if ( outBodyIndex < 0 || _asset == nullptr || static_cast<size_t>( outBodyIndex ) >= _asset->_listBody.size() )
            return nullptr;
        const PhysicsHitZoneDef& zone = _asset->_listBody[static_cast<size_t>( outBodyIndex )]._hitZone;
        return zone._name.empty() ? nullptr : &zone;
    }

    void RagdollComponent::setControllerActive( bool bActive )
    {
        GameObject*                   pOwner      = getOwner();
        CharacterControllerComponent* pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr && pController->isSelfActive() != bActive )
            pController->setActive( bActive );
    }

    void RagdollComponent::startRagdoll( const float3& impulse, int32 bodyIndex )
    {
        _state         = RagdollState::Ragdoll;
        _settleElapsed = 0.0f;
        _bSettled      = false;
        _bGettingUp    = false;
        if ( bodyIndex >= 0 && impulse.getLengthSquared() > 0.0f )
            _listPendingImpulse.push_back( PendingImpulse{ impulse, float3{}, bodyIndex } );
        setControllerActive( false );
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    void RagdollComponent::startPartialRagdoll( float32 weight )
    {
        if ( _state == RagdollState::Ragdoll || _pUnit == nullptr )
            return;
        const int32 boneIndex = _pUnit->getSkeleton().findBoneIndex( _partialRootBone );
        const int32 bodyIndex =
            ( boneIndex >= 0 && static_cast<size_t>( boneIndex ) < _ragdoll._listBodyOfBone.size() ) ? _ragdoll._listBodyOfBone[static_cast<size_t>( boneIndex )] : -1;
        if ( bodyIndex < 0 )
        {
            SW_LOG_WARNING( "'%#': partial ragdoll root bone '%#' has no body", getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)",
                            _partialRootBone.c_str() );
            return;
        }
        _listPartialMask.clear();
        markSubtree( bodyIndex, _listPartialMask );
        _partialWeight = MathUtil::saturate( weight );
        _state         = RagdollState::Partial;
        _pUnit->markPoseDirty();
    }

    void RagdollComponent::stopPartialRagdoll()
    {
        if ( _state != RagdollState::Partial )
            return;
        _listBlendStartWeight = _listBodyWeight;
        _blendBackElapsed     = 0.0f;
        _state                = RagdollState::BlendingBack;
    }

    void RagdollComponent::applyHitReaction( int32 bodyIndex, const float3& impulse, const float3& point )
    {
        if ( bodyIndex < 0 || static_cast<size_t>( bodyIndex ) >= _ragdoll._listBody.size() )
            return;
        if ( impulse.getLengthSquared() > 0.0f )
            _listPendingImpulse.push_back( PendingImpulse{ impulse, point, bodyIndex } );
        if ( _state == RagdollState::Ragdoll )
            return;
        vector<uint8> listMask;
        markSubtree( bodyIndex, listMask );
        _listReactionTime.resize( _ragdoll._listBody.size(), 0.0f );
        for ( size_t body = 0; body < listMask.size(); ++body )
        {
            if ( listMask[body] == SW_TRUE )
                _listReactionTime[body] = _hitReactionSeconds;
        }
        if ( _pUnit != nullptr )
            _pUnit->markPoseDirty();
    }

    void RagdollComponent::onHitReceived( const HitInfo& hit )
    {
        PhysicsComponent::onHitReceived( hit );
        int32 bodyIndex = hit._bodyIndex;
        if ( bodyIndex < 0 )
            (void)findHitZone( hit._body, bodyIndex );
        const float3 impulse = hit._direction * hit._impulse;
        if ( hit._bFatal && _bRagdollOnFatalHit )
        {
            startRagdoll( impulse, bodyIndex );
            return;
        }
        applyHitReaction( bodyIndex, impulse, hit._point );
        // 가산 움찔 — 한 번 도는 레이어를 처음부터 다시, 가중치는 `_flinchSeconds` 동안 1 → 0.
        GameObject*                pOwner    = getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( _flinchClip.empty() || pAnimator == nullptr || _state == RagdollState::Ragdoll )
            return;
        if ( _flinchLayer < 0 )
            _flinchLayer = pAnimator->addLayer( AnimLayerDesc{ _flinchClip, hashed_string{}, 1.0f, AnimLayerBlend::Additive, SW_FALSE } );
        if ( _flinchLayer < 0 )
            return;
        pAnimator->restartLayer( static_cast<uint32>( _flinchLayer ) );
        pAnimator->setLayerWeight( static_cast<uint32>( _flinchLayer ), 1.0f );
        _flinchElapsed = 0.0f;
    }

    void RagdollComponent::updateWeights( SkeletalMeshComponent& unit, float32 deltaSeconds )
    {
        // 움찔 레이어 가중치.
        GameObject*                pOwner    = unit.getOwner();
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( _flinchLayer >= 0 && pAnimator != nullptr )
        {
            _flinchElapsed += deltaSeconds;
            const float32 weight = _flinchSeconds > 0.0f ? MathUtil::saturate( 1.0f - _flinchElapsed / _flinchSeconds ) : 0.0f;
            pAnimator->setLayerWeight( static_cast<uint32>( _flinchLayer ), weight );
        }
        // 기상 클립이 끝나면 다음 상태로.
        if ( _bGettingUp && pAnimator != nullptr && pAnimator->getGraphPlayer().getPlayer().hasFinished() )
        {
            _bGettingUp = false;
            if ( _getUpExitState.empty() == false )
                (void)pAnimator->play( _getUpExitState, true, 0.2f );
        }

        const size_t bodyCount = _ragdoll._listBody.size();
        if ( bodyCount == 0 )
            return;
        _listReactionTime.resize( bodyCount, 0.0f );
        _listBodyWeight.resize( bodyCount, 0.0f );
        _listBodyDynamic.resize( bodyCount, SW_FALSE );
        _listPartialMask.resize( bodyCount, SW_FALSE );
        _listBlendStartWeight.resize( bodyCount, 1.0f );
        for ( float32& reactionTime : _listReactionTime )
        {
            reactionTime = MathUtil::max( 0.0f, reactionTime - deltaSeconds );
        }
        float32 blendFactor = 0.0f;
        if ( _state == RagdollState::BlendingBack )
        {
            _blendBackElapsed += deltaSeconds;
            blendFactor = _blendBackSeconds > 0.0f ? MathUtil::saturate( 1.0f - _blendBackElapsed / _blendBackSeconds ) : 0.0f;
            if ( blendFactor <= 0.0f )
            {
                _state           = RagdollState::Animated;
                _bHasPhysicsPose = false;
            }
        }

        for ( size_t body = 0; body < bodyCount; ++body )
        {
            float32 stateWeight = 0.0f;
            bool    bDynamic    = false;
            switch ( _state )
            {
                case RagdollState::Animated:
                {
                    break;
                }
                case RagdollState::Ragdoll:
                {
                    stateWeight = 1.0f;
                    bDynamic    = true;
                    break;
                }
                case RagdollState::Partial:
                {
                    const bool bInPartial = _listPartialMask[body] == SW_TRUE;
                    stateWeight           = bInPartial ? _partialWeight : 0.0f;
                    bDynamic              = bInPartial;
                    break;
                }
                case RagdollState::BlendingBack:
                {
                    stateWeight = _listBlendStartWeight[body] * blendFactor;
                    break;
                }
            }
            const float32 reactionWeight = ( _hitReactionSeconds > 0.0f && _listReactionTime[body] > 0.0f )
                                             ? _hitReactionWeight * _listReactionTime[body] / _hitReactionSeconds
                                             : 0.0f;
            const bool    bReacting      = reactionWeight > 0.0f && _state != RagdollState::BlendingBack;
            _listBodyWeight[body]        = MathUtil::max( stateWeight, bReacting ? reactionWeight : 0.0f );
            _listBodyDynamic[body]       = ( bDynamic || bReacting ) ? SW_TRUE : SW_FALSE;
        }

        // 뼈 가중치 — 바디가 있으면 그 바디, 없으면 가장 가까운 조상의 것(손가락은 손을 따른다).
        const Skeleton&      skeleton   = unit.getSkeleton();
        const vector<int32>& listParent = skeleton.getParentIndices();
        _listBoneWeight.assign( skeleton.getBoneCount(), 0.0f );
        for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount() && boneIndex < _ragdoll._listBodyOfBone.size(); ++boneIndex )
        {
            const int32 body   = _ragdoll._listBodyOfBone[boneIndex];
            const int32 parent = listParent[boneIndex];
            if ( body >= 0 )
                _listBoneWeight[boneIndex] = _listBodyWeight[static_cast<size_t>( body )];
            else if ( parent >= 0 )
                _listBoneWeight[boneIndex] = _listBoneWeight[static_cast<size_t>( parent )];
        }
    }

    void RagdollComponent::blendPhysicsPose( SkeletalMeshComponent& unit )
    {
        const Skeleton& skeleton  = unit.getSkeleton();
        const uint32    boneCount = skeleton.getBoneCount();
        if ( _bHasPhysicsPose == false || _listPhysicsModel.size() != boneCount || _listBoneWeight.size() != boneCount )
            return;
        bool bAnyWeight = false;
        for ( const float32 weight : _listBoneWeight )
        {
            bAnyWeight = bAnyWeight || weight > 0.0f;
        }
        if ( bAnyWeight == false )
            return;
        const vector<int32>& listParent = skeleton.getParentIndices();
        _physicsPose.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const int32    parent = listParent[boneIndex];
            const float4x4 local  = parent < 0 ? _listPhysicsModel[boneIndex] : _listPhysicsModel[boneIndex] * _listPhysicsModel[static_cast<size_t>( parent )].invert();
            _physicsPose.setBoneTransform( boneIndex, BoneTransform::makeFromMatrix( local ) );
        }
        Pose& pose = unit.getLocalPose();
        Pose::blendMasked( pose, _physicsPose, 1.0f, _listBoneWeight.data(), pose );
    }

    bool RagdollComponent::getUp()
    {
        GameObject*                pOwner    = getOwner();
        SceneComponent*            pRoot     = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        SkeletalAnimatorComponent* pAnimator = pOwner != nullptr ? pOwner->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( _state != RagdollState::Ragdoll || _bHasPhysicsPose == false || _pUnit == nullptr || pRoot == nullptr || pAnimator == nullptr )
            return false;
        const Skeleton& skeleton  = _pUnit->getSkeleton();
        const uint32    boneCount = skeleton.getBoneCount();
        if ( _listPhysicsModel.size() != boneCount )
            return false;
        // 골반(부모 바디가 없는 바디의 뼈)과 위 몸(부분 래그돌 뿌리 뼈)으로 누운 방향을 잰다.
        int32 pelvisBone = -1;
        for ( size_t body = 0; body < _ragdoll._listParentBody.size() && pelvisBone < 0; ++body )
        {
            if ( _ragdoll._listParentBody[body] < 0 )
                pelvisBone = _ragdoll._listBoneIndex[body];
        }
        const int32 upperBone = skeleton.findBoneIndex( _partialRootBone );
        if ( pelvisBone < 0 || upperBone < 0 )
            return false;
        const float4x4      oldUnitWorld  = _pUnit->getWorldMatrix();
        const float4x4      pelvisWorld   = _listPhysicsModel[static_cast<size_t>( pelvisBone )] * oldUnitWorld;
        const float4x4      upperWorld    = _listPhysicsModel[static_cast<size_t>( upperBone )] * oldUnitWorld;
        const float3        pelvisForward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pelvisWorld );
        const bool          bFaceUp       = pelvisForward._y >= 0.0f;
        const hashed_string clipName      = ( bFaceUp || _getUpClipFaceDown.empty() ) ? _getUpClipFaceUp : _getUpClipFaceDown;
        if ( clipName.empty() || pAnimator->preloadClip( clipName ) == false )
            return false;
        const AnimClip* pClip = pAnimator->findClip( clipName );
        if ( pClip == nullptr )
            return false;

        // 기상 클립의 첫 자세(모델 공간) — 그 골반 → 위 몸 방향을 래그돌의 것에 맞추도록 오브젝트를 돌리고 옮긴다.
        Pose startPose;
        startPose.setToReference( skeleton );
        Pose trackPose;
        if ( pClip->sampleTracks( 0.0f, trackPose ) )
        {
            vector<int32> listTrackToBone;
            pClip->makeTrackToBoneMap( skeleton, listTrackToBone );
            AnimClip::copyTracksToPose( trackPose, listTrackToBone, startPose );
        }
        vector<float4x4> listStartModel;
        startPose.computeModelSpace( skeleton.getParentIndices(), listStartModel );
        const float3 clipPelvis = listStartModel[static_cast<size_t>( pelvisBone )].getTranslation();
        float32      clipYaw    = 0.0f;
        float32      ragdollYaw = 0.0f;
        const bool   bClipDirection =
            RagdollComponentInternal::computePlanarYaw( listStartModel[static_cast<size_t>( upperBone )].getTranslation() - clipPelvis, clipYaw );
        const bool       bRagdollDirection = RagdollComponentInternal::computePlanarYaw( upperWorld.getTranslation() - pelvisWorld.getTranslation(), ragdollYaw );
        const float32    yaw               = ( bClipDirection && bRagdollDirection ) ? ragdollYaw - clipYaw : 0.0f;
        const quaternion rotation          = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, yaw );
        const float3     offset            = float3::transform( float3{ clipPelvis._x, 0.0f, clipPelvis._z }, rotation );
        const float3     pelvis            = pelvisWorld.getTranslation();
        const float3     rootPosition{ pelvis._x - offset._x, pRoot->getWorldPosition()._y, pelvis._z - offset._z };
        pRoot->setWorldPosition( rootPosition );
        pRoot->setLocalRotation( rotation.getEulerAngles() );

        // 래그돌 자세를 새 오브젝트 자리 기준으로 옮겨 둔다 — 그 자세에서 클립으로 섞는다.
        const float4x4 newUnitInverse = _pUnit->getWorldMatrix().invert();
        for ( float4x4& model : _listPhysicsModel )
        {
            model = model * oldUnitWorld * newUnitInverse;
        }
        _listBlendStartWeight.assign( _ragdoll._listBody.size(), 1.0f );
        _blendBackElapsed = 0.0f;
        _state            = RagdollState::BlendingBack;
        _bGettingUp       = true;
        (void)pAnimator->play( clipName, false, 0.0f );
        setControllerActive( true );
        _pUnit->markPoseDirty();
        return true;
    }

    void RagdollComponent::beginPhysicsFrame( ScenePhysics& physics )
    {
        if ( isSimulated() == false )
        {
            releasePhysics( physics );
            return;
        }
        IPhysicsScene3D* pScene = physics.getScene3D();
        if ( pScene == nullptr || _pUnit == nullptr )
            return;
        if ( _seenAssetReloadCount != PhysicsAssetCache::getReloadCount() )
            loadAsset(); // 물리 에셋 파일을 고쳤다 — 다시 세운다
        if ( consumeRebuild() || _bRebuild )
        {
            releasePhysics( physics );
            _bRebuild = false;
        }
        if ( _ragdoll.isEmpty() && _asset != nullptr )
        {
            refreshBoneNames( _pUnit->getSkeleton() );
            PhysicsRagdollOptions options;
            options._pSettings = physics.findSettings();
            options._userData  = getOwner()->getObjectID();
            options._bodyType  = PhysicsBodyType::Kinematic;
            if ( PhysicsRagdollBuilder::create( *pScene, *_asset, makeSkeletonView( *_pUnit ), _pUnit->getWorldMatrix(), options, _ragdoll ) == false )
            {
                _asset = nullptr; // 같은 오류를 프레임마다 내지 않는다 — 에셋을 고치면 다시 읽힌다
                return;
            }
            const size_t bodyCount = _ragdoll._listBody.size();
            _listAppliedDynamic.assign( bodyCount, SW_FALSE );
            _listBodyDynamic.resize( bodyCount, SW_FALSE );
            _listBodyWeight.resize( bodyCount, 0.0f );
            _listKinematicStartPosition.resize( bodyCount );
            _listKinematicTargetPosition.resize( bodyCount );
            _listKinematicStartRotation.resize( bodyCount );
            _listKinematicTargetRotation.resize( bodyCount );
            (void)consumeTeleport();
        }
        if ( _ragdoll.isEmpty() )
            return;

        // 바디 종류 — 가중치가 0 보다 크면 동적. 맞음 반응만으로 동적인 바디는 중력을 받지 않는다(축 처지지 않고 충격만 받는다).
        const bool bLimp = _state == RagdollState::Ragdoll || _state == RagdollState::Partial;
        for ( size_t body = 0; body < _ragdoll._listBody.size(); ++body )
        {
            const uint8 bWantDynamic = body < _listBodyDynamic.size() ? _listBodyDynamic[body] : SW_FALSE;
            // 꺼진 바디(잘려 나간 영역)는 시뮬레이션 밖이다 — 종류를 바꾸지 않는다.
            if ( bWantDynamic == _listAppliedDynamic[body] || pScene->isBodyEnabled( _ragdoll._listBody[body] ) == false )
                continue;
            pScene->setBodyType( _ragdoll._listBody[body], bWantDynamic == SW_TRUE ? PhysicsBodyType::Dynamic : PhysicsBodyType::Kinematic );
            const bool bPartialBody = _state == RagdollState::Partial && body < _listPartialMask.size() && _listPartialMask[body] == SW_TRUE;
            pScene->setGravityFactor( _ragdoll._listBody[body], ( _state == RagdollState::Ragdoll || bPartialBody ) && bLimp ? 1.0f : 0.0f );
            _listAppliedDynamic[body] = bWantDynamic;
        }
        for ( const PendingImpulse& impulse : _listPendingImpulse )
        {
            if ( impulse._bodyIndex < 0 || static_cast<size_t>( impulse._bodyIndex ) >= _ragdoll._listBody.size() )
                continue;
            const PhysicsBodyHandle body = _ragdoll._listBody[static_cast<size_t>( impulse._bodyIndex )];
            if ( pScene->isBodyEnabled( body ) == false )
                continue;
            if ( impulse._point.getLengthSquared() > 0.0f )
                pScene->addImpulseAtPoint( body, impulse._impulse, impulse._point );
            else
                pScene->addImpulse( body, impulse._impulse );
        }
        _listPendingImpulse.clear();
        // 코드가 오브젝트를 순간이동시켰다 — 키네마틱 바디도 그 자리로 바로 옮긴다.
        if ( consumeTeleport() )
            PhysicsRagdollBuilder::driveToPose( *pScene, _ragdoll, makeSkeletonView( *_pUnit ), _pUnit->getWorldMatrix(), 0.0f );
    }

    void RagdollComponent::prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount )
    {
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _ragdoll.isEmpty() || _pUnit == nullptr )
            return;
        const size_t            bodyCount = _ragdoll._listBody.size();
        const vector<float4x4>& listModel = _pUnit->getModelSpaceTransforms();
        if ( stepIndex == 0 )
        {
            // 이번 프레임의 포즈가 키네마틱 바디의 목표다 — 지금 바디 자리에서 프레임의 스텝 수로 나눠 간다(밀린 동적 바디가 그 속도를 받는다).
            const float4x4 unitWorld = _pUnit->getWorldMatrix();
            for ( size_t body = 0; body < bodyCount; ++body )
            {
                const size_t boneIndex = static_cast<size_t>( _ragdoll._listBoneIndex[body] );
                if ( boneIndex >= listModel.size() )
                    continue;
                RagdollComponentInternal::decompose( listModel[boneIndex] * unitWorld, _listKinematicTargetPosition[body], _listKinematicTargetRotation[body] );
                if ( pScene->getBodyTransform( _ragdoll._listBody[body], _listKinematicStartPosition[body], _listKinematicStartRotation[body] ) == false )
                {
                    _listKinematicStartPosition[body] = _listKinematicTargetPosition[body];
                    _listKinematicStartRotation[body] = _listKinematicTargetRotation[body];
                }
            }
        }
        const float32 fraction = static_cast<float32>( stepIndex + 1 ) / static_cast<float32>( stepCount > 0 ? stepCount : 1 );
        for ( size_t body = 0; body < bodyCount; ++body )
        {
            if ( _listAppliedDynamic[body] == SW_TRUE || pScene->isBodyEnabled( _ragdoll._listBody[body] ) == false )
                continue;
            const float3     position = float3::lerp( _listKinematicStartPosition[body], _listKinematicTargetPosition[body], fraction );
            const quaternion rotation = quaternion::slerp( _listKinematicStartRotation[body], _listKinematicTargetRotation[body], fraction );
            pScene->moveKinematic( _ragdoll._listBody[body], position, rotation, fixedDeltaTime );
        }
    }

    void RagdollComponent::endPhysicsFrame( ScenePhysics& physics, float32 alpha )
    {
        (void)alpha;
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene == nullptr || _ragdoll.isEmpty() || _pUnit == nullptr )
            return;
        // 돌아가는 동안은 시작할 때의 래그돌 자세를 그대로 둔다(바디는 이미 애니메이션을 따른다).
        if ( _state == RagdollState::BlendingBack )
            return;
        bool bAnyDynamic = false;
        for ( const uint8 bDynamic : _listAppliedDynamic )
        {
            bAnyDynamic = bAnyDynamic || bDynamic == SW_TRUE;
        }
        if ( bAnyDynamic == false )
        {
            _bHasPhysicsPose = false;
            return;
        }
        _listPhysicsModel.resize( _pUnit->getSkeleton().getBoneCount() );
        PhysicsRagdollBuilder::readBoneTransforms( *pScene, _ragdoll, makeSkeletonView( *_pUnit ), _pUnit->getWorldMatrix(),
                                                   span<float4x4>{ _listPhysicsModel.data(), _listPhysicsModel.size() } );
        _bHasPhysicsPose = true;
        if ( _state != RagdollState::Ragdoll )
            return;
        float32 maxSpeed = 0.0f;
        for ( const PhysicsBodyHandle& body : _ragdoll._listBody )
        {
            maxSpeed = MathUtil::max( maxSpeed, pScene->getLinearVelocity( body ).getLength() );
        }
        _settleElapsed = maxSpeed < _settleSpeed ? _settleElapsed + _lastDeltaSeconds : 0.0f;
        _bSettled      = _settleElapsed >= _settleSeconds;
        if ( _bSettled && _bAutoGetUp )
            (void)getUp();
    }

    void RagdollComponent::releasePhysics( ScenePhysics& physics )
    {
        IPhysicsScene3D* pScene = physics.findScene3D();
        if ( pScene != nullptr && _ragdoll.isEmpty() == false )
            PhysicsRagdollBuilder::destroy( *pScene, _ragdoll );
        _ragdoll = PhysicsRagdoll{};
        _listAppliedDynamic.clear();
        _bHasPhysicsPose = false;
    }
} // namespace sw
