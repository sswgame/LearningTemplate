/**
 * @file MeshVertex.h
 * @brief CPU 쪽 메시 정점(`RHIVertex` — 임포트 · 지형 · 파괴 · 천 · 물이 짓는 기하)입니다. RHI 형식을 모르는 값 헤더라 게임이 보는 메시 · 파괴 · 물 헤더가 RHI 헤더 없이 쓴다.
 * @details GPU 정점 배치(속성 스트림 · 형식)는 `RHIDescriptors.h` 다(언리얼 `FStaticMeshBuildVertex` 와 정점 버퍼 선언이 나뉜 자리). 이름의 RHI 머리는 옛 자리의 흔적이다 — 개명은 이름 전수 집계 때.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct RHIVertex
     * @brief CPU 쪽 메시 정점 하나입니다(임포트 · 지형 · 파괴 · 천 등 기하 처리가 쓰는 형식). 위치, 노멀, UV, 색, 탄젠트를 가집니다.
     * @details GPU 에는 이 모양 그대로 올리지 않는다 — 올릴 때 위치 스트림과 속성 스트림으로 나누고 메시의 정점 형식(`RHIMeshVertexFormat`)으로
     *          바꾼다(`RHIMeshVertexStreams`). 언리얼의 빌드 정점(`FStaticMeshBuildVertex`)과 GPU 정점 버퍼의 관계와 같다.
     */
    struct RHIVertex
    {
        float32 _arrPosition[3]; ///< 정점 위치 (X, Y, Z)
        /**
         * @brief 정점 노멀 (X, Y, Z)입니다. 정규화되어 있어야 합니다.
         * @details 주의: 셰이더에서 위치로 노멀을 만들어 내지 않는다. 원점 중심 박스형 도형에만 맞아서 평면, 구, 원뿔이
         *          조용히 틀린 빛을 받는다(`y = 0` 바닥 평면이 옆을 보는 것처럼 칠해진다).
         */
        float32 _arrNormal[3];
        /**
         * @brief 텍스처 좌표 (U, V)입니다.
         * @details 노멀과 **같은 함정**이 있다. 위치로 만들어 내면(`xy * 0.5 + 0.5`) 원점 중심 단위 도형이 아닐 때 알베도가
         *          엉뚱하게 붙고 옆면과 뚜껑이 같은 위치를 읽는다. 도형 생성기가 면마다 UV 를 펼쳐 준다.
         */
        float32 _arrUV[2];
        float32 _arrColor[4]; ///< 정점 색상 (R, G, B, A)
        /**
         * @brief 탄젠트 (X, Y, Z)와 바이탄젠트 부호(W, ±1)입니다. 노멀 맵이 탄젠트 공간을 세울 때 씁니다(glTF · MikkTSpace 규약 — 바이탄젠트 = cross( N, T ) × W).
         * @details 위치 · 노멀 · UV 에서 나오는 값이라 손으로 채우지 않는다. `Mesh::setVertices` 가 `Mesh::computeTangents` 로 구하고,
         *          `.mesh` 는 임포트 때 구한 값을 싣는다(`Mesh::setVerticesWithTangents`). 노멀과 같은 함정 — 셰이더가 다른 값에서 지어내지 않는다.
         */
        float32 _arrTangent[4];
    };

    /**
     * @brief GPU 메시 정점의 속성 스트림 형식입니다. 메시마다 고르고(`Mesh::setVertexFormat`), PSO 가 같은 형식으로 레이아웃을 짓는다(`RHIPipelineStateDesc::_meshVertexFormat`).
     * @details 위치 스트림(슬롯 0, float3)은 두 형식이 같다. 언리얼이 메시 종류마다 vertex factory 로 정점 선언을 고르는 자리다.
     *          메시가 고르는 값이라 RHI 헤더가 아니라 여기 둔다 — 게임이 보는 `Mesh.h` 가 RHI 헤더 없이 쓴다(배치 표는 `RHIDescriptors.h`).
     */
    enum class RHIMeshVertexFormat : uint8
    {
        Packed,        ///< 노멀 · 탄젠트 snorm8x4, UV float2, 색 float4(32 B). 노멀 · 탄젠트가 단위 벡터인 일반 메시
        HighPrecision, ///< 노멀 float3, UV float2, 색 float4, 탄젠트 float4(52 B). 색 · 탄젠트 칸에 단위 벡터가 아닌 데이터를 싣는 메시(지형 형태 변환) — 언리얼의 고정밀 탄젠트 옵션 자리
        Count,
    };
} // namespace sw
