/**
 * @file FractureRenderUtil.h
 * @brief 쪼갠 조각을 한 번의 그리기로 — 조각마다 본 하나(강체 스킨, 가중치 1)인 스킨드 메시 · 스켈레톤과, 쉬는 자세를 구워 넣은 정적 메시를 만듭니다.
 * @details Chaos 의 Geometry Collection 이 조각마다 본 변환으로 그리는 것과 같은 자리입니다. 조각이 수백이어도 칸(겉면 · 안쪽 면)마다 그리기 하나이고,
 *          GPU 스키닝(모프 풀의 스킨 구간)이 정점을 옮깁니다. 본 i 의 레퍼런스 포즈는 조각 무게 중심으로의 이동, 역 바인드는 그 반대라 단위 포즈가 곧
 *          쪼개기 전 모습입니다. 런타임은 본 로컬 = (이동 · 회전 · 줄임 배율)을 씁니다(`makeBoneTransform`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Destruction/FractureAsset.h"

namespace sw
{
    class Mesh;
    class Skeleton;

    /** @brief 조각 그리기 도우미입니다(전부 static). */
    struct SW_API FractureRenderUtil
    {
        /** @brief 조각마다 루트 본 하나(이름 `piece<번호>`)인 스켈레톤입니다. */
        static shared_ptr<Skeleton> createPieceSkeleton( const FractureAsset& asset );
        /** @brief 칸 하나의 삼각형을 조각 본에 가중치 1 로 묶은 스킨드 메시입니다. 그 칸의 삼각형이 없으면 nullptr 입니다. */
        static shared_ptr<Mesh> createSkinnedMesh( const FractureAsset& asset, FractureSurfaceSlot slot );
        /**
         * @brief 조각마다 본 로컬 변환(@p listPose, 잎 순)을 정점에 구워 넣은 정적 메시입니다(쉬는 파편을 정적 그림으로 합칠 때). 배율이 0 인 조각은 뺍니다.
         *        남는 삼각형이 없으면 nullptr 입니다.
         */
        static shared_ptr<Mesh> createBakedMesh( const FractureAsset& asset, FractureSurfaceSlot slot, vector_reference<const BoneTransform> listPose );
        /**
         * @brief 조각 하나의 본 로컬 변환입니다 — 메시 공간 점 p 를 `회전( p - 무게 중심 ) × 배율 + 회전( 무게 중심 ) + 옮김` 으로 보냅니다.
         * @param rotation 오브젝트 기준 회전(쪼개기 전 = 단위)
         * @param offset 오브젝트(메시) 공간의 옮김
         */
        static BoneTransform makeBoneTransform( const float3& centroid, const quaternion& rotation, const float3& offset, float32 scale );
    };
} // namespace sw
