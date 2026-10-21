// 식생 레이어 중 길을 막는 것(나무 줄기 · 바위 — `FoliageLayer::_navBlockRadius` 가 0 보다 크다)을 내비메시 베이크 기하로 냅니다.
// 식생은 메시 컴포넌트 · 강체가 아니라 인스턴스 배치로 그리므로 기하 모으기가 스스로는 모른다(지형 높이장과 같은 자리 — TerrainNavGeometry.cpp).
// 인스턴스마다 걷지 않는 상자(반지름 × 높이, 인스턴스 배율 · 요)를 뚫는다. 배치는 결정적이라(같은 규칙 · 시드 · 지형이면 같은 인스턴스) 쿠킹과 런타임 베이크가 같은 구멍을 낸다.
// 언리얼 Foliage 의 `bCanEverAffectNavigation` · 유니티 Terrain 나무의 NavMeshObstacle 에 해당한다. 풀(반지름 0)은 넣지 않는다.
#include "pch.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Environment/Placement/PlacementRule.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Object/Component/Navigation/NavGeometryContributor.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        void contributeFoliage( const Component& component, uint8 area, NavMeshGeometry& outGeometry )
        {
            (void)area; // 막는 식생은 늘 걷지 않는 영역이다 — 표면(지형)의 영역을 따르지 않는다
            const FoliageComponent&   foliage = static_cast<const FoliageComponent&>( component );
            vector<PlacementInstance> listInstance;
            for ( uint32 layerIndex = 0; layerIndex < foliage.getLayerCount(); ++layerIndex )
            {
                const FoliageLayer& layer = foliage.getLayer( layerIndex );
                if ( layer._navBlockRadius <= 0.0f )
                    continue;
                if ( foliage.computeLayerPlacements( layerIndex, listInstance ) == false )
                    continue;
                const float3 halfExtents{ layer._navBlockRadius, layer._navBlockHeight * 0.5f, layer._navBlockRadius };
                for ( const PlacementInstance& instance : listInstance )
                {
                    // 상자 중심은 뿌리 위 반 높이(배율을 곱한 것)다. 배치의 월드 원점은 0 이다(`FoliageComponent` 가 배치를 만들 때와 같다).
                    const float32  centerHeight = instance._height + halfExtents._y * instance._scale;
                    const float3   center{ instance._planePosition._x, centerHeight, instance._planePosition._y };
                    const float4x4 world = float4x4::makeTrs( center, float3{ 0.0f, instance._yaw, 0.0f }, float3{ instance._scale, instance._scale, instance._scale } );
                    outGeometry.addBox( halfExtents, world, NavigationConstant::kNotWalkableArea );
                }
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_NAV_GEOMETRY_CONTRIBUTOR( FoliageComponent, &contributeFoliage )
} // namespace sw
