/**
 * @file BodySurfacePick.h
 * @brief 미리보기 몸의 표면을 광선으로 집습니다 — 맞은 메시 구간(별칭 대응 · 강조)과 그 자리의 UV(문신 · 데칼 배치). 캐릭터 편집 창의 몸 탭이 쓴다.
 * @details 메시의 CPU 정점(바인드 자세 — `Mesh::getVertices`)을 몸 월드로 옮겨 삼각형마다 광선과 맞춘다(`CharacterGeometryUtil::intersectRayTriangle`, 양면). 가장 가까운 맞음이 답이다.
 *          shortcut: 바인드 자세로 집는다 — 미리보기 몸이 클립을 틀면 자세와 어긋난다, 집기를 스킨 결과(CPU 스키닝)로 하게 되면 그 정점으로 바꾼다.
 *          언리얼 스켈레탈 메시 에디터의 섹션 클릭 선택 · 데칼 배치가 하는 일이다(그쪽은 렌더 히트 프록시). ImGui 와 씬을 모릅니다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct MeshSection;
    struct RHIVertex;
} // namespace sw

namespace sw::editor
{
    /** @brief 표면 집기의 결과입니다. */
    struct BodySurfaceHit
    {
        float3  _position{};                                   ///< 맞은 월드 자리
        float2  _uv{};                                         ///< 맞은 자리의 UV(무게중심 보간)
        float32 _distance{ 0.0f };                             ///< 광선 시작에서의 거리
        uint32  _section{ constant::invalid_index::kUint32 };  ///< 맞은 삼각형이 든 구간 번호(구간이 없으면 무효)
        uint32  _triangle{ constant::invalid_index::kUint32 }; ///< 맞은 삼각형 번호

        /**
         * @brief 광선(@p origin, 단위 방향 @p direction)이 메시(@p world 로 옮긴 정점 · 인덱스)와 처음 맞는 자리를 찾습니다. 맞지 않으면 false 입니다.
         * @param listSection 메시 구간입니다(맞은 삼각형의 구간 번호를 고른다). 비면 구간 번호는 무효다.
         */
        [[nodiscard]] static bool pick( vector_reference<const RHIVertex> listVertex, vector_reference<const uint32> listIndex, vector_reference<const MeshSection> listSection,
                                        const float4x4& world, const float3& origin, const float3& direction, BodySurfaceHit& outHit );
        /** @brief 삼각형 @p triangle(인덱스 칸 triangle × 3)이 든 구간 번호입니다. 없으면 무효입니다. */
        static uint32 findSection( vector_reference<const MeshSection> listSection, uint32 triangle );
    };
} // namespace sw::editor
