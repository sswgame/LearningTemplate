/**
 * @file PolygonTriangulation.h
 * @brief 평면 다각형(구멍 포함)을 삼각형으로 나눕니다. 귀 자르기와 구멍 다리 놓기를 씁니다. 파쇄의 안쪽 면(캡)과 2D 조각 렌더링이 사용합니다.
 * @details 고리의 감은 방향을 그대로 유지한다. 바깥 고리와 같은 방향으로 감긴 삼각형이 나온다.
 *          그래서 입력 고리를 메시 앞면 규약대로 감아 넘기면 결과도 그 규약을 따른다(오목하거나 구멍 난 단면에서 중심 부채꼴이 틀리는 문제를 해결한다).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 다각형 삼각분할 함수 모음입니다. 전부 static 입니다. */
    struct SW_API PolygonTriangulationUtil
    {
        /**
         * @brief 고리들(@p listPoint 의 인덱스 목록)을 삼각형으로 나눠 @p outListIndex 끝에 붙입니다.
         * @details 넓이 절댓값이 가장 큰 고리의 방향이 "바깥"이다. 같은 방향의 고리는 바깥이고 반대 방향은 구멍이며, 구멍은 그것을 포함한 가장 작은
         *          바깥 고리에 다리로 이어 붙인다. 점이 3 개 미만이거나 넓이가 0 인 고리는 건너뛴다.
         * @return 모든 고리를 귀 자르기로 끝냈으면 true 입니다. 막힌 고리(자기 교차)가 있으면 남은 부분을 부채꼴로 막고 false 를 반환합니다.
         */
        [[nodiscard]] static bool triangulate( vector_reference<const float2> listPoint, const vector<vector<uint32>>& listLoop, vector<uint32>& outListIndex );
        /** @brief 고리의 부호 있는 넓이를 반환합니다(반시계가 +). */
        static float32 computeSignedArea( vector_reference<const float2> listPoint, vector_reference<const uint32> listLoopIndex );
        /** @brief 점이 고리 안에 있는지 반환합니다(짝수-홀수 규칙). */
        static bool isPointInLoop( vector_reference<const float2> listPoint, vector_reference<const uint32> listLoopIndex, const float2& point );
    };
} // namespace sw
