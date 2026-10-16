// 지형 높이장을 내비메시 베이크 기하로 냅니다 — 지형은 메시 컴포넌트 · 강체가 아니라 인스턴스 배치로 그리므로 기하 모으기가 스스로는 모른다.
// 언리얼 `ALandscape` 의 내비 기하(높이장 그대로) · 유니티 NavMeshSurface 가 Terrain 을 높이장으로 모으는 것과 같다. 구멍 셀은 걷지 않는다.
#include "pch.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/Component/Navigation/NavGeometryContributor.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        void contribute( const Component& component, uint8 area, NavMeshGeometry& outGeometry )
        {
            const TerrainHeightfield& heightfield = static_cast<const TerrainComponent&>( component ).getHeightfield();
            if ( heightfield.isValid() == false )
                return;
            const vector<uint8>& listHoleCell = heightfield.getHoleCells();
            outGeometry.addHeightfield( heightfield.getHeightSamples().data(), heightfield.getResolution(), heightfield.getOrigin(), heightfield.getCellSize(),
                                        listHoleCell.empty() ? nullptr : listHoleCell.data(), area );
        }
    } // namespace
} // namespace sw

namespace sw
{
    SW_NAV_GEOMETRY_CONTRIBUTOR( TerrainComponent, &contribute )
} // namespace sw
