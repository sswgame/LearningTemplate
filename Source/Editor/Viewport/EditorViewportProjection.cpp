#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportProjectionInternal
        {
            /** @brief clip 좌표를 캔버스 좌표로 나눕니다. */
            static ImVec2 toScreen( const float4& clip, const float2& canvasPos, const float2& canvasSize )
            {
                const float32 invW = 1.0f / clip._w;
                const float32 x    = clip._x * invW;
                const float32 y    = clip._y * invW;

                ImVec2 screenPt;
                screenPt.x = canvasPos._x + ( x * 0.5f + 0.5f ) * canvasSize._x;
                screenPt.y = canvasPos._y + ( 1.0f - ( y * 0.5f + 0.5f ) ) * canvasSize._y;
                return screenPt;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorViewportProjectionUtil::projectPoint( const float4x4& viewProj, const float3& worldPt,
                                                     const float2& canvasPos, const float2& canvasSize,
                                                     ImVec2& outScreenPt )
    {
        const float4 clip = float4::transform( float4{ worldPt, 1.0f }, viewProj );
        if ( clip._w <= 0.001f )
            return false;

        outScreenPt = EditorViewportProjectionInternal::toScreen( clip, canvasPos, canvasSize );
        return true;
    }

    bool EditorViewportProjectionUtil::projectSegment( const float4x4& viewProj, const float3& worldA,
                                                       const float3& worldB, const float2& canvasPos,
                                                       const float2& canvasSize, ImVec2& outScreenA,
                                                       ImVec2& outScreenB )
    {
        constexpr float32 kNearW = 0.001f;

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

        outScreenA = EditorViewportProjectionInternal::toScreen( clipA, canvasPos, canvasSize );
        outScreenB = EditorViewportProjectionInternal::toScreen( clipB, canvasPos, canvasSize );
        return true;
    }
} // namespace sw::editor
