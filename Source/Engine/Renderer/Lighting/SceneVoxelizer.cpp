#include "pch.h"

#include "Engine/Renderer/Lighting/SceneVoxelizer.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/Component/MeshComponent.h"

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

            /**
             * @brief 메시 머티리얼의 자체 발광 복사휘도입니다 — 프로퍼티 `_emissiveColor`(rgb) × `_emissiveStrength`(있으면). `_emissiveColor` 가 없으면 발광하지 않는 머티리얼이다(0).
             * @details 머티리얼 셰이더의 발광 훅(`computeMaterialEmissive`)은 GPU 코드라 CPU 가 돌릴 수 없다 — 같은 이름의 프로퍼티를 읽는다(텍스처 발광은 색 값만 본다).
             *          값을 읽지 못하면(글이 수 넷이 아님) 오류를 남기고 0 이다 — 다른 값으로 짐작하지 않는다.
             */
            static float3 findEmission( const MeshComponent& component )
            {
                const Material* pMaterial = component.getMaterial();
                if ( pMaterial == nullptr )
                    return float3{};
                const MaterialProperty* pColor = pMaterial->findProperty( hashed_string( "_emissiveColor" ) );
                if ( pColor == nullptr )
                    return float3{};
                float32 arrColor[4]{};
                if ( StringUtil::parseExactFloats( pColor->_value, " ,", arrColor, 4 ) == false )
                {
                    SW_LOG_ERROR_ONCE_ID( "SceneVoxelizer.EmissiveColorUnreadable", "Material property _emissiveColor '%#' is not four numbers - the mesh does not emit into realtime probes", pColor->_value );
                    return float3{};
                }
                float32                 strength  = 1.0f;
                const MaterialProperty* pStrength = pMaterial->findProperty( hashed_string( "_emissiveStrength" ) );
                if ( pStrength != nullptr && StringUtil::parseFloat( pStrength->_value, strength ) == false )
                {
                    SW_LOG_ERROR_ONCE_ID( "SceneVoxelizer.EmissiveStrengthUnreadable", "Material property _emissiveStrength '%#' is not a number - the mesh does not emit into realtime probes", pStrength->_value );
                    return float3{};
                }
                return float3{ arrColor[0], arrColor[1], arrColor[2] } * strength;
            }

            /** @brief 메시 컴포넌트의 삼각형을 월드로 옮겨 격자에 칠합니다. 발광 머티리얼이면 칸의 발광도 둡니다. */
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
                const float4x4 world     = component.getWorldMatrix();
                const float3   emission  = findEmission( component );
                const bool     bEmissive = emission._x > 0.0f || emission._y > 0.0f || emission._z > 0.0f;
                for ( uint32 cornerIndex = first; cornerIndex + 2u < end; cornerIndex += 3u )
                {
                    const RHIVertex& a      = vertices[indices[cornerIndex]];
                    const RHIVertex& b      = vertices[indices[cornerIndex + 1u]];
                    const RHIVertex& c      = vertices[indices[cornerIndex + 2u]];
                    const float3     worldA = float3::transform( float3{ a._arrPosition[0], a._arrPosition[1], a._arrPosition[2] }, world );
                    const float3     worldB = float3::transform( float3{ b._arrPosition[0], b._arrPosition[1], b._arrPosition[2] }, world );
                    const float3     worldC = float3::transform( float3{ c._arrPosition[0], c._arrPosition[1], c._arrPosition[2] }, world );
                    if ( bEmissive )
                        inoutGrid.voxelizeEmissiveTriangle( worldA, worldB, worldC, emission );
                    else
                        inoutGrid.voxelizeTriangle( worldA, worldB, worldC );
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
