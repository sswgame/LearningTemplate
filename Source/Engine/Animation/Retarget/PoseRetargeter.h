/**
 * @file PoseRetargeter.h
 * @brief 비율이 다른 두 스켈레톤 사이에서 포즈를 옮기는 리타기터(런타임)와, 클립 하나를 대상 스켈레톤 클립으로 베이크하는 오프라인 경로입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Asset/AssetID.h"

namespace sw
{
    struct AnimationCodecSettings;

    class AnimationClip;
    class IAnimationCodec;
    class Skeleton;
} // namespace sw

namespace sw
{
    /**
     * @class PoseRetargeter
     * @brief 프로필 하나를 원본과 대상 스켈레톤에 바인딩해 포즈를 옮깁니다. 언리얼 IK Retargeter 의 골반, 사슬 FK, IK 목표 단계를 한 함수로 처리합니다.
     * @details 한 번 옮길 때 다음 순서로 처리한다.
     *          1. 원본 포즈의 모델 공간을 구한다.
     *          2. 대상은 레퍼런스(또는 넘겨받은 덮어쓰기, 즉 본 비율 `BoneProportion` 을 적용한 포즈)에서 시작한다.
     *          3. 짝지은 본(루트, 골반, 사슬)은 **모델 공간 회전 차이**를 옮긴다. 대상 = (원본 × 원본 레퍼런스⁻¹) × 대상 레퍼런스다. 두 스켈레톤의
     *             로컬 축 약속이 달라도 맞다. 본 수가 다른 사슬은 사슬 길이 비율로 원본 본을 고른다.
     *          4. 루트와 골반 이동은 골반 높이 비(대상 / 원본)만큼 줄이거나 늘린다(`ScaleByPelvisHeight`).
     *          5. IK 목표 사슬(다리)은 끝을 "원본 끝 위치 × 높이 비"에 두도록 2 본 IK(본 셋) 또는 FABRIK 으로 푼다. 다리 길이가 달라도 발이
     *             보폭과 맞게 디뎌 미끄러지지 않는다. 다 펴도 닿지 않는 목표(보폭 끝)면 골반을 그만큼 내린다. 끝 본의 모델 회전은 3 의 값을 유지한다.
     */
    class SW_API PoseRetargeter
    {
    public:
        PoseRetargeter();

        /**
         * @brief 묶습니다. 프로필의 본 이름이 어느 스켈레톤에 없으면 오류이고 false 입니다.
         * @param pTargetReference 대상의 레퍼런스 덮어쓰기(본 비율을 건 포즈 등, 본 수 = 대상 본 수). nullptr 이면 대상 스켈레톤의 레퍼런스입니다.
         */
        [[nodiscard]] bool initialize( const RetargetProfile& profile, const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton, const Pose* pTargetReference );
        bool               isInitialized() const { return _bInitialized == SW_TRUE; }
        /** @brief 골반 높이 비(대상 / 원본)입니다. */
        float32 getHeightRatio() const { return _heightRatio; }
        /** @brief 대상 레퍼런스 포즈입니다(덮어쓰기 포함). */
        const Pose& getTargetReference() const { return _targetReference; }
        /** @brief 짝지은 대상 본 수입니다(루트 · 골반 · 사슬 · 이름 테이블 · 같은 이름, 겹침 제외). 나머지 대상 본은 레퍼런스 그대로입니다. */
        uint32 getMappedBoneCount() const { return static_cast<uint32>( _listPair.size() ); }
        /** @brief 원본과 대상의 루트 본 번호입니다(루트 모션 트랙 쌍). */
        uint32 getSourceRootBone() const { return _sourceRoot; }
        uint32 getTargetRootBone() const { return _targetRoot; }

        /** @brief 원본 로컬 포즈(원본 스켈레톤 순서)를 대상 로컬 포즈(대상 스켈레톤 순서)로 옮깁니다. */
        void retarget( const Pose& sourcePose, Pose& outTargetPose );

    private:
        struct BonePair
        {
            quaternion _sourceReferenceRotation{};
            quaternion _targetReferenceRotation{};
            uint32     _source{ 0 };
            uint32     _target{ 0 };
        };

        struct IKChain
        {
            vector<uint32> _listTargetBone{};
            float3         _sourceEndReference{};
            float3         _targetEndReference{};
            uint32         _sourceEnd{ 0 };
        };

        /** @brief 짝을 대상 본 순서로 세울 때의 비교입니다. */
        static bool isPairBefore( const BonePair& lhs, const BonePair& rhs ) { return lhs._target < rhs._target; }

        /** @brief 사슬의 원본 · 대상 본을 짝지어 더하고(본 수가 다르면 길이 비율로 가장 가까운 원본 본) IK 사슬을 모읍니다. 본을 못 찾으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool collectChainBones( const RetargetProfile& profile, const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton, vector<uint32>& inoutListSourceBone,
                                              vector<uint32>& inoutListTargetBone );
        /** @brief 본 이름 짝과 같은 이름 매핑을 더합니다. 본을 못 찾으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool collectNamedBones( const RetargetProfile& profile, const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton, vector<uint32>& inoutListSourceBone,
                                              vector<uint32>& inoutListTargetBone ) const;
        /** @brief 모은 본 짝에서 대상 본이 겹치지 않게(먼저 모은 짝이 이긴다) 짝 목록을 만들고 대상 본 순서(부모가 먼저)로 세웁니다. */
        void populatePairs( const vector<uint32>& listSourceBone, const vector<uint32>& listTargetBone );

        vector<BonePair>        _listPair; ///< 대상 본 순서(부모가 먼저)
        vector<IKChain>         _listIKChain;
        vector<float3>          _listGoal; ///< IK 목표(사슬마다, 재사용)
        vector<int32>           _listSourceParent;
        vector<int32>           _listTargetParent;
        Pose                    _targetReference;
        RigPoseBuffer           _sourceBuffer;
        RigPoseBuffer           _targetBuffer;
        float3                  _sourceRootReference;
        float3                  _targetRootReference;
        float3                  _sourcePelvisReference;
        float3                  _targetPelvisReference;
        float32                 _heightRatio;
        uint32                  _sourceRoot;
        uint32                  _targetRoot;
        uint32                  _sourcePelvis;
        uint32                  _targetPelvis;
        RetargetTranslationMode _translationMode;
        uint8                   _bInitialized;
    };
} // namespace sw

