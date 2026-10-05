/**
 * @file PhysicsDebugDraw.h
 * @brief 물리 디버그 그리기의 출구(`IPhysicsDebugRenderer`)와 셰이프 와이어프레임 도우미입니다.
 * @details 물리는 그리는 쪽을 모릅니다(티어가 아래다). 씬이 `drawDebug( renderer )` 로 바디마다 셰이프를 선으로 내고, 렌더러 쪽 어댑터가 그것을
 *          `DebugDrawQueue` 로 옮깁니다(`Engine/Graphics/Debug/PhysicsDebugDrawAdapter.h`, `gv_physicsDebugDraw`). 셰이프를 선으로 푸는
 *          일은 백엔드와 무관해 여기 한 곳에 둡니다 — 두 백엔드가 바디 자세만 넘깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/PhysicsShape.h"

namespace sw
{
    /** @brief 물리 디버그 선을 받는 쪽입니다. 2D 는 Z = 0 평면에 냅니다. */
    class SW_API IPhysicsDebugRenderer
    {
    public:
        IPhysicsDebugRenderer()                                          = default;
        virtual ~IPhysicsDebugRenderer()                                 = default;
        IPhysicsDebugRenderer( const IPhysicsDebugRenderer& )            = delete;
        IPhysicsDebugRenderer& operator=( const IPhysicsDebugRenderer& ) = delete;

        /** @brief 선 하나를 그립니다(월드). */
        virtual void drawLine( const float3& from, const float3& to, const float4& color ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이프를 와이어프레임 선으로 푸는 도우미입니다. */
    struct SW_API PhysicsDebugDrawUtil
    {
        /** @brief 바디 종류 · 상태별 기본 색입니다(정적 회색 · 키네마틱 파랑 · 동적 초록 · 잠든 것 어둡게 · 트리거 노랑). */
        static float4 getBodyColor( uint8 bodyType, bool bSleeping, bool bTrigger );
        /** @brief 3D 셰이프 하나를 바디 자세(@p bodyPosition · @p bodyRotation)에 그립니다. */
        static void drawShape3D( IPhysicsDebugRenderer& renderer, const PhysicsShapeDesc3D& shape, const float3& bodyPosition, const quaternion& bodyRotation,
                                 const float4& color );
        /** @brief 2D 셰이프 하나를 바디 자세(@p bodyPosition · @p bodyAngle)에 그립니다(Z = 0). */
        static void drawShape2D( IPhysicsDebugRenderer& renderer, const PhysicsShapeDesc2D& shape, const float2& bodyPosition, float32 bodyAngle,
                                 const float4& color );
    };
} // namespace sw
