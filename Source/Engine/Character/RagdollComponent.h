/**
 * @file RagdollComponent.h
 * @brief 래그돌 · 히트박스 — 캐릭터의 물리 에셋(`*.physics.xml`)으로 뼈마다 바디를 세워, 평소엔 애니메이션 포즈를 따르는 키네마틱 히트박스, 죽으면 동적 래그돌,
 *        맞으면 그 뼈 아래만 잠깐 물리(맞음 반응) · 가산 움찔, 상체만 래그돌(부분), 가라앉으면 기상 클립으로 섞어 돌아오기를 합니다.
 * @details 언리얼 Physics Asset + Physical Animation(바디마다 물리 섞임 가중치) + 히트 존, 유니티 래그돌 + 애니메이터 블렌드와 같은 자리입니다.
 *
 *          **바디마다 물리 가중치 하나**(0 = 애니메이션, 1 = 물리)가 모든 경우를 한 길로 합니다: 가중치가 0 이면 키네마틱(포즈로 끌려감), 0 보다 크면
 *          동적입니다. 포즈는 후처리 단계(PostProcess)에서 애니메이션 포즈와 지난 물리 프레임의 바디 자세를 뼈 가중치(바디 없는 뼈는 가장 가까운 조상
 *          바디의 것)로 섞습니다 — 물리 자세는 한 프레임 늦습니다.
 *
 *          프레임 하나: 틱(게임이 맞힘 · 죽음을 부른다) → 애니메이션 평가(후처리에서 섞음, 마무리에서 가중치 · 타이머) → 물리 프레임(시작: 바디 종류 ·
 *          충격량, 스텝마다: 키네마틱 바디를 이번 포즈까지 나눠 움직임, 끝: 바디 자세를 모델 공간으로 읽어 둠 · 가라앉음 판정).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Pose.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/PhysicsRagdoll.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct PhysicsAsset;
    struct PhysicsHitZoneDef;

    class RagdollComponent;
    class Skeleton;

    /** @brief 래그돌의 상태입니다. */
    ENUM()
    enum class RagdollState : uint8
    {
        Animated = 0, ///< 키네마틱 히트박스(맞음 반응만 잠깐 동적)
        Ragdoll,      ///< 모든 바디가 동적(죽음)
        Partial,      ///< 부분 래그돌 — `_partialRootBone` 아래만 동적
        BlendingBack, ///< 래그돌 자세에서 애니메이션(기상 클립)으로 섞으며 돌아가는 중 — 바디는 키네마틱
    };
} // namespace sw

namespace sw
{
    /** @class RagdollAnimationBinding @brief 유닛의 단계에 끼어드는 얼굴입니다(리플렉션 컴포넌트는 기반 하나). */
    class SW_API RagdollAnimationBinding final : public IAnimationPhaseTask
    {
    public:
        explicit RagdollAnimationBinding( RagdollComponent& owner );

        bool isAnimationActive() const override;
        void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        RagdollComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /** @class RagdollComponent @brief 파일 머리말 참고. 같은 오브젝트의 `SkeletalMeshComponent`(유닛)와 그 스켈레톤을 씁니다. */
    REFLECT( Category = "Physics", DisplayName = "Ragdoll", Tooltip = "Physics asset hitboxes driven by animation; ragdoll on death, hit reactions, partial ragdoll and get-up blend" )
    class SW_API RagdollComponent : public PhysicsComponent
    {
        friend class RagdollAnimationBinding;

    public:
        REFLECT_BODY();

        RagdollComponent();
        ~RagdollComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 맞았습니다 — 치명이면(`HitInfo::_bFatal`) 래그돌로, 아니면 맞은 바디 아래 반응 + 가산 움찔입니다. */
        void onHitReceived( const HitInfo& hit ) override;

        /** @brief 물리 에셋 경로를 바꿉니다(바디를 다시 세운다). */
        void          setPhysicsAssetPath( string_view path );
        const string& getPhysicsAssetPath() const { return _physicsAssetPath; }
        /** @brief 물리 에셋입니다(없으면 nullptr). */
        const PhysicsAsset* getPhysicsAsset() const { return _asset.get(); }
        /** @brief 세운 바디들입니다(시작 전 · 에셋 없음이면 비었다). */
        const PhysicsRagdoll& getRagdoll() const { return _ragdoll; }
        /** @brief 지금 상태입니다. */
        RagdollState getState() const { return _state; }
        /** @brief 기상 클립(누움 · 엎드림)과 끝난 뒤 상태를 정합니다(코드 · 시험 — 보통은 PROPERTY 데이터). */
        void setGetUpClips( const hashed_string& faceUpClip, const hashed_string& faceDownClip, const hashed_string& exitState );
        /** @brief 가산 움찔 클립을 정합니다. */
        void setFlinchClip( const hashed_string& clip ) { _flinchClip = clip; }
        /** @brief 바디 하나의 물리 섞임 가중치(0 = 애니메이션, 1 = 물리)입니다. */
        float32 getBodyWeight( uint32 bodyIndex ) const;
        /** @brief 바디가 지금 동적인지입니다. */
        bool isBodyDynamic( uint32 bodyIndex ) const;

