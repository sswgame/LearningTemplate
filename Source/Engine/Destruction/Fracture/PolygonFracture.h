/**
 * @file PolygonFracture.h
 * @brief 2D 보로노이 파쇄입니다. XY 평면의 다각형(반시계, 오목 가능)을 셀마다 반평면으로 잘라 조각 다각형을 만듭니다.
 *        결과는 3D 와 같은 `FractureAsset`(그래프, 묶음 계층, 연결, 조각 렌더링)이라 구조, 피해, 런타임을 그대로 나눠 씁니다.
 * @details 조각 렌더링은 Z = 0 의 평평한 삼각형이다(앞면 -Z 와 뒷면 +Z 두 면이고, UV 는 다각형 경계 상자 기준 [0, 1]이다).
 *          껍질 점은 조각 다각형의 꼭짓점이다(무게 중심 기준, Z = 0). 2D 물리 어댑터가 이 점을 8 점 이하 볼록 다각형으로 줄인다.
 *          부피 필드에는 넓이를, 연결에는 맞닿은 변 길이를 담는다.
 *          시드 배치는 3D 와 같은 설정(`FractureSettings`)을 쓰며, uniform, clustered, slices 는 X 와 Y 축만 쓴다.
 *          쪼개기가 싸서 런타임(오브젝트 시작 시점)에 실행한다.
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

    /** @brief 2D 다각형 쪼개기 함수 모음입니다. 전부 static 입니다. */
    struct SW_API PolygonFractureUtil
    {
        /**
         * @brief 다각형 @p listBorder(반시계, 시계면 뒤집는다)를 쪼개 @p outAsset 을 채웁니다.
         * @return 점이 3 개 미만이거나 넓이가 0 이거나 조각이 남지 않으면 false 를 반환하고 @p outError 에 이유를 씁니다.
         */
        [[nodiscard]] static bool fracture( vector_reference<const float2> listBorder, const FractureSettings& settings, FractureAsset& outAsset, string& outError );
        /** @brief 다각형의 넓이(부호 없음)와 무게 중심을 반환합니다. */
        static float32 computeArea( vector_reference<const float2> listPolygon, float2& outCentroid );
    };
} // namespace sw
