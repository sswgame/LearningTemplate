#include "pch.h"

#include "Editor/Viewport/Model/EditorCanvasProjection.h"

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

    bool EditorCanvasProjection::isInsideCanvas( const float2& point, const float2& canvasPos, const float2& canvasSize )
    {
        return point._x >= canvasPos._x && point._y >= canvasPos._y && point._x <= canvasPos._x + canvasSize._x && point._y <= canvasPos._y + canvasSize._y;
    }
} // namespace sw::editor
