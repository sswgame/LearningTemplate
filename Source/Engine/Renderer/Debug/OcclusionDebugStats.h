/**
 * @file OcclusionDebugStats.h
 * @brief 가림 타일 사본(`OcclusionTileSnapshot`)을 사람이 볼 숫자로 줄입니다 — 덮은 타일 · 깊이 범위, 경계 구 목록에서 가려진 수(HUD `occlusion` 범주 · 에디터 렌더 디버거 창).
 * @details 가려진 수는 GPU 컬링과 같은 식(`OcclusionTileSnapshot::isSphereOccluded` — gpu_cull.hlsl 의 판정)이다. 사본은 몇 프레임 늦은 16 px 밉의 최대 깊이라
 *          GPU 가 이번 프레임에 거른 수와 꼭 같지는 않다(언리얼 `stat initviews` 의 Occluded Primitives 와 같은 "대략 이만큼" 이다).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"

namespace sw
{
    /**
     * @struct OcclusionTileStats
     * @brief 가림 타일 한 벌의 요약입니다.
     */
    struct SW_API OcclusionTileStats
    {
        uint32  _tileCount{ 0 };        ///< 타일 수(열 × 행)
        uint32  _coveredTileCount{ 0 }; ///< 무언가 그려진 타일(최대 깊이 < 1 — 그 뒤의 물체를 가릴 수 있다)
        float32 _nearestDepth{ 1.0f };  ///< 타일 최대 깊이 가운데 가장 가까운 값
        uint64  _serial{ 0 };           ///< 사본 번호

        /** @brief @p snapshot 을 요약합니다. 쓸 수 없는 사본(`isValid` 아님)은 0 입니다. */
        static OcclusionTileStats make( const OcclusionTileSnapshot& snapshot );
        /** @brief 경계 구(@p listSphere — xyz 중심 · w 반지름) 가운데 사본의 깊이 뒤에 가려진 수입니다. */
        static uint32 countOccludedSpheres( const OcclusionTileSnapshot& snapshot, vector_reference<const float4> listSphere );
    };
} // namespace sw
