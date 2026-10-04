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
    class SkeletalMeshComponent;
    class Skeleton;
    class SkeletonBoneLod;

    /**
     * @class SkeletalMeshLodClient
     * @brief 유닛이 LOD 판정(`AnimationSystem::updateLod`)에 보이는 얼굴입니다. 컴포넌트가 하나 들고 시스템에 올립니다.
     */
    class SW_API SkeletalMeshLodClient final : public IAnimationLodClient
    {
    public:
        explicit SkeletalMeshLodClient( SkeletalMeshComponent& owner );

        bool                   findAnimationLodBounds( float3& outCenter, float32& outRadius ) const override;
        const SkeletonBoneLod* findBoneLod() const override;
        void                   applyAnimationLod( const AnimationLodState& state ) override;

    private:
        SkeletalMeshComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkeletalMeshComponent
     * @brief 장비 부품 하나 = 유닛 하나입니다(캐릭터 몸 · 투구 · 무기). 스켈레톤이 없는 부품은 본 하나짜리 암묵 스켈레톤을 받습니다.
     * @details - **포즈**: 로컬 포즈(SoA) → 모델 공간 → 스킨 팔레트. 기본 포즈 단계에서 레퍼런스 포즈로 시작하고, 등록된 일(애니메이터)이 덮어씁니다.
     *          - **리더 포즈(Leader Pose)**: 리더를 정하면 기본 포즈 단계에서 리더의 로컬 포즈를 본 이름으로 옮깁니다(모듈식 파츠가 몸을 따른다).
     *            리더는 의존이 되어 먼저 평가됩니다. `_bFollowParentPose` 면 부모 오브젝트의 유닛이 리더입니다.
     *          - **메시**: `_meshId` 의 `.mesh` 가 스킨을 가지면 컴포넌트마다 메시 객체를 따로 둡니다 — GPU 스키닝 결과(모프 풀 구간)가 메시마다
     *            하나라서, 포즈가 다른 캐릭터가 같은 메시 객체를 나누면 한 포즈로 그려집니다. 정점 데이터는 그 복사본입니다(군중 공유는 다음 일).
     *          - **LOD**: `AnimationSystem` 이 프레임마다 뷰(카메라 절두체들)로 판정해 넣습니다(`applyAnimationLod`) — 화면 밖이면 포즈를 건너뛰고
     *            (`_bAnimateWhenOffscreen` 이 아니면), 화면 크기 단계 · 예산이 정한 주기마다 포즈를 만들며(시간 · 알림은 매 프레임), 건너뛴 프레임은
     *            직전 두 포즈 사이를 보간합니다(한 주기만큼 늦다 — 언리얼 URO 보간과 같다). 본 LOD(`<스켈레톤>.bonelod.json`)는 작은 화면에서 끝 본을
     *            풀지 않습니다. `_updateRateDivisor`(PROPERTY)는 하한이고 LOD 가 더 늘릴 수 있습니다. 같은 주기의 유닛은 위상(핸들 해시)이 달라 같은
     *            프레임에 몰리지 않습니다. 쉬는 유닛(재생 없음 · 포즈 깨끗함)은 비용이 없습니다.
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
        /** @brief 스켈레톤을 런타임에 정합니다(저장되지 않습니다 — 시험 · 절차 생성). */
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
        /** @brief 포즈를 실제로 만든 횟수입니다(진단 · 시험). 보간만 한 프레임은 세지 않습니다. */
        uint32 getPoseEvaluationCount() const { return _poseEvaluationCount; }
        /** @brief 이번 프레임에 포즈를 만드는지입니다(`beginAnimationFrame` 뒤에 뜻이 있습니다). */
        bool isPoseNeededThisFrame() const { return _frameContext._bPoseNeeded == SW_TRUE; }

        // --- LOD ---
        /** @brief LOD 판정을 받습니다(게임 스레드, `AnimationSystem` 이 평가 앞에 부릅니다). 가시성 훅 · 주기 · 보간 · 본 LOD 단계를 정합니다. */
        void applyAnimationLod( const AnimationLodState& state );
        /** @brief 마지막 LOD 판정입니다. */
        const AnimationLodState& getAnimationLodState() const { return _lodState; }
        /** @brief 주기 위상입니다(핸들에서 나온다 — 같은 주기의 유닛이 같은 프레임에 몰리지 않게). */
        uint32 getUpdatePhase() const { return _updatePhase; }
        /** @brief 본 LOD 표를 런타임에 정합니다(시험 — 보통은 스켈레톤 곁 파일에서 읽습니다). */
        void setBoneLod( shared_ptr<const SkeletonBoneLod> boneLod );
        /** @brief 본 LOD 표입니다(없으면 nullptr). */
        const SkeletonBoneLod* findBoneLod() const { return _boneLod.get(); }
        /** @brief 지금 본 LOD 단계의 본 마스크(본 수, 1 = 푼다)입니다. 모든 본을 풀면 nullptr 입니다. 애니메이터가 샘플할 때 읽습니다(워커). */
        const uint8* findBoneLodMask() const;
        /** @brief LOD 판정을 받는 얼굴입니다. */
        SkeletalMeshLodClient& getLodClient() { return _lodClient; }

        // --- AnimationSystem 이 부르는 것 ---
        /** @brief 이번 프레임 할 일과 LOD(포즈를 만드는지)를 정합니다(게임 스레드). 쉬면 false 입니다. */
        [[nodiscard]] bool beginAnimationFrame( float32 deltaSeconds, uint64 frameIndex );
        /** @brief 단계 하나를 합니다(워커). */
        void runAnimationPhase( AnimationPhase phase );
        /** @brief 모든 단계 뒤 게임 스레드 일입니다. */
        void finishAnimationFrame();
        /** @brief 동기 그룹 후보를 모읍니다(게임 스레드). */
        void collectSyncPlayers( vector<AnimPlayer*>& inoutListPlayer, vector<float32>& inoutListWeight, vector<hashed_string>& inoutListGroup );

    private:
        /** @brief 스켈레톤을 경로에서 다시 읽습니다. */
        void resolveSkeleton();
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
        /** @brief 스켈레톤 곁 본 LOD 파일을 읽습니다(없으면 본 LOD 없음). */
        void resolveBoneLod();
        /** @brief 본 LOD 표 · 스켈레톤이 바뀌었으면 단계별 마스크를 다시 짓습니다. */
        void refreshBoneLodMasks();
        /** @brief 보간 프레임의 포즈를 만듭니다(직전 표시 포즈 → 마지막 평가 포즈). 스킨 팔레트 단계에서 부릅니다. */
        void applySkippedFrameInterpolation( bool bEvaluatedThisFrame );

        PROPERTY( Category = "Animation", DisplayName = "Skeleton", AssetPath, AssetType = "Skeleton", Tooltip = "Skeleton asset; empty uses a one-bone implicit skeleton" )
        string _skeletonPath;
        PROPERTY( Category = "Animation", DisplayName = "Update Rate Divisor", Tooltip = "Build the pose every N frames (animation LOD)", Min = 1.0, Max = 16.0 )
        uint32                     _updateRateDivisor;
        shared_ptr<const Skeleton> _skeleton;
        /// @brief 스킨드 메시의 공유 원본입니다(정체성 · 핫 리로드 대조용). 이 컴포넌트의 메시는 그 복사본입니다.
        shared_ptr<Mesh>                  _skinSourceMesh;
        uint64                            _skinSourceContentId;
        Pose                              _localPose;
        vector<float4x4>                  _listModelSpace;
        vector<float4x4>                  _listSkinPalette;
        vector<ComponentHandle>           _listDependency;
        vector<IAnimationPhaseTask*>      _listTask;
        vector<int32>                     _listLeaderBone; ///< 내 본 → 리더 본 인덱스(-1 = 없음)
        ComponentHandle                   _leader;
        const Skeleton*                   _pLeaderSkeletonForMap; ///< `_listLeaderBone` 을 지은 리더 스켈레톤(정체성만 봅니다)
        AnimationSystem*                  _pAnimationSystem;
        AnimationFrameContext             _frameContext;
        uint32                            _poseEvaluationCount;
        SkeletalMeshLodClient             _lodClient;
        AnimationLodState                 _lodState;
        shared_ptr<const SkeletonBoneLod> _boneLod;
        vector<vector<uint8>>             _listBoneLodMask;      ///< 본 LOD 단계 n + 1 의 본 마스크
        uint64                            _boneLodRevision;      ///< 마스크를 지은 본 LOD 내용 번호(0 = 다시 지을 것)
        const Skeleton*                   _pBoneLodMaskSkeleton; ///< 마스크를 지은 스켈레톤(정체성만 봅니다)
        Pose                              _interpolationFrom;    ///< 마지막 평가 때 표시하던 포즈
        Pose                              _interpolationTarget;  ///< 마지막으로 평가한 포즈
        uint32                            _updatePhase;
        uint32                            _framesSinceEvaluation; ///< 마지막 평가 뒤 지난 프레임
        uint32                            _effectiveDivisor;      ///< 이번 프레임의 주기(하한 PROPERTY 와 LOD 중 큰 쪽)
        PROPERTY( Category = "Animation", DisplayName = "Follow Parent Pose", Tooltip = "Use the parent object's skeletal mesh as the leader pose" )
        uint8 _bFollowParentPose : 1;
        PROPERTY( Category = "Animation", DisplayName = "Animate When Offscreen", Tooltip = "Keep building the pose while not visible" )
        uint8                  _bAnimateWhenOffscreen : 1;
        uint8                  _bVisibleHint          : 1;
        uint8                  _bPoseDirty            : 1;
        uint8                  _bInterpolateFrame     : 1; ///< 이번 프레임은 평가 없이 보간만 한다
        uint8                  _bInterpolationReady   : 1; ///< 보간할 두 포즈가 있다
        [[maybe_unused]] uint8 _reserved              : 2;
    };
} // namespace sw
