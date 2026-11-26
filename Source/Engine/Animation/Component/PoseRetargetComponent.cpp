#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Foundation/ComponentHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class RetargetProfile;
    class PoseRetargetComponent;
    class SkeletalMeshComponent;
    class Skeleton;

    /** @class PoseRetargetBinding @brief 컴포넌트가 유닛에 보이는 단계 일입니다(리플렉션 컴포넌트는 기반 하나라 이 객체가 구현). */
    class PoseRetargetBinding final : public IAnimationPhaseTask
    {
    public:
        explicit PoseRetargetBinding( PoseRetargetComponent& owner );

        bool isAnimationActive() const override;
        /** @brief 원본을 옮겨 쓸 때(프로필이 준비되고 원본이 있다)만 포즈를 쓴다. 원본이 없거나 프로필이 없으면 이 유닛은 읽기만 하는 작업만 남는다. */
        AnimationPoseAccess getPoseAccess() const override;
        void                runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& skeletalMesh, const AnimationFrameContext& context ) override;
        void                onAnimationMeshDetached( SkeletalMeshComponent& skeletalMesh ) override;

    private:
        PoseRetargetComponent& _owner;
    };

    /**
     * @class PoseRetargetComponent
     * @brief 원본 유닛을 의존으로 걸고, 기본 포즈 단계에서 그 유닛의 기본 포즈(그 유닛의 후처리 리그 앞)를 이 유닛 스켈레톤으로 옮깁니다.
     * @details 비율이 다른 스켈레톤(다리 길이 · `BoneProportion` 을 건 레퍼런스)도 골반 높이 비와 다리 IK 목표로 보폭을 맞춥니다(`PoseRetargeter`).
     *          이 유닛의 후처리 리그(`PoseModifierComponent` — 발 디딤 등)는 그 뒤에 돕니다. 같은 클립을 여러 캐릭터가 쓸 때 미리 베이크해 두려면
     *          `PoseRetargeter::bakeClip` 입니다.
     */
    REFLECT( Category = "Animation 3D", DisplayName = "Pose Retarget Component", Tooltip = "Copies another skeletalMesh's pose onto this skeleton through a retarget profile" )
    class PoseRetargetComponent : public Component
    {
        friend class PoseRetargetBinding;

    public:
        REFLECT_BODY();

        PoseRetargetComponent();
        virtual ~PoseRetargetComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 프로필 경로(`*.retarget.xml`)를 바꾸고 읽습니다. */
        void setProfilePath( string_view path );
        /** @brief 프로필을 런타임에 정합니다(저장되지 않습니다). nullptr 이면 옮기지 않습니다. */
        void setProfile( shared_ptr<const RetargetProfile> pProfile );
        /** @brief 원본 유닛을 정합니다(의존이 됩니다). nullptr 이면 뗍니다. */
        void setSource( SkeletalMeshComponent* pSource );
        /** @brief 대상 레퍼런스 덮어쓰기(본 비율을 건 포즈)입니다. 본 수가 이 유닛 스켈레톤과 같아야 쓰입니다. */
        void setTargetReferencePose( const Pose& referencePose );
        /** @brief 지금 묶인 리타기터입니다(테스트 · 진단). */
        const PoseRetargeter& getRetargeter() const { return _retargeter; }
        /** @brief 이번 기본 포즈 단계가 원본을 옮겨 쓸지 반환합니다(프로필이 준비되고 원본을 찾을 수 있다). 포즈 접근 선언과 `populateBasePose` 가 같은 판정을 쓴다. 감시 값도 읽는다. */
        bool isRetargeting() const;

    private:
        /** @brief 같은 오브젝트의 유닛에 붙고, 원본 의존을 맞춥니다. */
        void bindToUnit();
        /** @brief 원본 유닛입니다(없으면 nullptr). */
        SkeletalMeshComponent* findSource() const;
        /** @brief 기본 포즈 단계입니다(워커). */
        void populateBasePose( SkeletalMeshComponent& skeletalMesh );

        PROPERTY( Category = "Retarget", DisplayName = "Profile", Tooltip = "Retarget profile (*.retarget.xml)" )
        AssetRef<RetargetProfile> _profilePath;

        SW_TRANSIENT PoseRetargetBinding _binding;
        Subscription                     _bindingRegistration; ///< 유닛 단계 작업 등록 구독(놓으면 유닛에서 빠진다)
        SharedAssetRef<RetargetProfile>  _pProfile;            ///< 쥔 프로필(경로로 받았으면 공유 캐시의 한 벌)
        SW_TRANSIENT PoseRetargeter      _retargeter;
        SW_TRANSIENT Pose                _targetReference;
        SW_TRANSIENT TComponentHandle<SkeletalMeshComponent> _source;
        SkeletalMeshComponent*                               _pSkeletalMesh;
        const Skeleton*                                      _pBoundSourceSkeleton; ///< 리타기터를 묶은 원본 스켈레톤(정체성만 본다)
        const Skeleton*                                      _pBoundTargetSkeleton;
        PROPERTY( Category = "Retarget", DisplayName = "Source From Parent", Tooltip = "Use the parent object's skeletal mesh as the source" )
        uint8                  _bSourceFromParent  : 1;
        SW_TRANSIENT uint8     _bProfileReady      : 1;
        SW_TRANSIENT uint8     _bReferenceOverride : 1;
        [[maybe_unused]] uint8 _reserved           : 5;
    };

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
        _pProfile             = SharedAssetRef<RetargetProfile>{ std::move( pProfile ) };
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
        return pOwner->getMutableManager()->resolveComponent( _source );
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

#include "PoseRetargetComponent.cpp.gen.inl"
