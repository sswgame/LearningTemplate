/**
 * @file DebugDrawQueue.h
 * @brief CPU 디버그 프리미티브 큐(선 · 구)입니다. GPU 즉시 드로우가 아닙니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /// @brief 한 프레임 디버그 선입니다(시작 · 끝 · 색).
    struct DebugLine
    {
        float3 _from{};
        float3 _to{};
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    /// @brief 한 프레임 디버그 구입니다(중심 · 반지름 · 색).
    struct DebugSphere
    {
        float3  _center{};
        float32 _radius{ 1.0f };
        float4  _color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    /**
     * @class DebugDrawQueue
     * @brief 한 프레임 디버그 지오메트리 큐입니다. 에디터 Game View 등이 ImGui 로 소비합니다.
     */
    class SW_API DebugDrawQueue
    {
    public:
        /** @brief 빈 디버그 드로우 큐로 만듭니다. */
        DebugDrawQueue() = default;

        /** @brief 큐를 비웁니다. */
        void clear();

        /** @brief 선을 예약합니다. */
        void drawLine( const float3& from, const float3& to, const float4& color );
        /** @brief 구를 예약합니다. */
        void drawSphere( const float3& center, float32 radius, const float4& color );

        /** @brief 예약된 선 목록을 반환합니다. */
        const sw::vector<DebugLine>& getLines() const { return _listLine; }
        /** @brief 예약된 구 목록을 반환합니다. */
        const sw::vector<DebugSphere>& getSpheres() const { return _listSphere; }

    private:
        sw::vector<DebugLine>   _listLine;
        sw::vector<DebugSphere> _listSphere;
    };
} // namespace sw
