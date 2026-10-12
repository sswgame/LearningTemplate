/**
 * @file PhysicsDebugDraw.h
 * @brief 물리 디버그 그리기의 출구(`IPhysicsDebugRenderer`)와 셰이프 와이어프레임 도우미입니다.
 * @details 물리는 그리는 쪽을 모른다(티어가 아래다). 씬이 `drawDebug( renderer )` 로 바디마다 셰이프를 선으로 내보내고, 조립 지점의 어댑터가 그것을
 *          `DebugDrawQueue` 로 옮긴다(`SceneDebugDraw.cpp` 의 `SceneDebugDrawLineAdapter`, `gv_physicsDebugDraw`). 셰이프를 선으로 푸는
 *          일은 백엔드와 무관하므로 여기 한 곳에 둔다. 두 백엔드는 바디 자세만 넘긴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Physics/Collision/PhysicsShape.h"

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
    /** @brief 디버그 선 하나입니다(월드). */
    struct PhysicsDebugLine
    {
        float3 _from{};
        float3 _to{};
        float4 _color{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 받은 선을 목록에 모으는 출구입니다. 셰이프 시각화(에디터 기즈모, 테스트)가 선을 직접 그리지 않고 목록으로 받을 때 씁니다.
     * @details `PhysicsDebugDrawUtil::drawShape3D`(시뮬레이션 없이 서술자와 자세만으로)나 `IPhysicsBodyScene::drawBodyDebug`(살아 있는 바디)에 넘긴다.
     *          `clear` 는 용량을 남기므로 프레임마다 다시 써도 할당하지 않는다. 언리얼의 `FKAggregateGeom` 선 그리기, 유니티의 `OnDrawGizmos` 콜라이더 선에 해당한다.
     */
    class SW_API PhysicsDebugLineCollector final : public IPhysicsDebugRenderer
    {
    public:
        PhysicsDebugLineCollector()           = default;
        ~PhysicsDebugLineCollector() override = default;

        void drawLine( const float3& from, const float3& to, const float4& color ) override { _listLine.push_back( PhysicsDebugLine{ from, to, color } ); }

        /** @brief 모은 선입니다. */
        const vector<PhysicsDebugLine>& getLines() const { return _listLine; }
        /** @brief 선을 비웁니다(용량은 남긴다). */
        void clear() { _listLine.clear(); }

    private:
        vector<PhysicsDebugLine> _listLine;
    };
} // namespace sw

namespace sw
{
    /** @brief 셰이프를 와이어프레임 선으로 푸는 도우미입니다. */
    struct SW_API PhysicsDebugDrawUtil
    {
        /** @brief 바디 종류와 상태별 기본 색입니다(정적은 회색, 키네마틱은 파랑, 동적은 초록, 잠든 것은 어둡게, 트리거는 노랑). */
        static float4 getBodyColor( uint8 bodyType, bool bSleeping, bool bTrigger );
        /** @brief 3D 셰이프 하나를 바디 자세(@p bodyPosition, @p bodyRotation)에 그립니다. */
        static void drawShape3D( IPhysicsDebugRenderer& renderer, const PhysicsShapeDesc3D& shape, const float3& bodyPosition, const quaternion& bodyRotation,
                                 const float4& color );
        /** @brief 2D 셰이프 하나를 바디 자세(@p bodyPosition, @p bodyAngle)에 그립니다(Z = 0). */
        static void drawShape2D( IPhysicsDebugRenderer& renderer, const PhysicsShapeDesc2D& shape, const float2& bodyPosition, float32 bodyAngle,
                                 const float4& color );
    };
} // namespace sw
