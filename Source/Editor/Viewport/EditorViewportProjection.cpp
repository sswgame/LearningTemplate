#include "pch.h"

#include "Editor/Viewport/EditorViewportProjection.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Viewport/Model/EditorCanvasProjection.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include <imgui.h>

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

    bool EditorViewportProjectionUtil::makeMouseRay( const float4x4& invViewProj, const float2& canvasPos, const float2& canvasSize, Ray& outRay )
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

    bool EditorViewportProjectionUtil::projectPoint( const float4x4& viewProj, const float3& worldPt, const float2& canvasPos, const float2& canvasSize, ImVec2& outScreenPt )
    {
        float2 screen{};
        if ( EditorCanvasProjection::projectPoint( viewProj, worldPt, canvasPos, canvasSize, screen ) == false )
            return false;
        outScreenPt = ImVec2{ screen._x, screen._y };
        return true;
    }

    bool EditorViewportProjectionUtil::projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& canvasPos, const float2& canvasSize,
                                                       ImVec2& outScreenA, ImVec2& outScreenB )
    {
        float2 screenA{};
        float2 screenB{};
        if ( EditorCanvasProjection::projectSegment( viewProj, worldA, worldB, canvasPos, canvasSize, screenA, screenB ) == false )
            return false;
        outScreenA = ImVec2{ screenA._x, screenA._y };
        outScreenB = ImVec2{ screenB._x, screenB._y };
        return true;
    }
} // namespace sw::editor
