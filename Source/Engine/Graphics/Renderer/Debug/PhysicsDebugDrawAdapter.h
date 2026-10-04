/**
 * @file PhysicsDebugDrawAdapter.h
 * @brief 물리 디버그 선(`IPhysicsDebugRenderer`)을 디버그 드로우 큐(`DebugDrawQueue`)로 옮기는 어댑터입니다.
 * @details 물리는 티어가 아래라 그리는 쪽을 모릅니다. `EngineLoop` 가 `gv_physicsDebugDraw` 가 켜졌을 때 활성 씬의 물리를 이것으로 그립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Math/Math.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

namespace sw
{
    /** @class PhysicsDebugDrawAdapter @brief 물리 디버그 선을 큐에 넣습니다. */
    class PhysicsDebugDrawAdapter final : public IPhysicsDebugRenderer
    {
    public:
        explicit PhysicsDebugDrawAdapter( DebugDrawQueue& queue )
            : _queue{ queue }
        {
        }

        void drawLine( const float3& from, const float3& to, const float4& color ) override { _queue.drawLine( from, to, color ); }

    private:
        DebugDrawQueue& _queue;
    };
} // namespace sw
