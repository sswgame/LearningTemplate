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
#include "Core/Delegate/Subscription.h"
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
    class SceneComponent;
    class SceneNavigation;

    /** @class NavMeshObstacleComponent @brief 파일 머리말 참고. */
    REFLECT( Category = "Navigation", DisplayName = "NavMesh Obstacle", Tooltip = "Carves a moving hole in the navmesh; only the touched tiles rebake", Meta = "Icon=Navigation, IconColor=Navigation" )
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
        /** @brief 지금 오브젝트 자세의 장애물 캐시 장애물입니다(월드, `NavObstacleMode::TileCache`). 오브젝트가 없으면 false 입니다. */
        [[nodiscard]] bool makeCacheObstacle( NavCacheObstacleDesc& outDesc ) const;
        /** @brief 지금 자세의 월드 경계입니다. */
        [[nodiscard]] bool computeWorldBounds( AABB& outBounds ) const;
        /**
         * @brief 월드 행렬 @p world 의 +X 축이 XZ 평면에서 향하는 각입니다 — 장애물 캐시 상자의 요(`NavCacheObstacleDesc::_yaw`, Detour `addBoxObstacle` 의 yRadians).
         * @details Detour 는 요 a 인 상자의 +X 축을 월드 (cos a, -sin a) 에 둔다. 그래서 축 (x, z) 의 요는 atan2( -z, x ) 다(`NavMeshTileCacheTest.RotatedBoxFollowsObjectAxis` 가 맞춘다).
         */
        static float32 computeBoxYaw( const float4x4& world );

        NavObstacleShape getShape() const { return _shape; }
        void             setShape( NavObstacleShape shape );
        const float3&    getHalfExtents() const { return _halfExtents; }
        void             setHalfExtents( const float3& halfExtents );
        const float3&    getCenter() const { return _center; }
        void             setCenter( const float3& center );

    private:
        friend class NavDebugView;
        friend class NavTileRebaker;
        friend class SceneNavigation;

        PROPERTY( Category = "Obstacle", Tooltip = "Footprint shape" )
        NavObstacleShape _shape;
        PROPERTY( Category = "Obstacle", DisplayName = "Half Extents", Tooltip = "Box half size; cylinder uses x as radius and y as half height", Units = m )
        float3 _halfExtents;
        PROPERTY( Category = "Obstacle", Tooltip = "Offset from the object origin", Units = m )
        float3 _center;

        /**
         * @brief 따르는 트랜스폼(칸의 변경 번호)이나 모양이 바뀌었으면 `_volume` 을 지금 자세로 다시 만들고 true 입니다.
         * @details 그대로면 아무것도 만들지 않고 false 다 — 서 있는 장애물은 프레임마다 행렬 · 부피를 다시 만들지 않는다(`NavTileRebaker::updateObstacles`).
         *          오브젝트나 씬 컴포넌트가 없으면 부피를 비우고 false 다.
         */
        bool refreshVolume();

        NavConvexVolume       _volume;       ///< 마지막으로 만든 지금 자세의 뚫는 부피(버퍼를 다시 쓴다)
        const SceneComponent* _pSeenScene;   ///< `_volume` 을 만든 씬 컴포넌트(비교만 한다 — 번호는 같은 칸끼리만 견준다)
        AABB                  _carvedBounds; ///< 지금 내비메시에 뚫린 자리(마지막으로 알린 것)
        float3                _carvedPosition;
        float32               _carvedYaw;
        uint32                _navIndex;
        Subscription          _navRegistration;      ///< `SceneNavigation` 등록 구독(`onRegister` 가 받고 `onUnregister` 가 놓는다)
        uint32                _seenTransformVersion; ///< `_volume` 을 만든 때의 월드 변경 번호(`SceneComponent::getWorldTransformVersion`)
        uint8                 _bCarved     : 1;      ///< 내비메시가 이 장애물을 품고 있다
        uint8                 _bShapeDirty : 1;      ///< 모양을 바꿨다 — 다음 갱신에 다시 뚫는다
        uint8                 _reserved    : 6;
    };
} // namespace sw
