/**
 * @file BodySurfacePick.h
 * @brief 미리보기 캐릭터(몸과 부품 오브젝트)의 표면을 광선으로 집습니다 — 맞은 메시 구간(별칭 대응 · 강조)과 그 자리의 UV(문신 · 데칼 배치). 캐릭터 편집 창의 몸 탭이 쓴다.
 * @details 표면 하나(`BodyPickSurface`)는 오브젝트 하나의 메시를 **지금 포즈**로 월드에 옮긴 정점이다 — 스킨 정점이면 그 유닛의 스킨 팔레트(GPU 스키닝과 같은 행렬)로
 *          CPU 스키닝하고(`MeshSkinBinding::skinVertex`), 아니면 바인드 정점을 월드로만 옮긴다. 삼각형마다 광선과 맞추고(`CharacterGeometryUtil::intersectRayTriangle`, 양면)
 *          모든 표면 중 가장 가까운 맞음이 답이다. 언리얼의 스켈레탈 메시 라인 트레이스(스키닝된 정점 · 피직스 에셋) · 섹션 클릭 선택 · 데칼 배치와 같은 자리다.
 *          shortcut: 모프 타깃 가중치(얼굴 표정)는 넣지 않는다 — 체형 · 피팅은 메시에 베이크돼 있어 몸 자리에는 닿지 않는다, 표정 위 집기가 필요해지면 모프를 더한다.
 *          ImGui 와 씬을 모릅니다 — 표면은 창이 오브젝트에서 모아 짓는다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct MeshSection;
    struct MeshSkinVertex;
    struct RHIVertex;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 집을 표면 하나입니다 — 오브젝트 하나의 메시를 지금 포즈로 월드에 옮긴 정점과, 메시의 인덱스 · UV · 구간을 보는 칸입니다.
     * @details 보는 칸은 메시가 가진 목록이다 — 표면은 지은 프레임 안에서만 쓴다(메시가 바뀌면 다시 짓는다).
     */
    struct BodyPickSurface
    {
        vector<float3>                      _listPosition{}; ///< 정점마다 지금 포즈의 월드 자리(`_listVertex` 와 같은 순서)
        vector_reference<const RHIVertex>   _listVertex{};   ///< 메시 정점(UV 를 읽는다)
        vector_reference<const uint32>      _listIndex{};
        vector_reference<const MeshSection> _listSection{};
        uint8                               _bSkin{ SW_FALSE }; ///< 피부(몸) 표면이다 — 문신 · 피부 층의 UV 가 이 표면의 것이다

        /**
         * @brief 메시 정점 @p listVertex 를 지금 포즈로 월드에 옮겨 표면을 짓습니다.
         * @param listSkin 정점마다 본 영향입니다. 비었거나 @p listPalette 가 비면 스키닝하지 않는다(단단한 부품).
         * @param listPalette 유닛의 스킨 팔레트입니다(`SkeletalMeshComponent::getSkinPalette` — 역 바인드 × 모델 공간).
         * @param world 오브젝트의 월드 행렬입니다(스킨 결과는 유닛 모델 공간이다).
         */
        static void build( vector_reference<const RHIVertex> listVertex, vector_reference<const MeshSkinVertex> listSkin, const vector<float4x4>& listPalette,
                           const float4x4& world, BodyPickSurface& outSurface );
    };

    /** @brief 표면 집기의 결과입니다. */
    struct BodySurfaceHit
    {
        float3  _position{};                                   ///< 맞은 월드 자리
        float2  _uv{};                                         ///< 맞은 자리의 UV(무게중심 보간)
        float32 _distance{ 0.0f };                             ///< 광선 시작에서의 거리
        uint32  _surface{ constant::invalid_index::kUint32 };  ///< 맞은 표면 번호
        uint32  _section{ constant::invalid_index::kUint32 };  ///< 맞은 삼각형이 든 구간 번호(구간이 없으면 무효)
        uint32  _triangle{ constant::invalid_index::kUint32 }; ///< 맞은 삼각형 번호

        /** @brief 광선(@p origin, 단위 방향 @p direction)이 표면들과 처음 맞는 자리를 찾습니다. 맞지 않으면 false 입니다. */
        [[nodiscard]] static bool pick( vector_reference<const BodyPickSurface> listSurface, const float3& origin, const float3& direction, BodySurfaceHit& outHit );
        /** @brief 삼각형 @p triangle(인덱스 칸 triangle × 3)이 든 구간 번호입니다. 없으면 무효입니다. */
        static uint32 findSection( vector_reference<const MeshSection> listSection, uint32 triangle );
    };
} // namespace sw::editor
