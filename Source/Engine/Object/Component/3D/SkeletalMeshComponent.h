/**
 * @file SkeletalMeshComponent.h
 * @brief 애니메이션 유닛 — 스켈레톤 · 포즈 버퍼 · 스킨 팔레트 · 스킨드 메시입니다. 평가는 `AnimationSystem` 이 합니다(자기 onTick 이 아님).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Pose.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Mesh;
    class Skeleton;

    /**
     * @class SkeletalMeshComponent
     * @brief 장비 부품 하나 = 유닛 하나입니다(캐릭터 몸 · 투구 · 무기). 스켈레톤이 없는 부품은 본 하나짜리 암묵 스켈레톤을 받습니다.
     * @details - **포즈**: 로컬 포즈(SoA) → 모델 공간 → 스킨 팔레트. 기본 포즈 단계에서 레퍼런스 포즈로 시작하고, 등록된 일(애니메이터)이 덮어씁니다.
     *          - **리더 포즈(Leader Pose)**: 리더를 정하면 기본 포즈 단계에서 리더의 로컬 포즈를 본 이름으로 옮깁니다(모듈식 파츠가 몸을 따른다).
     *            리더는 의존이 되어 먼저 평가됩니다. `_bFollowParentPose` 면 부모 오브젝트의 유닛이 리더입니다.
     *          - **메시**: `_meshId` 의 `.mesh` 가 스킨을 가지면 컴포넌트마다 메시 객체를 따로 둡니다 — GPU 스키닝 결과(모프 풀 구간)가 메시마다
     *            하나라서, 포즈가 다른 캐릭터가 같은 메시 객체를 나누면 한 포즈로 그려집니다. 정점 데이터는 그 복사본입니다(군중 공유는 다음 일).
     *          - **LOD 훅**: 갱신 주기(`_updateRateDivisor` 프레임마다 한 번 포즈를 만든다 — 시간 · 알림은 매 프레임), 화면 밖이면(`setVisibleHint`)
     *            포즈를 건너뜀(`_bAnimateWhenOffscreen` 이 아니면). 쉬는 유닛(재생 없음 · 포즈 깨끗함)은 비용이 없습니다.
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Skeletal Mesh Component", Tooltip = "Skinned mesh driven by a skeleton pose (animation unit)" )
    class SW_API SkeletalMeshComponent : public MeshComponent
    {
    public:
        REFLECT_BODY();

        SkeletalMeshComponent();
        virtual ~SkeletalMeshComponent() override;

        void onBeginPlay() override;
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 메시 · 머티리얼을 풀고, 스킨드 메시면 컴포넌트 몫 메시를 만들고, 스켈레톤을 읽습니다. */
        void resolveRenderAssets() override;

        /** @brief 스켈레톤 에셋 경로를 바꾸고 읽습니다. 빈 경로면 본 하나짜리 암묵 스켈레톤입니다. */
        void          setSkeletonPath( string_view path );
        const string& getSkeletonPath() const { return _skeletonPath; }
        /** @brief 스켈레톤을 런타임에 정합니다(저장되지 않습니다 — 시험 · 절차 생성 · 파괴 조각). 경로가 빈 동안 렌더 에셋을 다시 풀어도 유지됩니다. */
        void setSkeleton( shared_ptr<const Skeleton> skeleton );
        /** @brief 스켈레톤입니다. 늘 있습니다(없으면 암묵 스켈레톤). */
        const Skeleton& getSkeleton() const;

        /** @brief 로컬 포즈입니다. 기본 포즈 단계의 일이 여기에 씁니다. */
        Pose&       getLocalPose() { return _localPose; }
        const Pose& getLocalPose() const { return _localPose; }
        /** @brief 모델 공간 본 행렬입니다(단계가 끝날 때마다 다시 구합니다). */
        const vector<float4x4>& getModelSpaceTransforms() const { return _listModelSpace; }
        /** @brief 스킨 팔레트(역 바인드 × 모델 공간)입니다. 렌더러가 스냅샷으로 옮깁니다. */
        const vector<float4x4>& getSkinPalette() const { return _listSkinPalette; }
        /** @brief 이름의 본의 모델 공간 행렬입니다. 없으면 false 입니다. */
        bool findBoneModelTransform( const hashed_string& boneName, float4x4& outTransform ) const;
        /** @brief 포즈를 다시 만들게 합니다(쉬던 유닛도 다음 프레임 한 번 돕니다). */
        void markPoseDirty() { _bPoseDirty = SW_TRUE; }
        /**
         * @brief 애니메이션 평가 밖에서 로컬 포즈를 고친 쪽이 모델 공간 · 스킨 팔레트를 바로 다시 구합니다(게임 스레드, 그 프레임의 평가 뒤).
         * @details 물리가 모는 유닛(파괴 조각 — 물리 단계가 평가보다 늦다)이 씁니다. 포즈를 깨끗하게 두므로 할 일이 없는 유닛은 다음 평가에서 쉬고
         *          레퍼런스 포즈로 덮이지 않습니다. 렌더 스냅샷은 틱 뒤에 팔레트를 옮기므로 같은 프레임에 그려집니다.
         */
        void applyExternalPose();

        /** @brief 리더를 정합니다(nullptr 이면 뗍니다). 리더는 의존이 됩니다. */
        void setLeaderPose( SkeletalMeshComponent* pLeader );
        /** @brief 리더입니다. 없거나 사라졌으면 nullptr 입니다. */
        SkeletalMeshComponent* findLeaderPose() const;

        /** @brief 이 유닛이 @p pUpstream 다음에 평가되게 합니다(교차 유닛 참조 · 부착). 고리가 생기면 평가가 오류를 알립니다. */
        void addAnimationDependency( SkeletalMeshComponent* pUpstream );
        /** @brief 의존을 뗍니다. */
        void removeAnimationDependency( SkeletalMeshComponent* pUpstream );
        /** @brief 의존 목록(핸들)입니다. 리더 포함입니다. */
        const vector<ComponentHandle>& getAnimationDependencies() const { return _listDependency; }

        /** @brief 단계에 끼어드는 일을 겁니다(빌립니다). 같은 것은 한 번만 들어갑니다. */
        void addAnimationPhaseTask( IAnimationPhaseTask* pTask );
        /** @brief 일을 뗍니다. 그 일에 `onAnimationUnitDetached` 를 알립니다. */
        void removeAnimationPhaseTask( IAnimationPhaseTask* pTask );

        /** @brief 포즈를 만드는 주기(프레임)입니다. 1 이면 매 프레임입니다. */
        void   setUpdateRateDivisor( uint32 divisor );
        uint32 getUpdateRateDivisor() const { return _updateRateDivisor; }
        /** @brief 화면에 보이는지 알려 주는 훅입니다(가시성 판정 쪽이 부릅니다, 기본 보임). */
        void setVisibleHint( bool bVisible ) { _bVisibleHint = bVisible ? SW_TRUE : SW_FALSE; }
        bool isVisibleHint() const { return _bVisibleHint == SW_TRUE; }
        /** @brief 화면 밖에서도 포즈를 만들지입니다. */
        void setAnimateWhenOffscreen( bool bAnimate ) { _bAnimateWhenOffscreen = bAnimate ? SW_TRUE : SW_FALSE; }
        /** @brief 포즈를 실제로 만든 횟수입니다(진단 · 시험). */
        uint32 getPoseEvaluationCount() const { return _poseEvaluationCount; }

        // --- AnimationSystem 이 부르는 것 ---
        /** @brief 이번 프레임 할 일과 LOD(포즈를 만드는지)를 정하고, 일하면 일들의 `prepareAnimationFrame` 을 부릅니다(게임 스레드). 쉬면 false 입니다. */
        [[nodiscard]] bool beginAnimationFrame( float32 deltaSeconds, uint64 frameIndex );
        /** @brief 단계 하나를 합니다(워커). */
        void runAnimationPhase( AnimationPhase phase );
        /** @brief 모든 단계 뒤 게임 스레드 일입니다. */
        void finishAnimationFrame();
        /** @brief 동기 그룹 후보를 모읍니다(게임 스레드). */
        void collectSyncPlayers( vector<AnimPlayer*>& inoutListPlayer, vector<float32>& inoutListWeight, vector<hashed_string>& inoutListGroup );

    private:
        /** @brief 스켈레톤을 경로에서 다시 읽습니다. 경로가 비고 코드가 정한 스켈레톤이 있으면 그대로 둡니다. */
        void resolveSkeleton();
        /** @brief 스켈레톤을 바꾸고 포즈 버퍼를 맞춥니다(nullptr 이면 암묵 스켈레톤). */
        void assignSkeleton( shared_ptr<const Skeleton> skeleton );
        /** @brief 공유 메시가 스킨을 가지면 컴포넌트 몫 복사본을 만듭니다. */
        void resolveSkinInstanceMesh();
        /** @brief 스켈레톤이 바뀌었으면 포즈 · 행렬 · 리더 표를 맞춥니다. */
        void resetPoseBuffers();
        /** @brief 리더의 로컬 포즈를 본 이름으로 옮깁니다. */
        void copyLeaderPose( const SkeletalMeshComponent& leader );
        /** @brief 단계 하나에 걸린 일을 모두 부릅니다. */
        void runTasks( AnimationPhase phase );
        /** @brief 매니저의 시스템에 순서를 다시 짓게 합니다. */
        void notifyOrderChanged();

        PROPERTY( Category = "Animation", DisplayName = "Skeleton", AssetPath, AssetType = "Skeleton", Tooltip = "Skeleton asset; empty uses a one-bone implicit skeleton" )
        string _skeletonPath;
        PROPERTY( Category = "Animation", DisplayName = "Update Rate Divisor", Tooltip = "Build the pose every N frames (animation LOD)", Min = 1.0, Max = 16.0 )
        uint32                     _updateRateDivisor;
        shared_ptr<const Skeleton> _skeleton;
        /// @brief 스킨드 메시의 공유 원본입니다(정체성 · 핫 리로드 대조용). 이 컴포넌트의 메시는 그 복사본입니다.
        shared_ptr<Mesh>             _skinSourceMesh;
        uint64                       _skinSourceContentId;
        Pose                         _localPose;
        vector<float4x4>             _listModelSpace;
        vector<float4x4>             _listSkinPalette;
        vector<ComponentHandle>      _listDependency;
        vector<IAnimationPhaseTask*> _listTask;
        vector<int32>                _listLeaderBone; ///< 내 본 → 리더 본 인덱스(-1 = 없음)
        ComponentHandle              _leader;
        const Skeleton*              _pLeaderSkeletonForMap; ///< `_listLeaderBone` 을 지은 리더 스켈레톤(정체성만 봅니다)
        AnimationSystem*             _pAnimationSystem;
        AnimationFrameContext        _frameContext;
        uint32                       _poseEvaluationCount;
        PROPERTY( Category = "Animation", DisplayName = "Follow Parent Pose", Tooltip = "Use the parent object's skeletal mesh as the leader pose" )
        uint8 _bFollowParentPose : 1;
        PROPERTY( Category = "Animation", DisplayName = "Animate When Offscreen", Tooltip = "Keep building the pose while not visible" )
        uint8                  _bAnimateWhenOffscreen : 1;
        uint8                  _bVisibleHint          : 1;
        uint8                  _bPoseDirty            : 1;
        uint8                  _bRuntimeSkeleton      : 1; ///< 스켈레톤을 코드가 정했다(`setSkeleton`) — 빈 경로로 다시 풀 때 지키지 않는다
        [[maybe_unused]] uint8 _reserved              : 3;
    };
} // namespace sw