namespace sw
{
    /** @brief 프로필의 리타깃 포즈(`target_pose`)를 대상 포즈에 얹는 길 하나입니다 — 리타기터 묶기(`initialize`)와 에디터 리타깃 창의 미리보기가 같이 씁니다. */
    struct SW_API RetargetPoseUtil
    {
        /**
         * @brief @p inoutPose(본 수 = 대상 스켈레톤 본 수)의 대상 본마다 프로필의 돌림을 부모 공간에서 더 돌립니다(새 로컬 회전 = 돌림 × 로컬 회전).
         * @return 대상 스켈레톤에 없는 본을 적었거나 본 수가 다르면 오류를 남기고 false 입니다(포즈는 그대로입니다).
         */
        [[nodiscard]] static bool applyTargetPose( const RetargetProfile& profile, const Skeleton& targetSkeleton, Pose& inoutPose );
    };
} // namespace sw

namespace sw
{
    /** @brief 오프라인 리타깃 베이크입니다(쿠킹과 도구에서 씁니다). */
    struct SW_API RetargetBakeUtil
    {
        /**
         * @brief 원본 클립을 @p sampleRate 로 샘플해 리타깃하고, 대상 스켈레톤 트랙(본 이름)의 클립으로 압축합니다. 알림, 커브, 반복, 루트 모션
         *        트랙(이름으로 짝지음)을 옮깁니다. 파일로 쓰는 일은 호출하는 쪽이 합니다(`AnimationClip::saveToFile`).
         */
        [[nodiscard]] static bool bakeClip( PoseRetargeter& retargeter, const AnimationClip& sourceClip, const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton,
                                            float32 sampleRate, const IAnimationCodec& codec, const AnimationCodecSettings& settings, AnimationClip& outClip );
        /**
         * @brief 파일에서 파일로 베이크합니다. 프로필(`*.retarget.xml`)이 지정한 두 스켈레톤을 읽고, 원본 클립을 30 Hz 로 샘플해 원본과 같은 코덱으로
         *        압축한 뒤 @p outClipPath 에 씁니다. `App --bake-retarget=<프로필>,<원본 클립>,<출력 클립>` 이 호출합니다.
         */
        [[nodiscard]] static bool bakeClipFile( const AssetID& profileID, const AssetID& sourceClipID, string_view outClipPath );
    };
} // namespace sw

namespace sw
{
    /** @brief 사슬 하나의 레퍼런스 길이 쌍입니다(m). 비가 크게 다르면 IK 목표 사슬이 필요하다는 신호다. */
    struct RetargetChainInspection
    {
        hashed_string _name{};
        float32       _sourceLength{ 0.0f };
        float32       _targetLength{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 프로필 점검 결과입니다(에디터 리타깃 창이 보인다). */
    struct RetargetProfileInspection
    {
        vector<RetargetChainInspection> _listChain{};
        float32                         _heightRatio{ 0.0f };
        uint32                          _mappedTargetBoneCount{ 0 };
        uint32                          _targetBoneCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 프로필을 두 스켈레톤에 묶어 보는 점검입니다 — 런타임 리타기터와 같은 검사 하나(에디터 쪽 사본 규칙을 두지 않는다). */
    struct SW_API RetargetProfileInspector
    {
        /**
         * @brief 프로필이 적은 두 스켈레톤을 읽고 리타기터를 묶어 봅니다. 사슬마다 레퍼런스 길이와 짝지은 대상 본 수를 냅니다.
         * @return 스켈레톤을 읽지 못하거나 묶이지 않으면 false 이고 까닭을 @p outError 에 영어 한 문장으로 담습니다(본 이름은 로그).
         */
        [[nodiscard]] static bool inspect( const RetargetProfile& profile, RetargetProfileInspection& outInspection, string& outError );
    };
} // namespace sw
