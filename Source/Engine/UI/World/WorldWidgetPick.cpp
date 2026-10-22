#include "pch.h"

#include "Engine/UI/World/WorldWidgetPick.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct WorldWidgetPickInternal
        {
            /** @brief 사각형의 한 변 절반(로컬)입니다. 단위 사각형이라 ±0.5 다. */
            static constexpr float32 kHalfExtent = 0.5f;
            /** @brief 행렬식이 이보다 작으면 사각형이 퇴화한 것으로 봅니다(스케일 0). */
            static constexpr float32 kMinDeterminant = 1e-7f;
            /** @brief 광선이 평면과 이만큼 평행에 가까우면 맞히지 않은 것으로 봅니다. */
            static constexpr float32 kMinParallel = 1e-8f;

            static bool unproject( const float4x4& inverseViewProjection, float32 ndcX, float32 ndcY, float32 ndcZ, float3& outWorld )
            {
                const float4 world = float4::transform( float4{ ndcX, ndcY, ndcZ, 1.0f }, inverseViewProjection );
                if ( MathUtil::abs( world._w ) < MathUtil::kEpsilon )
                    return false;
                outWorld = float3{ world._x / world._w, world._y / world._w, world._z / world._w };
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool WorldWidgetPick::makeRay( const float4x4& inverseViewProjection, const float2& uiPoint, const float2& viewportSize, WorldWidgetRay& outRay )
    {
        if ( viewportSize._x <= 0.0f || viewportSize._y <= 0.0f )
            return false;
        const float32 ndcX = uiPoint._x / viewportSize._x * 2.0f - 1.0f;
        const float32 ndcY = 1.0f - uiPoint._y / viewportSize._y * 2.0f;
        float3        nearPoint{};
        float3        farPoint{};
        if ( WorldWidgetPickInternal::unproject( inverseViewProjection, ndcX, ndcY, 0.0f, nearPoint ) == false ||
             WorldWidgetPickInternal::unproject( inverseViewProjection, ndcX, ndcY, 1.0f, farPoint ) == false )
            return false;
        const float3  direction = farPoint - nearPoint;
        const float32 length    = direction.getLength();
        if ( length < MathUtil::kEpsilon )
            return false;
        outRay._origin    = nearPoint;
        outRay._direction = direction * ( 1.0f / length );
        return true;
    }

    bool WorldWidgetPick::intersectQuad( const float4x4& quadWorld, const WorldWidgetRay& ray, const float2& textureSize, bool bRequireInside, WorldWidgetHit& outHit )
    {
        using Internal = WorldWidgetPickInternal;
        if ( MathUtil::abs( quadWorld.determinant() ) < Internal::kMinDeterminant )
            return false;
        // 광선을 사각형의 로컬 공간으로 옮긴다. 아핀 변환이라 매개변수 t 가 그대로 월드 거리다(방향은 길이 1).
        const float4x4 inverse        = quadWorld.invert();
        const float3   localOrigin    = float3::transform( ray._origin, inverse );
        const float3   localDirection = float3::transformVector( ray._direction, inverse );
        if ( MathUtil::abs( localDirection._z ) < Internal::kMinParallel )
            return false;
        const float32 distance = -localOrigin._z / localDirection._z;
        if ( distance < 0.0f )
            return false;
        const float32 localX = localOrigin._x + localDirection._x * distance;
        const float32 localY = localOrigin._y + localDirection._y * distance;
        const bool    bInside =
            -Internal::kHalfExtent <= localX && localX <= Internal::kHalfExtent && -Internal::kHalfExtent <= localY && localY <= Internal::kHalfExtent;
        if ( bRequireInside && bInside == false )
            return false;
        // 로컬 +Z 로 나아가는 광선은 −Z 를 향한 앞면을 본다.
        const bool    bBackFace = localDirection._z < 0.0f;
        const float32 u         = bBackFace ? Internal::kHalfExtent - localX : localX + Internal::kHalfExtent;
        const float32 v         = Internal::kHalfExtent - localY;
        outHit._point           = float2{ u * textureSize._x, v * textureSize._y };
        outHit._distance        = distance;
        outHit._bBackFace       = bBackFace ? SW_TRUE : SW_FALSE;
        outHit._bInsideQuad     = bInside ? SW_TRUE : SW_FALSE;
        return true;
    }
} // namespace sw
