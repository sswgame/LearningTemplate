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

            /** @brief 클립 좌표를 w 로 나눠 화면 좌표로 옮깁니다(왼쪽 위가 @p screenOrigin). */
            static float2 toScreen( const float4& clip, const float2& screenOrigin, const float2& screenSize )
            {
                const float32 invW = 1.0f / clip._w;
                const float32 x    = clip._x * invW;
                const float32 y    = clip._y * invW;
                return float2{ screenOrigin._x + ( x * 0.5f + 0.5f ) * screenSize._x, screenOrigin._y + ( 1.0f - ( y * 0.5f + 0.5f ) ) * screenSize._y };
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

    bool SceneViewQuery::projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& screenOrigin, const float2& screenSize,
                                         float2& outScreenA, float2& outScreenB )
    {
        float4 clipA = float4::transform( float4{ worldA, 1.0f }, viewProj );
        float4 clipB = float4::transform( float4{ worldB, 1.0f }, viewProj );
        if ( clipA._w <= kSegmentNearW && clipB._w <= kSegmentNearW )
            return false;

        // 한쪽만 뒤에 있으면 w == kSegmentNearW 가 되는 지점까지 당긴다. 동차 좌표는 선형이라 clip 공간에서 바로 보간된다.
        if ( clipA._w <= kSegmentNearW )
        {
            const float32 t = ( kSegmentNearW - clipA._w ) / ( clipB._w - clipA._w );
            clipA           = clipA + ( clipB - clipA ) * t;
        }
        else if ( clipB._w <= kSegmentNearW )
        {
            const float32 t = ( kSegmentNearW - clipB._w ) / ( clipA._w - clipB._w );
            clipB           = clipB + ( clipA - clipB ) * t;
        }

        outScreenA = SceneViewQueryInternal::toScreen( clipA, screenOrigin, screenSize );
        outScreenB = SceneViewQueryInternal::toScreen( clipB, screenOrigin, screenSize );
        return true;
    }

    bool SceneViewQuery::projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& screenSize, float2& outScreenA, float2& outScreenB )
    {
        return projectSegment( viewProj, worldA, worldB, float2{ 0.0f, 0.0f }, screenSize, outScreenA, outScreenB );
    }

    bool SceneViewQuery::projectBoxRect( const float4x4& viewProj, const AABB& box, const float2& screenOrigin, const float2& screenSize, float2& outMin, float2& outMax )
    {
        constexpr uint32 kCornerCount = 8; // 비트 0 · 1 · 2 가 X · Y · Z 의 최소 · 최대
        constexpr uint32 kAxisCount   = 3;
        bool             bAny         = false;
        outMin                        = float2{ MathUtil::kMaxFloat, MathUtil::kMaxFloat };
        outMax                        = float2{ -MathUtil::kMaxFloat, -MathUtil::kMaxFloat };
        for ( uint32 corner = 0; corner < kCornerCount; ++corner )
        {
            for ( uint32 axis = 0; axis < kAxisCount; ++axis )
            {
                const uint32 axisBit = 1u << axis;
                if ( ( corner & axisBit ) != 0 )
                    continue; // 모서리 하나를 그 축의 최소 쪽 끝에서만 센다(열두 개)
                const uint32 other = corner | axisBit;
                const float3 cornerA{ ( corner & 1u ) != 0 ? box._max._x : box._min._x, ( corner & 2u ) != 0 ? box._max._y : box._min._y,
                                      ( corner & 4u ) != 0 ? box._max._z : box._min._z };
                const float3 cornerB{ ( other & 1u ) != 0 ? box._max._x : box._min._x, ( other & 2u ) != 0 ? box._max._y : box._min._y,
                                      ( other & 4u ) != 0 ? box._max._z : box._min._z };
                float2 screenA{};
                float2 screenB{};
                if ( projectSegment( viewProj, cornerA, cornerB, screenOrigin, screenSize, screenA, screenB ) == false )
                    continue;
                outMin = float2{ MathUtil::min( outMin._x, MathUtil::min( screenA._x, screenB._x ) ), MathUtil::min( outMin._y, MathUtil::min( screenA._y, screenB._y ) ) };
                outMax = float2{ MathUtil::max( outMax._x, MathUtil::max( screenA._x, screenB._x ) ), MathUtil::max( outMax._y, MathUtil::max( screenA._y, screenB._y ) ) };
                bAny   = true;
            }
        }
        return bAny;
    }
} // namespace sw
