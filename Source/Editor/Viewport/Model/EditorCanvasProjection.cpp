#include "pch.h"

#include "Editor/Viewport/Model/EditorCanvasProjection.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

namespace sw::editor
{
    namespace
    {
        struct EditorCanvasProjectionInternal
        {
            static constexpr float32 kNearW = 0.001f; ///< 이보다 작은 클립 w 는 카메라 뒤(근평면 밖)다

            /** @brief clip 좌표를 w 로 나눠 캔버스 좌표로 옮깁니다. */
            static float2 toScreen( const float4& clip, const float2& canvasPos, const float2& canvasSize )
            {
                const float32 invW = 1.0f / clip._w;
                const float32 x    = clip._x * invW;
                const float32 y    = clip._y * invW;
                return float2{ canvasPos._x + ( x * 0.5f + 0.5f ) * canvasSize._x, canvasPos._y + ( 1.0f - ( y * 0.5f + 0.5f ) ) * canvasSize._y };
            }
        };
    } // namespace

    bool EditorCanvasProjection::projectPoint( const float4x4& viewProj, const float3& worldPt, const float2& canvasPos, const float2& canvasSize, float2& outScreenPt )
    {
        const float4 clip = float4::transform( float4{ worldPt, 1.0f }, viewProj );
        if ( clip._w <= EditorCanvasProjectionInternal::kNearW )
            return false;
        outScreenPt = EditorCanvasProjectionInternal::toScreen( clip, canvasPos, canvasSize );
        return true;
    }

    bool EditorCanvasProjection::projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& canvasPos, const float2& canvasSize,
                                                 float2& outScreenA, float2& outScreenB )
    {
        constexpr float32 kNearW = EditorCanvasProjectionInternal::kNearW;

        float4 clipA = float4::transform( float4{ worldA, 1.0f }, viewProj );
        float4 clipB = float4::transform( float4{ worldB, 1.0f }, viewProj );
        if ( clipA._w <= kNearW && clipB._w <= kNearW )
            return false;

        // 한쪽만 뒤에 있으면 w == kNearW 가 되는 지점까지 당긴다. 동차 좌표는 선형이라 clip 공간에서 바로 보간된다.
        if ( clipA._w <= kNearW )
        {
            const float32 t = ( kNearW - clipA._w ) / ( clipB._w - clipA._w );
            clipA           = clipA + ( clipB - clipA ) * t;
        }
        else if ( clipB._w <= kNearW )
        {
            const float32 t = ( kNearW - clipB._w ) / ( clipA._w - clipB._w );
            clipB           = clipB + ( clipA - clipB ) * t;
        }

        outScreenA = EditorCanvasProjectionInternal::toScreen( clipA, canvasPos, canvasSize );
        outScreenB = EditorCanvasProjectionInternal::toScreen( clipB, canvasPos, canvasSize );
        return true;
    }

    bool EditorCanvasProjection::projectBoxRect( const float4x4& viewProj, const AABB& box, const float2& canvasPos, const float2& canvasSize, float2& outMin, float2& outMax )
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
                if ( projectSegment( viewProj, cornerA, cornerB, canvasPos, canvasSize, screenA, screenB ) == false )
                    continue;
                outMin = float2{ MathUtil::min( outMin._x, MathUtil::min( screenA._x, screenB._x ) ), MathUtil::min( outMin._y, MathUtil::min( screenA._y, screenB._y ) ) };
                outMax = float2{ MathUtil::max( outMax._x, MathUtil::max( screenA._x, screenB._x ) ), MathUtil::max( outMax._y, MathUtil::max( screenA._y, screenB._y ) ) };
                bAny   = true;
            }
        }
        return bAny;
    }

    bool EditorCanvasProjection::isInsideCanvas( const float2& point, const float2& canvasPos, const float2& canvasSize )
    {
        return point._x >= canvasPos._x && point._y >= canvasPos._y && point._x <= canvasPos._x + canvasSize._x && point._y <= canvasPos._y + canvasSize._y;
    }
} // namespace sw::editor
