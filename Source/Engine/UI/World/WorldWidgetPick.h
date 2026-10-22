/**
 * @file WorldWidgetPick.h
 * @brief 화면 점에서 쏜 광선이 World 위젯 사각형의 어느 위젯 좌표를 맞히는지 계산합니다(언리얼 `WidgetInteractionComponent` 의 광선 → 위젯 좌표 부분).
 * @details 순수 계산이다. 사각형은 단위 스프라이트 사각형(`MeshUtil::createSpriteQuad`: 로컬 XY 평면, ±0.5, 앞면은 −Z 를 향함, 양면)에 월드 행렬을 곱한 것이고,
 *          위젯 좌표는 그 사각형에 붙인 렌더 텍스처의 픽셀(= UI 단위)이다. 앞면은 u 가 +X 로, 뒷면은 −X 로 늘고 v 는 위가 0 이다(메시의 UV 와 같은 규약).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 월드 광선입니다(방향은 길이 1). */
    struct WorldWidgetRay
    {
        float3 _origin{};
        float3 _direction{ 0.0f, 0.0f, 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 광선이 사각형을 맞힌 자리입니다. */
    struct WorldWidgetHit
    {
        float2  _point{};                 ///< 위젯 좌표(렌더 텍스처 픽셀, 왼쪽 위가 원점)입니다. 사각형 밖(`bRequireInside` 가 false)이면 텍스처 밖 값도 갖는다.
        float32 _distance{ 0.0f };        ///< 광선 출발점에서 사각형 평면까지의 거리(m)입니다.
        uint8   _bBackFace{ SW_FALSE };   ///< 뒷면(+Z 를 향한 면)을 맞혔는지 나타냅니다.
        uint8   _bInsideQuad{ SW_FALSE }; ///< 맞힌 자리가 사각형 안인지 나타냅니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WorldWidgetPick
     * @brief 광선 만들기와 사각형 맞히기입니다.
     */
    struct SW_API WorldWidgetPick
    {
        /**
         * @brief UI 점 @p uiPoint(뷰포트 @p viewportSize 안, 왼쪽 위 원점)를 지나는 광선을 만듭니다. 깊이 범위는 0..1 입니다(근평면 → 원평면).
         * @param inverseViewProjection 게임 카메라의 (뷰 × 투영) 역행렬입니다.
         * @return 뷰포트가 비었거나 역투영이 퇴화하면 false 입니다.
         */
        [[nodiscard]] static bool makeRay( const float4x4& inverseViewProjection, const float2& uiPoint, const float2& viewportSize, WorldWidgetRay& outRay );
        /**
         * @brief 광선이 사각형(@p quadWorld = 단위 사각형 → 월드)의 평면을 맞히는 자리를 위젯 좌표로 돌려줍니다.
         * @param textureSize     사각형에 붙인 렌더 텍스처 크기(UI 단위)입니다.
         * @param bRequireInside  true 면 사각형 밖을 맞힌 것은 false 입니다. 포인터를 잡은 위젯(끌기)이 사각형 밖으로 벗어난 점을 따라갈 때는 false 로 평면 점을 받습니다.
         * @return 광선이 평면과 평행이거나, 평면이 출발점 뒤에 있거나, 행렬이 퇴화했거나, `bRequireInside` 인데 사각형 밖이면 false 입니다.
         */
        [[nodiscard]] static bool intersectQuad( const float4x4& quadWorld, const WorldWidgetRay& ray, const float2& textureSize, bool bRequireInside, WorldWidgetHit& outHit );
    };
} // namespace sw
