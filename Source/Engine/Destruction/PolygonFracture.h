/**
 * @file PolygonFracture.h
 * @brief 2D 보로노이 파쇄 — XY 평면의 다각형(반시계, 오목 가능)을 칸마다 반평면으로 잘라 조각 다각형을 냅니다. 결과는 3D 와 같은 `FractureAsset`
 *        (그래프 · 묶음 계층 · 연결 · 조각 그림)이라 구조 · 피해 · 런타임을 그대로 나눠 씁니다.
 * @details 조각 그림은 Z = 0 의 평평한 삼각형(앞 −Z · 뒤 +Z 두 면, UV 는 다각형 경계 상자로 [0, 1])이고, 껍질 점은 조각 다각형의 꼭짓점(무게 중심 기준,
 *          Z = 0 — 2D 물리 창구가 8 점 이하 볼록 다각형으로 줄인다), 부피 칸은 넓이, 연결은 맞닿은 변 길이입니다. 씨앗 배치는 3D 와 같은 설정
 *          (`FractureSettings` — uniform · clustered · slices 는 X · Y 칸)입니다. 쪼개기가 싸서 런타임(오브젝트 시작)에 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct FractureAsset;
    struct FractureSettings;

    /** @brief 2D 다각형 쪼개기 함수 모음입니다(전부 static). */
    struct SW_API PolygonFractureUtil
    {
        /**
         * @brief 다각형 @p listBorder(반시계 — 시계면 뒤집는다)을 쪼개 @p outAsset 을 채웁니다.
         * @return 점이 셋 미만이거나 넓이가 0 이거나 조각이 남지 않으면 false 이고 @p outError 에 까닭입니다.
         */
        [[nodiscard]] static bool fracture( vector_reference<const float2> listBorder, const FractureSettings& settings, FractureAsset& outAsset, string& outError );
        /** @brief 다각형의 넓이(부호 없음)와 무게 중심입니다. */
        static float32 computeArea( vector_reference<const float2> listPolygon, float2& outCentroid );
    };
} // namespace sw