        /** @brief 죽음 — 모든 바디를 동적으로 넘깁니다(속도는 애니메이션이 끌던 것을 잇는다). @p bodyIndex 의 바디에 @p impulse 를 줍니다(-1 이면 없음). */
        void startRagdoll( const float3& impulse, int32 bodyIndex );
        /** @brief 부분 래그돌 — `_partialRootBone` 아래 바디를 @p weight 로 섞는 동적으로(상체가 축 늘어진다). */
        void startPartialRagdoll( float32 weight );
        /** @brief 부분 래그돌을 끝내고 `_blendBackSeconds` 동안 애니메이션으로 섞어 돌아갑니다. */
        void stopPartialRagdoll();
        /** @brief 바디가 가라앉았는지입니다(래그돌 상태에서 모든 바디가 `_settleSpeed` 보다 느리게 `_settleSeconds` 동안). */
        bool isSettled() const { return _bSettled; }
        /**
         * @brief 기상 — 오브젝트를 골반 아래로 옮겨 기상 클립(엎드림 · 누움에 따라)의 첫 자세에 맞추고, 래그돌 자세에서 그 클립으로 섞어 돌아갑니다.
         * @return 래그돌이 아니거나 기상 클립이 없으면 false 입니다.
         */
        bool getUp();
        /** @brief 맞음 반응 — @p bodyIndex 바디 아래를 `_hitReactionSeconds` 동안 동적으로(가중치는 줄어든다), 충격량은 그 바디의 @p point 에 줍니다. */
        void applyHitReaction( int32 bodyIndex, const float3& impulse, const float3& point );
        /** @brief 바디의 히트 존입니다. 이 래그돌의 바디가 아니거나 히트 존 이름이 비면 nullptr 입니다. @p outBodyIndex 는 바디 번호(아니면 -1). */
        const PhysicsHitZoneDef* findHitZone( PhysicsBodyHandle body, int32& outBodyIndex ) const;

    protected:
        void beginPhysicsFrame( ScenePhysics& physics ) override;
        void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount ) override;
        void endPhysicsFrame( ScenePhysics& physics, float32 alpha ) override;
        void releasePhysics( ScenePhysics& physics ) override;

    private:
        /** @brief 충격 하나(물리 프레임 시작에 준다)입니다. */
        struct PendingImpulse
        {
            float3 _impulse{};
            float3 _point{};
            int32  _bodyIndex{ -1 };
        };

        void bindToUnit();
        void unbindFromUnit();
        /** @brief 에셋을 (다시) 받습니다. 바뀌면 바디를 다시 세웁니다. */
        void loadAsset();
        /** @brief 유닛 스켈레톤의 뼈 이름 배열을 맞춥니다. */
        void refreshBoneNames( const Skeleton& skeleton );
        /** @brief 유닛의 지금 포즈 보기입니다(이름 · 부모 · 모델 공간). */
        PhysicsSkeletonView makeSkeletonView( const SkeletalMeshComponent& unit ) const;
        /** @brief 바디 @p bodyIndex 와 그 아래 바디들에 @p value 를 표시합니다(@p inoutListMask). */
        void markSubtree( int32 bodyIndex, vector<uint8>& inoutListMask ) const;
        /** @brief 게임 스레드 — 타이머를 흘리고 바디 가중치 · 동적 여부 · 뼈 가중치를 다시 정합니다. */
        void updateWeights( SkeletalMeshComponent& unit, float32 deltaSeconds );
        /** @brief 후처리 — 애니메이션 포즈와 물리 자세를 뼈 가중치로 섞습니다(워커). */
        void blendPhysicsPose( SkeletalMeshComponent& unit );
        /** @brief 이 오브젝트의 캐릭터 컨트롤러를 켜고 끕니다(래그돌 동안 캡슐이 남지 않게). */
        void setControllerActive( bool bActive );

