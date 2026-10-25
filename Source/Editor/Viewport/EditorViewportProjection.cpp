#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief clip 좌표를 w 로 나눠 캔버스 좌표로 옮깁니다. */
        ImVec2 toScreen( const float4& clip, const float2& canvasPos, const float2& canvasSize )
        {
            const float32 invW = 1.0f / clip._w;
            const float32 x    = clip._x * invW;
            const float32 y    = clip._y * invW;

            ImVec2 screenPt;
            screenPt.x = canvasPos._x + ( x * 0.5f + 0.5f ) * canvasSize._x;
            screenPt.y = canvasPos._y + ( 1.0f - ( y * 0.5f + 0.5f ) ) * canvasSize._y;
            return screenPt;
        }
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorViewportProjectionUtil::storeGizmoMatrix( float32* pOutMatrix, const float4x4& matrix )
    {
        Memory::copy( pOutMatrix, &matrix, sizeof( float32 ) * 16 );
    }

    void EditorViewportProjectionUtil::loadGizmoMatrix( float4x4& outMatrix, const float32* pMatrix )
    {
        Memory::copy( &outMatrix, pMatrix, sizeof( float32 ) * 16 );
    }

    void EditorViewportProjectionUtil::loadViewProjection( const float32* pView, const float32* pProj, float4x4& outViewProjection )
    {
        float4x4 viewMat{};
        float4x4 projMat{};
        loadGizmoMatrix( viewMat, pView );
        loadGizmoMatrix( projMat, pProj );
        outViewProjection = viewMat * projMat;
    }

    bool EditorViewportProjectionUtil::makeMouseRay( const float4x4& invViewProj, const float2& canvasPos, const float2& canvasSize, EditorPickRay& outRay )
    {
        if ( canvasSize._x <= 1.0f || canvasSize._y <= 1.0f )
            return false;

        const ImVec2  mouse               = ImGui::GetMousePos();
        const float32 u                   = ( mouse.x - canvasPos._x ) / canvasSize._x;
        const float32 v                   = ( mouse.y - canvasPos._y ) / canvasSize._y;
        const bool    bMouseOutsideCanvas = u < 0.0f || 1.0f < u || v < 0.0f || 1.0f < v;
        if ( bMouseOutsideCanvas )
            return false;
        return EditorViewportRayPick::makeRay( invViewProj, u, v, outRay );
    }

    bool EditorViewportProjectionUtil::projectPoint( const float4x4& viewProj, const float3& worldPt,
                                                     const float2& canvasPos, const float2& canvasSize,
                                                     ImVec2& outScreenPt )
    {
        const float4 clip = float4::transform( float4{ worldPt, 1.0f }, viewProj );
        if ( clip._w <= 0.001f )
            return false;

        outScreenPt = toScreen( clip, canvasPos, canvasSize );
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

        outScreenA = toScreen( clipA, canvasPos, canvasSize );
        outScreenB = toScreen( clipB, canvasPos, canvasSize );
        return true;
    }
} // namespace sw::editor
