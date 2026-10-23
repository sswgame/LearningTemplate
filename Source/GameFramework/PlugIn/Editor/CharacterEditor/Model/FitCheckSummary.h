/**
 * @file FitCheckSummary.h
 * @brief 피팅 결과(`FitResult`)를 창에 보일 표로 요약합니다 — 부품별 잘린 면 비율 · 조임 강도, 관통 점 수 · 깊이. 보기 전용이며 ImGui 와 씬을 모릅니다.
 * @details 피팅은 몸 메시와 부품 형상(`AppearanceGeometry`)이 있어야 풀 수 있다. 이 요약은 푼 결과를 받아 읽기만 하므로, 풀어서 넘기는 일(슬라이더를 놓을 때 한 번 계산)은 미리보기 씬이 붙은 뒤의 일이다.
 *          보정 조각 작성은 이 창의 범위 밖이다(설계 문서 "결정과 위험").
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct FitResult;
} // namespace sw

namespace sw::editor
{
    /** @brief 부품 하나의 요약입니다. */
    struct FitPartSummary
    {
        hashed_string _name{};
        uint32        _triangleCount{ 0 };
        uint32        _cutTriangleCount{ 0 };
        uint32        _movedVertexCount{ 0 }; ///< 델타가 0 이 아닌 정점 수
        float32       _maxDelta{ 0.0f };      ///< 가장 크게 움직인 정점의 거리(조임 강도의 상한)
        float32       _cutRatio{ 0.0f };      ///< 잘린 면 / 전체 면

        bool isCut() const { return _cutTriangleCount > 0; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 단단함끼리의 관통 하나입니다. 부품 이름으로 풀어 둔다. */
    struct FitPenetrationSummary
    {
        hashed_string  _partA{};
        hashed_string  _partB{};
        uint32         _vertexCount{ 0 };
        float32        _maxDepth{ 0.0f };
        vector<float3> _listPoint{}; ///< 파고든 정점의 바인드 공간 위치(앞쪽 일부) — 뷰포트에 점으로 찍는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 피팅 요약 전체입니다. */
    struct FitCheckSummary
    {
        vector<FitPartSummary>        _listPart{};
        vector<FitPenetrationSummary> _listPenetration{};
        vector<string>                _listMessage{};
        uint32                        _penetrationVertexCount{ 0 }; ///< 모든 관통의 점 수 합
        float32                       _maxPenetrationDepth{ 0.0f };

        /** @brief 관통이 하나라도 있으면 true 입니다. */
        bool hasPenetration() const { return _listPenetration.empty() == false; }

        /** @brief 푼 결과를 요약합니다. 관통의 부품 번호가 범위 밖이면 빈 이름으로 둡니다. */
        static FitCheckSummary fromResult( const FitResult& result );
    };
} // namespace sw::editor
