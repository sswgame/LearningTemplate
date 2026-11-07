#include "pch.h"

#include "Engine/Renderer/Lighting/SceneVoxelizer.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Rendering/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct SceneVoxelizerInternal
        {
            /** @brief 이 메시 컴포넌트가 복셀에 들어가는지입니다 — 불투명 · 보이는 · 스킨 없는 메시. */
            static bool isOccluder( const MeshComponent& mesh )
            {
                const Mesh* pRawMesh = mesh.getRawMesh();
                return pRawMesh != nullptr && mesh.isActive() && mesh.isVisible() && mesh.getBlendMode() == RHIBlendMode::Opaque && pRawMesh->hasSkin() == false;
            }

            /** @brief 메시 컴포넌트의 삼각형을 월드로 옮겨 격자에 칠합니다. */
            static void voxelizeMesh( const MeshComponent& component, SceneVoxelGrid& inoutGrid )
            {
                const Mesh&              mesh     = *component.getRawMesh();
                const vector<RHIVertex>& vertices = mesh.getVertices();
                const vector<uint32>&    indices  = mesh.getIndices();
                uint32                   first    = 0;
                uint32                   count    = 0;
                if ( component.findDrawSection( first, count ) == false )
                    return; // 구간 이름이 메시에 없다 — 그리지 않는 것은 가리지도 않는다
                if ( count == 0 )
                    count = static_cast<uint32>( indices.size() ); // 구간 이름이 비면 (0, 0) — 메시 전체
                const uint32 end = first + count;
                if ( end > indices.size() )
                    return; // LOD 인덱스 흐름의 범위 — CPU 는 LOD 0 만 안다
                const float4x4 world = component.getWorldMatrix();
                for ( uint32 cornerIndex = first; cornerIndex + 2u < end; cornerIndex += 3u )
                {
                    const RHIVertex& a = vertices[indices[cornerIndex]];
                    const RHIVertex& b = vertices[indices[cornerIndex + 1u]];
                    const RHIVertex& c = vertices[indices[cornerIndex + 2u]];
                    inoutGrid.voxelizeTriangle( float3::transform( float3{ a._arrPosition[0], a._arrPosition[1], a._arrPosition[2] }, world ),
                                                float3::transform( float3{ b._arrPosition[0], b._arrPosition[1], b._arrPosition[2] }, world ),
                                                float3::transform( float3{ c._arrPosition[0], c._arrPosition[1], c._arrPosition[2] }, world ) );
                }
            }
        };
    } // namespace

    SceneVoxelGrid SceneVoxelizer::makeGrid( const PrimitiveRegistry& primitives, const AABB& bounds, float32 cellSize )
    {
        using Internal      = SceneVoxelizerInternal;
        SceneVoxelGrid grid = SceneVoxelGrid::make( bounds, cellSize );
        if ( grid.isEmpty() )
            return grid;
        const AABB gridBounds = grid.getBounds();
        for ( const ScenePrimitive* pPrimitive : primitives.getAll() )
        {
            // 레지스트리의 실제 타입은 메시 컴포넌트 계열뿐이다(ScenePrimitive.h 의 불변식).
            const MeshComponent* pMesh = static_cast<const MeshComponent*>( pPrimitive );
            if ( pMesh == nullptr || Internal::isOccluder( *pMesh ) == false )
                continue;
            AABB worldBox{};
            if ( pMesh->getWorldBox( worldBox ) == false || worldBox.intersects( gridBounds ) == false )
                continue;
            Internal::voxelizeMesh( *pMesh, grid );
        }
        return grid;
    }
} // namespace sw
