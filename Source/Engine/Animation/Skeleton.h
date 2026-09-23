/**
 * @file Skeleton.h
 * @brief 계층형 본 배열과 거기서 나오는 스키닝 행렬입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /**
     * @brief 3D 스켈레탈 애니메이션의 본(Bone) 하나입니다.
     */
    struct SW_API Bone
    {
        hashed_string _name;                                          /**< 본 이름(intern 문자열)입니다. */
        int32         _parentIndex{ -1 };                             /**< 부모 본 인덱스입니다. 루트는 -1 이며, 항상 자신보다 작습니다. */
        float4x4      _invReferencePose{ float4x4::Identity };        /**< 레퍼런스 포즈의 역행렬입니다. */
        float4x4      _boneSpaceTransform{ float4x4::Identity };      /**< 부모 기준 로컬 변환입니다. */
        float4x4      _characterSpaceTransform{ float4x4::Identity }; /**< 캐릭터 원점 기준 변환입니다. */
    };

    /**
     * @brief 계층형 본과 최종 스키닝 행렬을 관리하는 스켈레톤입니다.
     */
    class SW_API Skeleton
    {
    public:
        Skeleton()                                 = default;
        ~Skeleton()                                = default;
        Skeleton( const Skeleton& )                = default;
        Skeleton& operator=( const Skeleton& )     = default;
        Skeleton( Skeleton&& ) noexcept            = default;
        Skeleton& operator=( Skeleton&& ) noexcept = default;

        /**
         * @brief 본 하나를 끝에 붙이고 그 인덱스를 반환합니다.
         * @param parentIndex 부모 본 인덱스. **이미 추가된 본만 가리킬 수 있습니다**(루트는 -1).
         *                    `updateCharacterSpaceTransforms` 가 배열을 앞에서 뒤로 한 번만 훑기
         *                    때문입니다. 범위를 벗어나면 아무것도 추가하지 않고 -1 을 반환합니다.
         */
        int32 addBone( string_view name, int32 parentIndex, const float4x4& invReferencePose, const float4x4& boneSpaceTransform );
        /** @brief 이름으로 본 인덱스를 찾습니다. 없으면 -1 입니다. */
        int32 findBoneIndex( const hashed_string& name ) const;

        /** @brief 본 하나의 로컬 변환을 바꿉니다. 범위를 벗어난 인덱스는 무시합니다. */
        void setBoneSpaceTransform( int32 boneIndex, const float4x4& boneSpaceTransform );
        /** @brief 로컬 변환에서 캐릭터 공간 변환과 스키닝 행렬을 다시 계산합니다. */
        void updateCharacterSpaceTransforms();

        /** @brief 정점 셰이더에 올릴 최종 스키닝 행렬 배열입니다. */
        const vector<float4x4>& getSkinningMatrices() const { return _listSkinningMatrix; }
        /** @brief 본 개수입니다. */
        size_t getBoneCount() const { return _listBone.size(); }
        /** @brief 본과 스키닝 행렬을 모두 비웁니다. */
        void clear();

    private:
        vector<Bone>     _listBone;           /**< 부모가 항상 자식보다 앞에 오는 본 배열입니다. */
        vector<float4x4> _listSkinningMatrix; /**< `_listBone` 과 같은 길이의 스키닝 행렬입니다. */
    };
} // namespace sw
