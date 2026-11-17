/**
 * @file VFXParticleOrder.h
 * @brief 방출기 안 입자의 그리는 순서(`VFXSortMode`)를 입자 번호 목록으로 만드는 순수 계산입니다. 렌더러 없이 테스트합니다.
 * @details 입자 데이터는 옮기지 않고 순서 목록(입자 번호의 `uint32` 배열)만 만든다 — 뷰들이 입자 버퍼 하나를 함께 쓰고, 셰이더는 순서 구간으로 입자를 고른다(PS-2).
 *          `ViewDepth` 는 (방출기, 뷰)마다 구간 하나, 나이 순서는 방출기마다 하나다. 깊이 규칙은 메시 투명 정렬(`GPUTransparentSorter::makeTransparentSortKey`)과 같다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/VFX/VFXParticle.h"

namespace sw
{
    /**
     * @struct VFXParticleOrder
     * @brief 프레임 순서 목록 안에서 방출기 하나(뷰 깊이면 방출기 × 뷰 하나)의 구간입니다. 만드는 함수가 결과를 목록 끝에 붙이고 그 구간을 돌려줍니다.
     * @details 구간이 없으면(정렬하지 않는 방출기) `_first` 가 무효 값이고 셰이더는 인스턴스 번호를 그대로 쓴다.
     */
    struct SW_API VFXParticleOrder
    {
        uint32 _first{ constant::invalid_index::kUint32 };
        uint32 _count{ 0 };

        /**
         * @brief 먼 것부터(뒤 → 앞) 그리는 순서를 @p outListOrder 끝에 붙이고 그 구간을 돌려줍니다. 같은 깊이는 입자 번호 순(안정)입니다.
         * @param world 방출기 행렬(행벡터 규약) — 입자는 방출기 로컬 좌표다
         * @param eye 뷰의 눈 위치(월드)
         * @param axis 깊이 축(`GPUTransparentSorter::getAxis` — 직교 카메라는 시선 방향). 영벡터면 눈까지의 거리로 잰다
         */
        static VFXParticleOrder appendViewDepth( vector_reference<const GPUParticle> listParticle, const float4x4& world, const float3& eye, const float3& axis,
                                                 vector<uint32>& outListOrder );
        /**
         * @brief 나이 순서를 @p outListOrder 끝에 붙이고 그 구간을 돌려줍니다. 나이는 수명 비율(`_rotationLife.y`)이고 같은 나이는 입자 번호 순입니다. 뷰와 무관합니다.
         * @param bOldestFirst true 면 오래된 입자부터(`OldestFirst`), false 면 어린 입자부터(`YoungestFirst`)
         */
        static VFXParticleOrder appendByAge( vector_reference<const GPUParticle> listParticle, bool bOldestFirst, vector<uint32>& outListOrder );
    };
} // namespace sw