        PROPERTY( Category = "Ragdoll", DisplayName = "Physics Asset", AssetPath, AssetType = "PhysicsAsset", Tooltip = "Bodies, joints and hit zones (*.physics.xml)" )
        string _physicsAssetPath;
        PROPERTY( Category = "Ragdoll", DisplayName = "Partial Root Bone", Tooltip = "Bodies under this bone go limp in a partial ragdoll" )
        hashed_string _partialRootBone;
        PROPERTY( Category = "Ragdoll", DisplayName = "Get Up Clip (Face Up)", Tooltip = "Clip played when getting up lying on the back" )
        hashed_string _getUpClipFaceUp;
        PROPERTY( Category = "Ragdoll", DisplayName = "Get Up Clip (Face Down)", Tooltip = "Clip played when getting up lying on the front (empty: the face-up clip)" )
        hashed_string _getUpClipFaceDown;
        PROPERTY( Category = "Ragdoll", DisplayName = "Get Up Exit State", Tooltip = "State played when the get-up clip ends (empty: stay)" )
        hashed_string _getUpExitState;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Flinch Clip", Tooltip = "Additive clip played once on a non-fatal hit (empty: none)" )
        hashed_string _flinchClip;
        PROPERTY( Category = "Ragdoll", DisplayName = "Blend Back Seconds", Min = 0.0, Max = 3.0, Meta = "Units=s" )
        float32 _blendBackSeconds;
        PROPERTY( Category = "Ragdoll", DisplayName = "Settle Speed", Min = 0.0, Tooltip = "Bodies slower than this count as resting", Meta = "Units=m/s" )
        float32 _settleSpeed;
        PROPERTY( Category = "Ragdoll", DisplayName = "Settle Seconds", Min = 0.0, Meta = "Units=s" )
        float32 _settleSeconds;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Reaction Seconds", Min = 0.0, Max = 3.0, Meta = "Units=s" )
        float32 _hitReactionSeconds;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Reaction Weight", Min = 0.0, Max = 1.0, Tooltip = "Physics blend weight right after a hit" )
        float32 _hitReactionWeight;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Flinch Seconds", Min = 0.0, Max = 3.0, Meta = "Units=s" )
        float32 _flinchSeconds;
        PROPERTY( Category = "Ragdoll", DisplayName = "State", ReadOnly, Transient )
        RagdollState _state;
        PROPERTY( Category = "Ragdoll", DisplayName = "Ragdoll On Fatal Hit", Tooltip = "Go ragdoll when a hit is marked fatal" )
        bool _bRagdollOnFatalHit;
        PROPERTY( Category = "Ragdoll", DisplayName = "Auto Get Up", Tooltip = "Get up by itself once the ragdoll settles" )
        bool _bAutoGetUp;

        RagdollAnimationBinding        _binding;
        shared_ptr<const PhysicsAsset> _asset;
        PhysicsRagdoll                 _ragdoll;
        vector<hashed_string>          _listBoneName;
        vector<float32>                _listBodyWeight;       ///< 바디마다 물리 섞임 가중치
        vector<float32>                _listReactionTime;     ///< 바디마다 남은 맞음 반응 시간(초)
        vector<uint8>                  _listBodyDynamic;      ///< 바디마다 동적이어야 하는가(가중치 > 0)
        vector<uint8>                  _listAppliedDynamic;   ///< 바디에 실제로 든 종류
        vector<uint8>                  _listPartialMask;      ///< 부분 래그돌에 드는 바디
        vector<float32>                _listBlendStartWeight; ///< 돌아가기를 시작할 때의 바디 가중치
        vector<float32>                _listBoneWeight;       ///< 뼈마다 섞임 가중치(후처리가 읽는다)
        vector<float4x4>               _listPhysicsModel;     ///< 지난 물리 프레임의 뼈 모델 공간 행렬(오브젝트 기준)
        vector<float3>                 _listKinematicStartPosition;
        vector<float3>                 _listKinematicTargetPosition;
        vector<quaternion>             _listKinematicStartRotation;
        vector<quaternion>             _listKinematicTargetRotation;
        vector<PendingImpulse>         _listPendingImpulse;
        Pose                           _physicsPose; ///< 후처리 스크래치
        const Skeleton*                _pBoneNameSkeleton;
        SkeletalMeshComponent*         _pUnit;
        uint32                         _seenAssetReloadCount;
        int32                          _flinchLayer;
        float32                        _partialWeight;
        float32                        _settleElapsed;
        float32                        _blendBackElapsed;
        float32                        _flinchElapsed;
        float32                        _lastDeltaSeconds;
        bool                           _bHasPhysicsPose;
        bool                           _bSettled;
        bool                           _bRebuild;
        bool                           _bGettingUp; ///< 기상 클립을 재생 중 — 끝나면 `_getUpExitState`
    };
} // namespace sw
