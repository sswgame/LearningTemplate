/**
 * @file FractureRenderUtil.h
 * @brief 쪼갠 조각을 한 번의 그리기로 처리합니다. 조각마다 본 하나(강체 스킨, 가중치 1)를 둔 스킨드 메시와 스켈레톤, 쉬는 자세를 베이크한 정적 메시를 만듭니다.
 * @details Chaos 의 Geometry Collection 이 조각마다 본 변환으로 그리는 것에 해당한다. 조각이 수백 개여도 슬롯(겉면, 안쪽 면)마다 그리기 하나이고,
 *          GPU 스키닝(모프 풀의 스킨 구간)이 정점을 옮긴다. 본 i 의 레퍼런스 포즈는 조각 무게 중심으로의 이동이고 역 바인드는 그 반대다.
 *          그래서 단위 포즈가 곧 쪼개기 전 모습이다. 런타임은 본 로컬 = (이동, 회전, 축소 배율)을 쓴다(`makeBoneTransform`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"

namespace sw
{
    class Mesh;
    class Skeleton;

    /** @brief 조각 그리기 도우미입니다. 전부 static 입니다. */
    struct SW_API FractureRenderUtil
    {
        /** @brief 조각마다 루트 본 하나(이름 `piece<번호>`)를 둔 스켈레톤을 만듭니다. */
        static shared_ptr<Skeleton> createPieceSkeleton( const FractureAsset& asset );
        /** @brief 슬롯 하나의 삼각형을 조각 본에 가중치 1 로 묶은 스킨드 메시를 만듭니다. 그 슬롯의 삼각형이 없으면 nullptr 를 반환합니다. */
        static shared_ptr<Mesh> createSkinnedMesh( const FractureAsset& asset, FractureSurfaceSlot slot );
        /**
         * @brief 조각마다 본 로컬 변환(@p listPose, 잎 순)을 정점에 베이크한 정적 메시를 만듭니다. 쉬는 파편을 정적 메시로 합칠 때 씁니다. 배율이 0 인 조각은 뺍니다.
         *        남는 삼각형이 없으면 nullptr 를 반환합니다.
         */
        static shared_ptr<Mesh> createBakedMesh( const FractureAsset& asset, FractureSurfaceSlot slot, vector_reference<const BoneTransform> listPose );
        /**
         * @brief 조각 하나의 본 로컬 변환을 반환합니다. 메시 공간 점 p 를 `회전( p - 무게 중심 ) × 배율 + 회전( 무게 중심 ) + 이동` 으로 보냅니다.
         * @param rotation 오브젝트 기준 회전입니다(쪼개기 전에는 단위 회전입니다).
         * @param offset 오브젝트(메시) 공간의 이동입니다.
         */
        static BoneTransform makeBoneTransform( const float3& centroid, const quaternion& rotation, const float3& offset, float32 scale );
    };
} // namespace sw
