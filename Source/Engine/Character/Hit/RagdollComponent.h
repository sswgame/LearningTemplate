/**
 * @file RagdollComponent.h
 * @brief 래그돌과 히트박스입니다. 캐릭터의 물리 에셋(`*.physics.xml`)으로 뼈마다 바디를 만들고, 평소에는 애니메이션 포즈를 따르는 키네마틱 히트박스로 둡니다.
 *        죽으면 동적 래그돌이 되고, 맞으면 그 뼈 아래만 잠깐 물리(맞음 반응)와 가산 움찔을 적용합니다. 상체만 래그돌로 만들 수도 있고(부분), 가라앉으면 기상 클립으로 섞어 돌아옵니다.
 * @details 언리얼의 Physics Asset, Physical Animation(바디마다 물리 섞임 가중치), 히트 존과 유니티의 래그돌, 애니메이터 블렌드에 해당한다.
 *
 *          **바디마다 물리 가중치 하나**(0 = 애니메이션, 1 = 물리)로 모든 경우를 처리한다. 가중치가 0 이면 키네마틱(포즈로 끌려감)이고, 0 보다 크면
 *          동적이다. 포즈는 후처리 단계(PostProcess)에서 애니메이션 포즈와 지난 물리 프레임의 바디 자세를 뼈 가중치(바디 없는 뼈는 가장 가까운 조상
 *          바디의 가중치)로 섞어서 만든다. 그래서 물리 자세는 한 프레임 늦다.
 *
 *          한 프레임은 다음 순서로 진행한다. 틱(게임이 맞음이나 죽음을 호출한다), 애니메이션 평가(후처리에서 섞고, 마무리에서 가중치와 타이머를 갱신한다),
 *          물리 프레임이다. 물리 프레임의 시작에서는 바디 종류와 충격량을 적용하고, 스텝마다 키네마틱 바디를 이번 포즈까지 나눠 움직이며,
 *          끝에서는 바디 자세를 모델 공간으로 읽어 두고 가라앉음을 판정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Physics/PhysicsComponent.h"
#include "Engine/Physics/Asset/PhysicsRagdoll.h"
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
        Animated = 0, ///< 키네마틱 히트박스다(맞음 반응일 때만 잠깐 동적이 된다).
        Ragdoll,      ///< 모든 바디가 동적이다(죽음).
        Partial,      ///< 부분 래그돌이다. `_partialRootBone` 아래만 동적이다.
        BlendingBack, ///< 래그돌 자세에서 애니메이션(기상 클립)으로 섞으며 돌아가는 중이다. 바디는 키네마틱이다.
    };
} // namespace sw

namespace sw
{
    /** @class RagdollAnimationBinding @brief 유닛의 단계에 끼어드는 어댑터입니다(리플렉션 컴포넌트는 기반 클래스 하나만 둘 수 있습니다). */
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
        /** @brief 맞았을 때 호출합니다. 치명이면(`HitInfo::_bFatal`) 래그돌로 바꾸고, 아니면 맞은 바디 아래에 반응과 가산 움찔을 적용합니다. */
        void onHitReceived( const HitInfo& hit ) override;

        /** @brief 물리 에셋 경로를 바꿉니다(바디를 다시 만듭니다). */
        void          setPhysicsAssetPath( string_view path );
        const string& getPhysicsAssetPath() const { return _physicsAssetPath; }
        /** @brief 물리 에셋입니다(없으면 nullptr). */
        const PhysicsAsset* getPhysicsAsset() const { return _asset.get(); }
        /** @brief 만든 바디들을 반환합니다(시작 전이거나 에셋이 없으면 비어 있습니다). */
        const PhysicsRagdoll& getRagdoll() const { return _ragdoll; }
        /** @brief 지금 상태입니다. */
        RagdollState getState() const { return _state; }
        /** @brief 기상 클립(누움, 엎드림)과 끝난 뒤 상태를 정합니다(코드와 테스트용이며, 보통은 PROPERTY 데이터입니다). */
        void setGetUpClips( const hashed_string& faceUpClip, const hashed_string& faceDownClip, const hashed_string& exitState );
        /** @brief 가산 움찔 클립을 정합니다. */
        void setFlinchClip( const hashed_string& clip ) { _flinchClip = clip; }
        /** @brief 바디 하나의 물리 섞임 가중치(0 = 애니메이션, 1 = 물리)입니다. */
        float32 getBodyWeight( uint32 bodyIndex ) const;
        /** @brief 바디가 지금 동적인지입니다. */
        bool isBodyDynamic( uint32 bodyIndex ) const;

        /** @brief 죽음입니다. 모든 바디를 동적으로 넘깁니다(속도는 애니메이션이 끌던 값을 이어받습니다). @p bodyIndex 의 바디에 @p impulse 를 줍니다(-1 이면 주지 않습니다). */
        void startRagdoll( const float3& impulse, int32 bodyIndex );
        /** @brief 부분 래그돌입니다. `_partialRootBone` 아래 바디를 @p weight 만큼 섞는 동적 바디로 바꿉니다(상체가 축 늘어집니다). */
        void startPartialRagdoll( float32 weight );
        /** @brief 부분 래그돌을 끝내고 `_blendBackSeconds` 동안 애니메이션으로 섞어 돌아갑니다. */
        void stopPartialRagdoll();
        /** @brief 바디가 가라앉았는지입니다(래그돌 상태에서 모든 바디가 `_settleSpeed` 보다 느리게 `_settleSeconds` 동안). */
        bool isSettled() const { return _bSettled; }
        /**
         * @brief 기상입니다. 오브젝트를 골반 아래로 옮겨 기상 클립(엎드림이나 누움에 따라)의 첫 자세에 맞추고, 래그돌 자세에서 그 클립으로 섞어 돌아갑니다.
         * @return 래그돌이 아니거나 기상 클립이 없으면 false 를 반환합니다.
         */
        bool getUp();
        /** @brief 맞음 반응입니다. @p bodyIndex 바디 아래를 `_hitReactionSeconds` 동안 동적으로 바꾸고(가중치는 줄어듭니다), 충격량은 그 바디의 @p point 에 줍니다. */
        void applyHitReaction( int32 bodyIndex, const float3& impulse, const float3& point );
        /**
         * @brief 이 뼈들의 바디를 래그돌에서 분리합니다. 시뮬레이션에서 빼고, 그 바디에 걸린 관절(부모와 자식 쪽)을 지웁니다.
         *        잘려 나간 영역에서 보이지 않는 바디가 부딪히거나, 빠진 바디를 잇는 관절이 남지 않게 하려는 것입니다. 되돌리지 않으며, 다시 만들려면 물리 에셋을 다시 읽어야 합니다.
         */
        void detachBoneBodies( const vector<hashed_string>& listBone );
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
        /** @brief 유닛의 현재 포즈 뷰입니다(이름, 부모, 모델 공간). */
        PhysicsSkeletonView makeSkeletonView( const SkeletalMeshComponent& unit ) const;
        /** @brief 바디 @p bodyIndex 와 그 아래 바디들에 @p value 를 표시합니다(@p inoutListMask). */
        void markSubtree( int32 bodyIndex, vector<uint8>& inoutListMask ) const;
        /** @brief 게임 스레드에서 타이머를 흘리고, 바디 가중치, 동적 여부, 뼈 가중치를 다시 정합니다. */
        void updateWeights( SkeletalMeshComponent& unit, float32 deltaSeconds );
        /** @brief 후처리 단계에서 애니메이션 포즈와 물리 자세를 뼈 가중치로 섞습니다(워커). */
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
        PROPERTY( Category = "Ragdoll", DisplayName = "Blend Back Seconds", Min = 0.0, Max = 3.0, Units = s )
        float32 _blendBackSeconds;
        PROPERTY( Category = "Ragdoll", DisplayName = "Settle Speed", Min = 0.0, Tooltip = "Bodies slower than this count as resting", Units = "m/s" )
        float32 _settleSpeed;
        PROPERTY( Category = "Ragdoll", DisplayName = "Settle Seconds", Min = 0.0, Units = s )
        float32 _settleSeconds;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Reaction Seconds", Min = 0.0, Max = 3.0, Units = s )
        float32 _hitReactionSeconds;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Reaction Weight", Min = 0.0, Max = 1.0, Tooltip = "Physics blend weight right after a hit" )
        float32 _hitReactionWeight;
        PROPERTY( Category = "Hit Reaction", DisplayName = "Flinch Seconds", Min = 0.0, Max = 3.0, Units = s )
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
        vector<float32>                _listBodyWeight;       ///< 바디마다 물리 섞임 가중치다.
        vector<float32>                _listReactionTime;     ///< 바디마다 남은 맞음 반응 시간이다(초).
        vector<uint8>                  _listBodyDynamic;      ///< 바디마다 동적이어야 하는지 나타낸다(가중치 > 0).
        vector<uint8>                  _listAppliedDynamic;   ///< 바디에 실제로 적용된 종류다.
        vector<uint8>                  _listPartialMask;      ///< 부분 래그돌에 속한 바디다.
        vector<float32>                _listBlendStartWeight; ///< 돌아가기를 시작할 때의 바디 가중치다.
        vector<float32>                _listBoneWeight;       ///< 뼈마다 섞임 가중치다(후처리가 읽는다).
        vector<float4x4>               _listPhysicsModel;     ///< 지난 물리 프레임의 뼈 모델 공간 행렬이다(오브젝트 기준).
        vector<float3>                 _listKinematicStartPosition;
        vector<float3>                 _listKinematicTargetPosition;
        vector<quaternion>             _listKinematicStartRotation;
        vector<quaternion>             _listKinematicTargetRotation;
        vector<PendingImpulse>         _listPendingImpulse;
        Pose                           _physicsPose; ///< 후처리용 스크래치 버퍼다.
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
        bool                           _bGettingUp; ///< 기상 클립을 재생 중이다. 끝나면 `_getUpExitState` 로 간다.
    };
} // namespace sw
