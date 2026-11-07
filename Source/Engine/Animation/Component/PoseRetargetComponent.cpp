#include "pch.h"

#include "Engine/Animation/Component/PoseRetargetComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
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

    void PoseRetargetBinding::runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context )
    {
        (void)context;
        if ( phase == AnimationPhase::BasePose )
            _owner.populateBasePose( skeletalMesh );
    }

    void PoseRetargetBinding::onAnimationMeshDetached( SkeletalMeshComponent& skeletalMesh )
    {
        if ( _owner._pSkeletalMesh == &skeletalMesh )
            _owner._pSkeletalMesh = nullptr;
    }

    PoseRetargetComponent::PoseRetargetComponent()
        : _profilePath{}
        , _binding{ *this }
        , _pProfile{}
        , _retargeter{}
        , _targetReference{}
        , _source{}
        , _pSkeletalMesh{ nullptr }
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
        if ( _pSkeletalMesh != nullptr )
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
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
        _pSkeletalMesh = nullptr;
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
        _profilePath = string{ path };
        // 프로필은 공유 캐시의 한 벌이다 — 제자리 다시 읽기 뒤 핫 리로드가 이 칸을 알리면(`onPropertyChanged`) 같은 객체를 다시 받고 리타기터를 다시 묶는다.
        _pProfile             = _profilePath.acquire();
        _bProfileReady        = _pProfile != nullptr ? SW_TRUE : SW_FALSE;
        _pBoundSourceSkeleton = nullptr; // 다음 평가가 다시 묶는다
    }

    void PoseRetargetComponent::setProfile( shared_ptr<const RetargetProfile> pProfile )
    {
        _pProfile             = std::move( pProfile );
        _bProfileReady        = _pProfile != nullptr ? SW_TRUE : SW_FALSE;
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
        if ( pOld != nullptr && _pSkeletalMesh != nullptr )
            _pSkeletalMesh->removeAnimationDependency( pOld );
        _source                     = TComponentHandle<SkeletalMeshComponent>{ pSource };
        _pBoundSourceSkeleton       = nullptr;
        const bool bNeedsDependency = pSource != nullptr && _pSkeletalMesh != nullptr && pSource != _pSkeletalMesh;
        if ( bNeedsDependency )
            _pSkeletalMesh->addAnimationDependency( pSource ); // 원본의 모든 단계(최종 포즈)가 먼저 끝나야 한다
    }

    void PoseRetargetComponent::bindToUnit()
    {
        GameObject*            pOwner        = getOwner();
        SkeletalMeshComponent* pSkeletalMesh = ( pOwner != nullptr ) ? pOwner->getComponent<SkeletalMeshComponent>() : nullptr;
        if ( pSkeletalMesh == _pSkeletalMesh )
            return;
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration.reset();
        _pSkeletalMesh = pSkeletalMesh;
        if ( _pSkeletalMesh != nullptr )
            _bindingRegistration = _pSkeletalMesh->registerAnimationPhaseTask( &_binding );
    }

    bool PoseRetargetComponent::isRetargeting() const
    {
        return _bProfileReady == SW_TRUE && findSource() != nullptr;
    }

    void PoseRetargetComponent::populateBasePose( SkeletalMeshComponent& skeletalMesh )
    {
        const SkeletalMeshComponent* pSource = isRetargeting() ? findSource() : nullptr;
        if ( pSource == nullptr )
            return;
        const Skeleton& sourceSkeleton = pSource->getSkeleton();
        const Skeleton& targetSkeleton = skeletalMesh.getSkeleton();
        if ( _pBoundSourceSkeleton != &sourceSkeleton || _pBoundTargetSkeleton != &targetSkeleton )
        {
            // 스켈레톤이 바뀌었을 때만 다시 묶는다(이름 찾기 · 레퍼런스 모델 공간).
            const bool bOverride = _bReferenceOverride == SW_TRUE && _targetReference.getBoneCount() == targetSkeleton.getBoneCount();
            if ( _retargeter.initialize( *_pProfile, sourceSkeleton, targetSkeleton, bOverride ? &_targetReference : nullptr ) == false )
            {
                SW_LOG_ERROR_ID( "PoseRetarget.RetargetProfileDoesNot", "Retarget profile '%#' does not fit the source/target skeletons", _profilePath.c_str() );
                _bProfileReady = SW_FALSE;
                return;
            }
            _pBoundSourceSkeleton = &sourceSkeleton;
            _pBoundTargetSkeleton = &targetSkeleton;
        }
        // 원본은 의존이라 이 유닛보다 먼저 모든 단계를 마쳤다(`AnimationDependencyEdge`) — 원본의 후처리(IK · 스프링)와 부착까지 적용된 최종 로컬 포즈를 옮긴다.
        // 이 유닛 자신의 후처리(리그)는 그 위에서 다시 돈다.
        _retargeter.retarget( pSource->getLocalPose(), skeletalMesh.getLocalPose() );
    }
} // namespace sw

namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( poseRetargetActive, "PoseRetarget.Retargeting", "1 while the first pose retarget component copies its source's pose through its profile", PoseRetargetComponent,
                                &PoseRetargetComponent::isRetargeting );
} // namespace sw
