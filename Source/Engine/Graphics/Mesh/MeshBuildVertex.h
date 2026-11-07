/**
 * @file Engine/Graphics/Mesh/MeshBuildVertex.h
 * @brief 게임 코드가 메시를 지을 때 넘기는 정점(위치 · 노멀 · UV · 색)입니다 — GPU 정점 칸(`RHIVertex`)의 배치를 모르고 메시를 짓습니다.
 * @details `RHIVertex` 로 옮기는 일은 메시(`Mesh::setBuildVertices`)가 한다. 언리얼 `FDynamicMeshVertex` → 정점 버퍼가 렌더 쪽에서 갈리는 것과 같은 자리다(기억).
 */
#pragma once
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @struct MeshBuildVertex @brief 삼각형 목록 정점 하나입니다(탄젠트는 메시가 구한다). */
    struct MeshBuildVertex
    {
        float3 _position{};
        float3 _normal{};
        float2 _uv{};
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };
} // namespace sw
