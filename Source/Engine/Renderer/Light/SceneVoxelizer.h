/**
 * @file SceneVoxelizer.h
 * @brief 씬의 정적 불투명 메시를 점유 복셀 격자(`SceneVoxelGrid`)로 옮깁니다(RealtimeGlobalIllumination.md R5).
 * @details 실시간 프로브 갱신(R6)이 GPU 에서 광선을 쏠 장면 표현을 만든다. 모으는 것은 프리미티브 레지스트리의 메시 컴포넌트 중 **불투명 · 보이는 · 스킨 없는** 것이다
 *          (스킨 메시와 반투명은 빛을 가리지 않는다 — 한계로 둔다). 메시의 CPU 정점(`Mesh::getVertices`, LOD 0 비인덱스 삼각형 목록)을 컴포넌트의 월드 행렬로 옮겨 칠하므로 메시 에셋이
 *          아직 읽는 중이면 그 컴포넌트는 건너뛴다. 면의 반사율은 메시가 아니라 볼륨 한 값이다(`LightProbeVolumeComponent::getBounceAlbedo` — 베이커와 같은 규칙).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/AABB.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"

namespace sw
{
    class PrimitiveRegistry;

    /** @brief 씬의 메시를 복셀 격자로 만드는 도구입니다(전부 static). */
    class SW_API SceneVoxelizer
    {
    public:
        /**
         * @brief 상자 @p bounds 를 덮는 격자를 만들고 레지스트리의 메시를 칠합니다.
         * @param cellSize 목표 칸 크기(m) — 상자가 크면 `SceneVoxelGrid::make` 가 키운다
         * @return 칠한 격자. 상자가 유효하지 않으면 빈 격자다.
         */
        [[nodiscard]] static SceneVoxelGrid makeGrid( const PrimitiveRegistry& primitives, const AABB& bounds, float32 cellSize );
    };
} // namespace sw
