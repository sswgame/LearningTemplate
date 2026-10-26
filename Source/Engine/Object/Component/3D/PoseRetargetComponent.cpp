#include "pch.h"

#include "Engine/Object/Component/3D/PoseRetargetComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "PoseRetarget" );

    PoseRetargetBinding::PoseRetargetBinding( PoseRetargetComponent& owner )
        : _owner{ owner }
    {
    }

    bool PoseRetargetBinding::isAnimationActive() const
    {
        return _owner._bProfileReady == SW_TRUE && _owner._source.isValid();
    }

    AnimationPoseAccess PoseRetargetBinding::getPoseAccess() const
    {
        return _owner.isRetargeting() ? AnimationPoseAccess::Writes : AnimationPoseAccess::ReadsOnly;
    }

    void PoseRetargetBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context )
    {
        (void)context;
        if ( phase == AnimationPhase::BasePose )
            _owner.populateBasePose( unit );
    }

    void PoseRetargetBinding::onAnimationUnitDetached( SkeletalMeshComponent& unit )
    {
        if ( _owner._pUnit == &unit )
            _owner._pUnit = nullptr;
    }

    PoseRetargetComponent::PoseRetargetComponent()
        : _profilePath{}
        , _binding{ *this }
        , _profile{}
        , _retargeter{}
        , _targetReference{}
        , _source{}
        , _pUnit{ nullptr }
        , _pBoundSourceSkeleton{ nullptr }
        , _pBoundTargetSkeleton{ nullptr }
        , _bSourceFromParent{ SW_FALSE }
        , _bProfileReady{ SW_FALSE }
        , _bReferenceOverride{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    PoseRetargetComponent::~PoseRetargetComponent()
    {
        if ( _pUnit != nullptr )
            _bindingRegistration.reset();
    }

    void PoseRetargetComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _bProfileReady == SW_FALSE && _profilePath.empty() == false )
            setProfilePath( _profilePath );
        const bool bBindToParentSource = _bSourceFromParent == SW_TRUE && getOwner() != nullptr && getOwner()->getParent() != nullptr;
        if ( bBindToParentSource )
            setSource( getOwner()->getParent()->getComponent<SkeletalMeshComponent>() );
        bindToUnit();
    }

    void PoseRetargetComponent::onEndPlay()
    {
        setSource( nullptr );
        if ( _pUnit != nullptr )
            _bindingRegistration.reset();
        _pUnit = nullptr;
        Component::onEndPlay();
    }

    void PoseRetargetComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        static const hashed_string s_profileName( "_profilePath" );
        if ( propertyName == s_profileName )
            setProfilePath( _profilePath );
    }

    void PoseRetargetComponent::setProfilePath( string_view path )
    {
        _profilePath   = string{ path };
        _bProfileReady = SW_FALSE;
        if ( _profilePath.empty() == false && _profile.loadFromResource( _profilePath ) )
            _bProfileReady = SW_TRUE;
        _pBoundSourceSkeleton = nullptr; // 다음 평가가 다시 묶는다
    }

    void PoseRetargetComponent::setProfile( const RetargetProfile& profile )
    {
        _profile              = profile;
        _bProfileReady        = SW_TRUE;
        _pBoundSourceSkeleton = nullptr;
    }

    void PoseRetargetComponent::setTargetReferencePose( const Pose& referencePose )
    {
        _targetReference      = referencePose;
        _bReferenceOverride   = SW_TRUE;
        _pBoundSourceSkeleton = nullptr;
    }

    SkeletalMeshComponent* PoseRetargetComponent::findSource() const
    {
        const GameObject* pOwner              = getOwner();
        const bool        bSourceUnresolvable = _source.isValid() == false || pOwner == nullptr || pOwner->getManager() == nullptr;
        if ( bSourceUnresolvable )
            return nullptr;
        return pOwner->getManager()->resolveComponent( _source );
    }

    void PoseRetargetComponent::setSource( SkeletalMeshComponent* pSource )
    {
        bindToUnit();
        SkeletalMeshComponent* pOld = findSource();
        if ( pOld != nullptr && _pUnit != nullptr )
            _pUnit->removeAnimationDependency( pOld );
        _source                     = TComponentHandle<SkeletalMeshComponent>{ pSource };
        _pBoundSourceSkeleton       = nullptr;
        const bool bNeedsDependency = pSource != nullptr && _pUnit != nullptr && pSource != _pUnit;
        if ( bNeedsDependency )
            _pUnit->addAnimationDependency( pSource ); // 원본의 모든 단계(최종 포즈)가 먼저 끝나야 한다
    }

    void PoseRetargetComponent::bindToUnit()
    {
        GameObject*            pOwner = getOwner();
        SkeletalMeshComponent* pUnit  = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pUnit == _pUnit )
            return;
        if ( _pUnit != nullptr )
            _bindingRegistration.reset();
        _pUnit = pUnit;
        if ( _pUnit != nullptr )
            _bindingRegistration = _pUnit->registerAnimationPhaseTask( &_binding );
    }

    bool PoseRetargetComponent::isRetargeting() const
    {
        return _bProfileReady == SW_TRUE && findSource() != nullptr;
    }

    void PoseRetargetComponent::populateBasePose( SkeletalMeshComponent& unit )
    {
        const SkeletalMeshComponent* pSource = isRetargeting() ? findSource() : nullptr;
        if ( pSource == nullptr )
            return;
        const Skeleton& sourceSkeleton = pSource->getSkeleton();
        const Skeleton& targetSkeleton = unit.getSkeleton();
        if ( _pBoundSourceSkeleton != &sourceSkeleton || _pBoundTargetSkeleton != &targetSkeleton )
        {
            // 스켈레톤이 바뀌었을 때만 다시 묶는다(이름 찾기 · 레퍼런스 모델 공간).
            const bool bOverride = _bReferenceOverride == SW_TRUE && _targetReference.getBoneCount() == targetSkeleton.getBoneCount();
            if ( _retargeter.initialize( _profile, sourceSkeleton, targetSkeleton, bOverride ? &_targetReference : nullptr ) == false )
            {
                SW_LOG_ERROR( "Retarget profile '%#' does not fit the source/target skeletons", _profilePath.c_str() );
                _bProfileReady = SW_FALSE;
                return;
            }
            _pBoundSourceSkeleton = &sourceSkeleton;
            _pBoundTargetSkeleton = &targetSkeleton;
        }
        // 원본은 의존이라 이 유닛보다 먼저 모든 단계를 마쳤다(`AnimationDependencyEdge`) — 원본의 후처리(IK · 스프링)와 부착까지 적용된 최종 로컬 포즈를 옮긴다.
        // 이 유닛 자신의 후처리(리그)는 그 위에서 다시 돈다.
        _retargeter.retarget( pSource->getLocalPose(), unit.getLocalPose() );
    }
} // namespace sw
