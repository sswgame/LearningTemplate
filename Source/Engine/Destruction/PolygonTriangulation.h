/**
 * @file PolygonTriangulation.h
 * @brief 평면 다각형(구멍 포함)을 삼각형으로 나눕니다 — 귀 자르기 + 구멍 다리 놓기입니다. 파쇄의 안쪽 면(캡) · 2D 조각 그리기가 씁니다.
 * @details 고리의 감은 방향을 그대로 지킵니다: 바깥 고리와 같은 방향으로 감긴 삼각형이 나옵니다. 그래서 입력 고리를 메시 앞면 규약대로 감아 넘기면
 *          결과도 그 규약입니다(오목한 단면 · 구멍 난 단면에서 중심 부채꼴이 틀리는 자리를 메운다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 다각형 삼각분할 함수 모음입니다(전부 static). */
    struct SW_API PolygonTriangulationUtil
    {
        /**
         * @brief 고리들(@p listPoint 의 번호 목록)을 삼각형으로 나눠 @p outListIndex 끝에 붙입니다.
         * @details 넓이 절댓값이 가장 큰 고리의 방향이 "바깥" 입니다. 같은 방향의 고리는 바깥, 반대 방향은 구멍이고 구멍은 그것을 품은 가장 작은
         *          바깥 고리에 다리로 이어 붙입니다. 점이 셋 미만이거나 넓이가 0 인 고리는 건너뜁니다.
         * @return 모든 고리를 귀 자르기로 끝냈으면 true 입니다. 막힌 고리(자기 교차)는 남은 부분을 부채꼴로 막고 false 입니다.
         */
        [[nodiscard]] static bool triangulate( vector_reference<const float2> listPoint, const vector<vector<uint32>>& listLoop, vector<uint32>& outListIndex );
        /** @brief 고리의 부호 있는 넓이입니다(반시계가 +). */
        static float32 computeSignedArea( vector_reference<const float2> listPoint, vector_reference<const uint32> listLoopIndex );
        /** @brief 점이 고리 안에 있는지입니다(짝수-홀수 규칙). */
        static bool isPointInLoop( vector_reference<const float2> listPoint, vector_reference<const uint32> listLoopIndex, const float2& point );
    };
} // namespace sw
