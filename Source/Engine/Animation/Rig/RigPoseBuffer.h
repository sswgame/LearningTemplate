/**
 * @file RigPoseBuffer.h
 * @brief 후처리 리그의 작업 포즈 — 로컬 변환(원본)과 지연 갱신되는 모델 공간 변환(이동 · 회전 · 스케일)을 함께 듭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    /**
     * @class RigPoseBuffer
     * @brief IK · 제약 노드가 읽고 쓰는 포즈입니다. 로컬이 원본이고, 모델 공간은 읽을 때 더러운 본부터 다시 구합니다.
     * @details 행벡터 규약(모델 = 로컬 × 부모 모델)을 쿼터니언으로 풉니다 — 모델 회전 = 부모 회전 * 로컬 회전(로컬을 먼저 적용),
     *          모델 위치 = 부모 위치 + 부모 회전으로 돌린 (로컬 위치 ∘ 부모 스케일), 모델 스케일 = 로컬 스케일 ∘ 부모 스케일.
     *          균등 스케일이면 `Pose::computeModelSpace` 의 행렬과 같습니다(비균등 스케일의 전단은 버립니다).
     *          모델 공간에 쓰면(`setModelRotation` · `setModelPosition`) 그 본의 로컬을 바꾸고 자손을 더럽힙니다 — 부모가 자식보다 앞이라
     *          다음 읽기가 더러운 첫 본부터 한 번 훑습니다.
     */
    class SW_API RigPoseBuffer
    {
    public:
        RigPoseBuffer();

        /** @brief 로컬 포즈와 부모 표를 복사해 시작합니다(본 수 = 둘 중 작은 쪽). */
        void initialize( const Pose& localPose, const vector<int32>& listParent );
        /** @brief 로컬 포즈를 @p outPose 에 씁니다(본 수가 같아야 합니다). */
        void writeTo( Pose& outPose ) const;

        uint32 getBoneCount() const { return static_cast<uint32>( _listParent.size() ); }
        int32  getParent( uint32 boneIndex ) const { return _listParent[boneIndex]; }
        /** @brief @p ancestorIndex 가 @p boneIndex 의 조상(또는 자기)인지입니다. */
        bool isAncestorOf( int32 ancestorIndex, int32 boneIndex ) const;

        /** @brief 로컬 변환입니다. */
        BoneTransform getLocal( uint32 boneIndex ) const;
        /** @brief 로컬 변환을 바꾸고 자손을 더럽힙니다. */
        void setLocal( uint32 boneIndex, const BoneTransform& transform );
        /** @brief 로컬 회전만 바꿉니다. */
        void              setLocalRotation( uint32 boneIndex, const quaternion& rotation );
        const quaternion& getLocalRotation( uint32 boneIndex ) const { return _listLocalRotation[boneIndex]; }
        const float3&     getLocalTranslation( uint32 boneIndex ) const { return _listLocalTranslation[boneIndex]; }

        /** @brief 모델 공간 위치입니다. */
        float3 getModelPosition( uint32 boneIndex );
        /** @brief 모델 공간 회전입니다. */
        quaternion getModelRotation( uint32 boneIndex );
        /** @brief 모델 공간 스케일입니다. */
        float3 getModelScale( uint32 boneIndex );
        /** @brief 모델 공간 행렬입니다(`createTrs`). */
        float4x4 getModelMatrix( uint32 boneIndex );

        /** @brief 모델 공간 회전을 정합니다(로컬 회전 = 부모 모델 회전의 역 * 회전). */
        void setModelRotation( uint32 boneIndex, const quaternion& rotation );
        /** @brief 모델 공간 위치를 정합니다(로컬 이동을 바꿉니다). */
        void setModelPosition( uint32 boneIndex, const float3& position );
        /** @brief 모델 공간에서 @p delta 만큼 더 돌립니다(지금 회전 뒤에 적용). */
        void rotateModel( uint32 boneIndex, const quaternion& delta );
        /**
         * @brief @p boneIndex 를 돌려 @p childIndex 의 모델 위치가 @p newChildPosition 방향을 향하게 합니다(길이는 그대로).
         * @details 사슬 IK 가 풀어 낸 관절 위치를 회전으로 옮기는 자리입니다 — 본 → 자식 방향을 새 방향으로 돌리는 최소 회전입니다.
         */
        void aimBoneAt( uint32 boneIndex, uint32 childIndex, const float3& newChildPosition );

    private:
        /** @brief 더러운 본부터 모델 공간을 다시 구합니다. */
        void refreshModel();
        void markDirty( uint32 boneIndex );

        vector<int32>      _listParent;
        vector<float3>     _listLocalTranslation;
        vector<quaternion> _listLocalRotation;
        vector<float3>     _listLocalScale;
        vector<float3>     _listModelPosition;
        vector<quaternion> _listModelRotation;
        vector<float3>     _listModelScale;
        vector<uint8>      _listDirty;
        uint32             _firstDirty; ///< 더러운 첫 본(없으면 본 수)
    };
} // namespace sw
