/**
 * @file Pose.h
 * @brief 본 로컬 변환(이동 · 회전 · 스케일)을 SoA 로 든 포즈와, 포즈 섞기 · 가산 · 마스크 · 모델 공간 · 스킨 팔레트 계산입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class Skeleton;

    /**
     * @struct BoneTransform
     * @brief 본 하나의 로컬 변환(부모 기준)입니다. 행벡터 규약의 S * R * T 순서로 합성합니다(`toMatrix`).
     */
    struct SW_API BoneTransform
    {
        float3     _translation{};
        quaternion _rotation{};
        float3     _scale{ 1.0f, 1.0f, 1.0f };

        /** @brief 행렬로 합성합니다(`float4x4::createTrs`). */
        float4x4 toMatrix() const { return float4x4::createTrs( _translation, _rotation, _scale ); }
        /** @brief 행렬을 분해합니다. 스케일이 0 인 축이 있으면 회전은 단위입니다. */
        static BoneTransform makeFromMatrix( const float4x4& matrix );
        /** @brief 두 변환을 섞습니다 — 이동 · 스케일은 선형, 회전은 짧은 쪽 nlerp 입니다. */
        static BoneTransform blend( const BoneTransform& from, const BoneTransform& to, float32 weight );
    };
} // namespace sw

namespace sw
{
    /**
     * @class Pose
     * @brief 본마다 로컬 변환을 이동 · 회전 · 스케일 배열 셋(SoA)으로 듭니다. 본 순서는 스켈레톤(또는 클립 트랙) 순서입니다.
     * @details 섞기 · 가산은 배열을 차례로 훑는 일이라 SoA 가 캐시에 맞습니다(언리얼 `FCompactPose` · ozz `SoaTransform` 자리).
     *          가산 포즈의 규약: 이동은 차이(t - tRef), 회전은 `inverse( rRef ) * r`(로컬 공간에서 먼저 적용), 스케일은 비율(s / sRef)입니다.
     */
    class SW_API Pose
    {
    public:
        Pose() = default;

        /** @brief 본 수를 맞춥니다. 늘어난 본은 단위 변환입니다. */
        void resize( uint32 boneCount );
        /** @brief 본 수입니다. */
        uint32 getBoneCount() const { return static_cast<uint32>( _listTranslation.size() ); }
        /** @brief 모든 본을 단위 변환으로 둡니다(가산 포즈의 "변화 없음"). */
        void setToIdentity();
        /** @brief 스켈레톤의 레퍼런스 포즈로 둡니다(본 수도 맞춥니다). */
        void setToReference( const Skeleton& skeleton );

        /** @brief 본 하나의 변환입니다. */
        BoneTransform getBoneTransform( uint32 boneIndex ) const;
        /** @brief 본 하나의 변환을 바꿉니다. 범위 밖이면 무시합니다. */
        void setBoneTransform( uint32 boneIndex, const BoneTransform& transform );

        const vector<float3>&     getTranslations() const { return _listTranslation; }
        const vector<quaternion>& getRotations() const { return _listRotation; }
        const vector<float3>&     getScales() const { return _listScale; }
        float3*                   getTranslationData() { return _listTranslation.data(); }
        quaternion*               getRotationData() { return _listRotation.data(); }
        float3*                   getScaleData() { return _listScale.data(); }

        /** @brief @p from 과 @p to 를 @p weight(0 = from, 1 = to)로 섞어 @p outPose 에 씁니다. 세 포즈의 본 수가 같아야 합니다(@p outPose 는 맞춥니다). */
        static void blend( const Pose& from, const Pose& to, float32 weight, Pose& outPose );
        /**
         * @brief 본마다 가중치를 곱해 섞습니다(본 마스크 레이어). 본 b 의 실제 가중치는 `weight * arrBoneWeight[b]` 입니다.
         * @param pBoneWeight 본 수만큼의 마스크(0..1). nullptr 이면 모든 본 1 입니다.
         */
        static void blendMasked( const Pose& base, const Pose& layer, float32 weight, const float32* pBoneWeight, Pose& outPose );
        /** @brief @p pose 를 @p referencePose 기준의 가산 포즈로 바꿔 @p outAdditive 에 씁니다(클래스 설명의 규약). */
        static void makeAdditive( const Pose& pose, const Pose& referencePose, Pose& outAdditive );
        /** @brief 가산 포즈를 @p weight(본마다 @p pBoneWeight 를 곱함)만큼 얹습니다. */
        void applyAdditive( const Pose& additive, float32 weight, const float32* pBoneWeight );

        /**
         * @brief 로컬 포즈에서 모델 공간 행렬을 구합니다. 부모가 자식보다 앞에 오므로 한 번 훑습니다.
         * @param listParent 본마다 부모 인덱스(루트는 -1). 길이는 본 수입니다.
         */
        void computeModelSpace( const vector<int32>& listParent, vector<float4x4>& outListModel ) const;
        /** @brief 스킨 팔레트 = 역 바인드 * 모델 공간(행벡터 규약 — 정점에 역 바인드가 먼저 걸린다)입니다. */
        static void computeSkinPalette( const Skeleton& skeleton, const vector<float4x4>& listModel, vector<float4x4>& outListPalette );

    private:
        vector<float3>     _listTranslation;
        vector<quaternion> _listRotation;
        vector<float3>     _listScale;
    };
} // namespace sw
