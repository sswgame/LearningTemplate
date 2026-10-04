/**
 * @file NavMeshObstacleComponent.h
 * @brief 내비메시를 뚫는 움직이는 장애물 — 유니티 `NavMeshObstacle`(Carve) · 언리얼 동적 장애물(타일 재생성)의 자리입니다.
 * @details 오브젝트 자세의 상자(또는 원기둥) 발자국을 몸 반지름만큼 넓혀 뚫습니다. 생기거나 · 사라지거나 · `NavMeshSettings::_obstacleMoveThreshold` 보다
 *          멀리 움직이거나 돌면 옛 · 새 자리의 타일만 워커에서 다시 베이크하고, 다음 갱신에 바꿔 끼웁니다(그 사이 에이전트는 옛 타일 위를 걷는다).
 *          정적 기하가 아니므로 쿠킹본에는 들지 않습니다 — 쿠킹본을 읽은 뒤 장애물 자리의 타일을 다시 베이크합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 장애물 발자국의 모양입니다. */
    ENUM()
    enum class NavObstacleShape : uint8
    {
        Box = 0,  ///< `_halfExtents` 상자(오브젝트 회전을 따른다)
        Cylinder, ///< 반지름 `_halfExtents.x`, 반 높이 `_halfExtents.y` 의 세운 원기둥(팔각형으로 뚫는다)
    };
} // namespace sw

namespace sw
{
    class SceneNavigation;

    /** @class NavMeshObstacleComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Navigation", DisplayName = "NavMesh Obstacle", Tooltip = "Carves a moving hole in the navmesh; only the touched tiles rebake" )
    class SW_API NavMeshObstacleComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavMeshObstacleComponent();
        ~NavMeshObstacleComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 지금 오브젝트 자세의 뚫는 부피입니다(월드). 오브젝트가 없으면 false 입니다. */
        [[nodiscard]] bool makeVolume( NavConvexVolume& outVolume ) const;
        /** @brief 지금 자세의 월드 경계입니다. */
        [[nodiscard]] bool computeWorldBounds( AABB& outBounds ) const;

        NavObstacleShape getShape() const { return _shape; }
        void             setShape( NavObstacleShape shape );
        const float3&    getHalfExtents() const { return _halfExtents; }
        void             setHalfExtents( const float3& halfExtents );
        const float3&    getCenter() const { return _center; }
        void             setCenter( const float3& center );

    private:
        friend class SceneNavigation;

        PROPERTY( Category = "Obstacle", Tooltip = "Footprint shape" )
        NavObstacleShape _shape;
        PROPERTY( Category = "Obstacle", DisplayName = "Half Extents", Tooltip = "Box half size; cylinder uses x as radius and y as half height", Meta = "Units=m" )
        float3 _halfExtents;
        PROPERTY( Category = "Obstacle", Tooltip = "Offset from the object origin", Meta = "Units=m" )
        float3 _center;

        AABB    _carvedBounds; ///< 지금 내비메시에 뚫린 자리(마지막으로 알린 것)
        float3  _carvedPosition;
        float32 _carvedYaw;
        uint32  _navIndex;
        uint8   _bCarved     : 1; ///< 내비메시가 이 장애물을 품고 있다
        uint8   _bShapeDirty : 1; ///< 모양을 바꿨다 — 다음 갱신에 다시 뚫는다
        uint8   _reserved    : 6;
    };
} // namespace sw
