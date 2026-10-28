/**
 * @file ModifierGizmoLogic.h
 * @brief 모디파이어 뷰포트 편집(기즈모 끌기 · 제어점 고르기 · 범위 시각화)의 순수 계산입니다. ImGui · 씬 · 에디터를 모릅니다.
 * @details 시각화(`MeshModifiersVisualizer.cpp`)가 마우스 레이를 만들어 넘기고, 여기서는 레이와 선분 · 점 · 평면의 거리와 교점,
 *          끌기 이동량, 범위 평면 사각형 같은 값만 계산한다. 그래서 단위 테스트(`TestModifierGizmoLogic.cpp`)가 그대로 부를 수 있다.
 *          레이 방향은 정규화돼 있다고 가정한다. 모든 좌표는 같은 공간(보통 월드)이어야 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    /** @brief 매개변수 하나가 뷰포트에서 무엇으로 그려지는지입니다. */
    enum class ModifierRangeKind : uint8
    {
        None,
        Radius,     ///< 중심 둘레 구(Float)
        LowerLimit, ///< 주축 위 비율 하한 평면(Float, [0,1])
        UpperLimit, ///< 주축 위 비율 상한 평면(Float, [0,1])
        Center,     ///< 모디파이어 공간 점(Vector3)
        Direction,  ///< 주축 둘레 각도(Float, 도) 또는 방향 벡터(Vector3)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 모디파이어 뷰포트 편집의 순수 계산 모음입니다. */
    struct ModifierGizmoLogic
    {
        /**
         * @brief 레이와 가장 가까운 무한 직선 위 점의 매개변수 t 를 구합니다(점 = @p lineOrigin + t · @p lineDirection).
         * @details @p lineDirection 은 정규화하지 않아도 된다(t 는 그 길이 단위). 둘이 평행하면 0 입니다.
         */
        static float32 computeClosestLineParameter( const float3& rayOrigin, const float3& rayDirection, const float3& lineOrigin, const float3& lineDirection );
        /** @brief 레이(반직선)와 선분 사이의 최단 거리입니다. */
        static float32 computeRaySegmentDistance( const float3& rayOrigin, const float3& rayDirection, const float3& segmentA, const float3& segmentB );
        /** @brief 레이(반직선)와 점 사이의 거리입니다. 점이 레이 뒤에 있으면 원점까지의 거리입니다. */
        static float32 computeRayPointDistance( const float3& rayOrigin, const float3& rayDirection, const float3& point );
        /** @brief 레이와 평면의 교점을 구합니다. 평행하거나 교점이 레이 뒤면 false 입니다. */
        [[nodiscard]] static bool tryIntersectRayPlane( const float3& rayOrigin, const float3& rayDirection, const float3& planePoint, const float3& planeNormal,
                                                        float3& outPoint );
        /**
         * @brief 끌기 평면 위에서 시작 레이 교점부터 지금 레이 교점까지의 이동량을 구합니다. 어느 레이든 평면에 닿지 않으면 false 입니다.
         * @details 자유 이동(카메라를 마주 보는 평면)에 쓴다. 평면 법선은 끌기 시작 때의 레이 방향을 넘긴다.
         */
        [[nodiscard]] static bool tryComputePlaneDragDelta( const float3& startRayOrigin, const float3& startRayDirection, const float3& rayOrigin,
                                                            const float3& rayDirection, const float3& planePoint, const float3& planeNormal, float3& outDelta );
        /**
         * @brief 레이가 반경 @p radius 의 구로 맞히는 점 중 레이 원점에 가장 가까운 점의 번호를 찾습니다. 없으면 -1 입니다.
         */
        static int32 findPointOnRay( const float3& rayOrigin, const float3& rayDirection, const vector<float3>& listPoint, float32 radius );
        /** @brief @p axis 둘레로 @p from 에서 @p to 까지 돈 각도(도, -180 ~ 180, 오른손 방향이 양)입니다. 축에 수직인 성분만 봅니다. */
        static float32 computeSignedAngleDegree( const float3& from, const float3& to, const float3& axis );
        /**
         * @brief 주축 위 비율 @p fraction(상자 아래 0 · 위 1)에 놓인, 주축에 수직인 상자 단면 사각형의 네 모서리를 채웁니다(둘레 순서).
         * @details 상자가 무효면 아무것도 채우지 않고 false 입니다. 비율은 자르지 않는다(모디파이어의 `getAxisFraction` 과 같은 뜻).
         */
        [[nodiscard]] static bool tryMakeLimitQuad( const MeshModifierBounds& bounds, uint32 axisIndex, float32 fraction, vector<float3>& outListCorner );
        /** @brief 주축 @p axisIndex 에 수직인 평면에서 각도 @p angleDegree 의 단위 방향입니다. 0 도는 다음 축(X → Y → Z → X) 방향입니다. */
        static float3 computeDirectionVector( uint32 axisIndex, float32 angleDegree );
        /** @brief 2 차원 격자(점 번호 = x + countX · y)에서 @p axisIndex 축으로 마주 보는 점 번호입니다. Z 축이나 범위 밖이면 -1 입니다. */
        static int32 findMirrorIndex2D( int32 index, int32 countX, int32 countY, uint32 axisIndex );
        /**
         * @brief 매개변수 정의가 뷰포트에서 무엇으로 그려지는지 정합니다.
         * @details 정의의 `_pHint` 에 낱말(radius · lower limit · upper limit · center · direction, 대소문자 무시)이 있으면 그것이 먼저고,
         *          없으면 이름(`Radius` · `Lower Limit` · `Upper Limit` · `Center` · `Direction`)이 같은지 본다. 종류가 맞지 않으면 None 이다
         *          (Radius · 한계는 Float, Center 는 Vector3, Direction 은 Float 또는 Vector3).
         */
        static ModifierRangeKind computeRangeKind( const MeshModifierParamDef& def );
        /**
         * @brief 기즈모 오일러 각(@p eulerDegree, 기준 = Rz · Ry · Rx · 모디파이어)에 기준 공간 축 @p axisIndex 둘레 증분 회전 @p deltaDegree 를 건 결과의 오일러 각입니다.
         * @details 새 회전 = 축 회전(delta) · 지금 회전이다(회전 고리는 기준 공간 축을 그린다). 결과 행렬을 같은 순서의 오일러 각으로 다시 푼다.
         *          Y 가 ±90 도 근처(짐벌)면 X 를 0 으로 두고 Z 에 몰아 유한한 값을 낸다. 성분 하나만 바꾸던 옛 방식은 다른 성분이 0 일 때만 맞았다.
         */
        static float3 applyRotationDelta( const float3& eulerDegree, uint32 axisIndex, float32 deltaDegree );
    };
} // namespace sw::editor
