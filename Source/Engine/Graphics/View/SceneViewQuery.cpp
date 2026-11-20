#include "pch.h"

#include "Engine/Graphics/View/SceneViewQuery.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

namespace sw
{
    namespace
    {
        struct SceneViewQueryInternal
        {
            /** @brief 클립 w 가 이보다 작으면 카메라 뒤(또는 눈 평면 위)로 봅니다 — 나누면 좌표가 뒤집히거나 무한대다. */
            static constexpr float32 kMinClipW = 1e-5f;

            /** @brief NDC 점(깊이 @p ndcZ)을 월드로 되돌립니다. 동차 w 가 0 이면 false 입니다. */
            static bool unproject( const float4x4& inverseViewProj, float32 ndcX, float32 ndcY, float32 ndcZ, float3& outWorld )
            {
                const float4 world = float4::transform( float4{ ndcX, ndcY, ndcZ, 1.0f }, inverseViewProj );
                if ( MathUtil::abs( world._w ) < MathUtil::kEpsilon )
                    return false;
                const float32 inverseW = 1.0f / world._w;
                outWorld               = float3{ world._x * inverseW, world._y * inverseW, world._z * inverseW };
                return true;
            }
        };
    } // namespace

    SceneViewQuery SceneViewQuery::make( const float4x4& viewProj, const float3& eyePosition )
    {
        SceneViewQuery query{};
        query._view            = LODView::make( viewProj, eyePosition );
        query._inverseViewProj = viewProj.invert();
        return query;
    }

    bool SceneViewQuery::projectWorldToScreen( const float4x4& viewProj, const float3& world, const float2& screenSize, float2& outScreen )
    {
        const float4 clip = float4::transform( float4{ world._x, world._y, world._z, 1.0f }, viewProj );
        if ( clip._w < SceneViewQueryInternal::kMinClipW )
            return false;
        // NDC(y 위가 +) → 화면(y 아래가 +, 왼쪽 위 원점).
        const float32 inverseW = 1.0f / clip._w;
        outScreen              = float2{ ( clip._x * inverseW * 0.5f + 0.5f ) * screenSize._x, ( 0.5f - clip._y * inverseW * 0.5f ) * screenSize._y };
        return true;
    }

    bool SceneViewQuery::unprojectScreenToRay( const float4x4& inverseViewProj, const float2& screenPoint, const float2& screenSize, Ray& outRay )
    {
        using Internal = SceneViewQueryInternal;
        if ( screenSize._x <= 0.0f || screenSize._y <= 0.0f )
            return false;
        const float32 ndcX = screenPoint._x / screenSize._x * 2.0f - 1.0f;
        const float32 ndcY = 1.0f - screenPoint._y / screenSize._y * 2.0f;
        float3        nearPoint{};
        float3        farPoint{};
        if ( Internal::unproject( inverseViewProj, ndcX, ndcY, 0.0f, nearPoint ) == false || Internal::unproject( inverseViewProj, ndcX, ndcY, 1.0f, farPoint ) == false )
            return false;
        const float3  direction = farPoint - nearPoint;
        const float32 length    = direction.getLength();
        if ( length < MathUtil::kEpsilon )
            return false;
        outRay._origin    = nearPoint;
        outRay._direction = direction * ( 1.0f / length );
        return true;
    }
} // namespace sw
