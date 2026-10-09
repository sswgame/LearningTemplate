/**
 * @file PoseRetargetComponent.h
 * @brief 런타임 리타깃 — 다른 유닛(원본 스켈레톤)의 기본 포즈를 프로필로 이 유닛의 스켈레톤에 옮깁니다(기본 포즈 단계).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class PoseRetargetComponent;
    class SkeletalMeshComponent;
    class Skeleton;

    /** @class PoseRetargetBinding @brief 컴포넌트가 유닛에 보이는 단계 일입니다(리플렉션 컴포넌트는 기반 하나라 이 객체가 구현). */
    class SW_API PoseRetargetBinding final : public IAnimationPhaseTask
    {
    public:
        explicit PoseRetargetBinding( PoseRetargetComponent& owner );

        bool isAnimationActive() const override;
        void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        PoseRetargetComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PoseRetargetComponent
     * @brief 원본 유닛을 의존으로 걸고, 기본 포즈 단계에서 그 유닛의 **기본 포즈**(그 유닛의 후처리 리그 앞)를 이 유닛 스켈레톤으로 옮깁니다.
     * @details 비율이 다른 스켈레톤(다리 길이 · `BoneProportion` 을 건 레퍼런스)도 골반 높이 비와 다리 IK 목표로 보폭을 맞춥니다(`PoseRetargeter`).
     *          이 유닛의 후처리 리그(`PoseModifierComponent` — 발 디딤 등)는 그 뒤에 돕니다. 같은 클립을 여러 캐릭터가 쓸 때 미리 구워 두려면
     *          `RetargetBakeUtil::bakeClip` 입니다.
     */
    REFLECT( Category = "Animation 3D", DisplayName = "Pose Retarget Component", Tooltip = "Copies another unit's pose onto this skeleton through a retarget profile" )
    class SW_API PoseRetargetComponent : public Component
    {
        friend class PoseRetargetBinding;

    public:
        REFLECT_BODY();

        PoseRetargetComponent();
        virtual ~PoseRetargetComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 프로필 경로(`*.retarget.json`)를 바꾸고 읽습니다. */
        void setProfilePath( string_view path );
        /** @brief 프로필을 런타임에 정합니다(저장되지 않습니다). */
        void setProfile( const RetargetProfile& profile );
        /** @brief 원본 유닛을 정합니다(의존이 됩니다). nullptr 이면 뗍니다. */
        void setSource( SkeletalMeshComponent* pSource );
        /** @brief 대상 레퍼런스 덮어쓰기(본 비율을 건 포즈)입니다. 본 수가 이 유닛 스켈레톤과 같아야 쓰입니다. */
        void setTargetReferencePose( const Pose& referencePose );
        /** @brief 지금 묶인 리타기터입니다(시험 · 진단). */
        const PoseRetargeter& getRetargeter() const { return _retargeter; }

    private:
        /** @brief 같은 오브젝트의 유닛에 붙고, 원본 의존을 맞춥니다. */
        void bindToUnit();
        /** @brief 원본 유닛입니다(없으면 nullptr). */
        SkeletalMeshComponent* findSource() const;
        /** @brief 기본 포즈 단계입니다(워커). */
        void buildBasePose( SkeletalMeshComponent& unit );

        PROPERTY( Category = "Retarget", DisplayName = "Profile", AssetPath, AssetType = "RetargetProfile", Tooltip = "Retarget profile (*.retarget.json)" )
        string _profilePath;

        PoseRetargetBinding    _binding;
        RetargetProfile        _profile;
        PoseRetargeter         _retargeter;
        Pose                   _targetReference;
        ComponentHandle        _source;
        SkeletalMeshComponent* _pUnit;
        const Skeleton*        _pBoundSourceSkeleton; ///< 리타기터를 묶은 원본 스켈레톤(정체성만 본다)
        const Skeleton*        _pBoundTargetSkeleton;
        PROPERTY( Category = "Retarget", DisplayName = "Source From Parent", Tooltip = "Use the parent object's skeletal mesh as the source" )
        uint8                  _bSourceFromParent  : 1;
        uint8                  _bProfileReady      : 1;
        uint8                  _bReferenceOverride : 1;
        [[maybe_unused]] uint8 _reserved           : 5;
    };
} // namespace sw
