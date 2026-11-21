#include "pch.h"

#include "Engine/Renderer/Lighting/SceneVoxelizer.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct SceneVoxelizerInternal
        {
            /** @brief 메시가 그려질 머티리얼입니다 — 렌더러와 같은 한 벌(`Material::resolveDrawMaterial`: 자기 · 인스턴스 부모 · 씬 기본). */
            static const Material* resolveMaterial( const MeshComponent& mesh, Material* pSceneDefault )
            {
                return Material::resolveDrawMaterial( mesh.getMaterial(), mesh.getRawMaterialInstance(), pSceneDefault );
            }

            /** @brief 이 메시 컴포넌트가 복셀에 들어가는지입니다 — 불투명(머티리얼이 정한다 — 렌더러의 그리기 목록과 같은 근거) · 보이는 · 스킨 없는 메시. 그릴 머티리얼이 없으면 렌더러도 그리지 않는다. */
            static bool isOccluder( const MeshComponent& mesh, Material* pSceneDefault )
            {
                const Mesh*     pRawMesh  = mesh.getRawMesh();
                const Material* pMaterial = resolveMaterial( mesh, pSceneDefault );
                return pRawMesh != nullptr && pMaterial != nullptr && mesh.isActive() && mesh.isVisible() && pMaterial->getBlendMode() == BlendMode::Opaque &&
                       pRawMesh->hasSkin() == false;
            }

            /**
             * @brief 메시 머티리얼의 자체 발광 복사휘도입니다 — 프로퍼티 `_emissiveColor`(rgb) × `_emissiveStrength`(있으면). `_emissiveColor` 가 없으면 발광하지 않는 머티리얼이다(0).
             * @details 머티리얼 셰이더의 발광 훅(`computeMaterialEmissive`)은 GPU 코드라 CPU 가 돌릴 수 없다 — 같은 이름의 프로퍼티를 읽는다(텍스처 발광은 색 값만 본다).
             *          값을 읽지 못하면(글이 수 넷이 아님) 오류를 남기고 0 이다 — 다른 값으로 짐작하지 않는다.
             */
            static float3 findEmission( const MeshComponent& component, Material* pSceneDefault )
            {
                const Material* pMaterial = resolveMaterial( component, pSceneDefault );
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

            /** @brief 칸 하나가 지금 격자에 칠할 가리개면 그 월드 상자를, 아니면(빈 칸 · 가리개 아님 · 격자 밖) 빈 상자를 돌려줍니다. */
            static AABB findOccluderBox( const PrimitiveComponent* pPrimitive, Material* pSceneDefault, const AABB& gridBounds )
            {
                // 레지스트리의 실제 타입은 메시 컴포넌트 계열뿐이다(PrimitiveComponent.h 의 불변식).
                const MeshComponent* pMesh = static_cast<const MeshComponent*>( pPrimitive );
                if ( pMesh == nullptr || isOccluder( *pMesh, pSceneDefault ) == false )
                    return AABB::empty();
                AABB worldBox{};
                if ( pMesh->getWorldBox( worldBox ) == false || worldBox.intersects( gridBounds ) == false )
                    return AABB::empty();
                return worldBox;
            }

            /** @brief 메시 컴포넌트의 삼각형을 월드로 옮겨 격자의 구간 @p clip 에 칠합니다. 발광 머티리얼이면 칸의 발광도 둡니다. */
            static void voxelizeMesh( const MeshComponent& component, Material* pSceneDefault, const SceneVoxelGrid::CellRange& clip, SceneVoxelGrid& inoutGrid )
            {
                const Mesh&              mesh     = *component.getRawMesh();
                const vector<RHIVertex>& vertices = mesh.getCPUView().getVertices();
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
                const float3   emission  = findEmission( component, pSceneDefault );
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
                        inoutGrid.voxelizeEmissiveTriangle( worldA, worldB, worldC, emission, clip );
                    else
                        inoutGrid.voxelizeTriangle( worldA, worldB, worldC, clip );
                }
            }
        };
    } // namespace

    SceneVoxelGrid SceneVoxelizer::makeGrid( const PrimitiveRegistry& primitives, Material* pSceneDefault, const AABB& bounds, float32 cellSize )
    {
        using Internal      = SceneVoxelizerInternal;
        SceneVoxelGrid grid = SceneVoxelGrid::make( bounds, cellSize );
        if ( grid.isEmpty() )
            return grid;
        const AABB                         gridBounds    = grid.getBounds();
        const SceneVoxelGrid::CellRange    fullRange     = grid.getFullRange();
        const vector<PrimitiveComponent*>& listPrimitive = primitives.getAll();
        // 칸마다 칠한 상자를 남긴다 — 나중에 그 칸만 다시 칠할 때(`updateGrid`) 사라진 메시가 덮던 곳을 안다.
        grid._listSourceBox.assign( listPrimitive.size(), AABB::empty() );
        for ( size_t slot = 0; slot < listPrimitive.size(); ++slot )
        {
            const AABB box = Internal::findOccluderBox( listPrimitive[slot], pSceneDefault, gridBounds );
            if ( box.isValid() == false )
                continue;
            grid._listSourceBox[slot] = box;
            Internal::voxelizeMesh( *static_cast<const MeshComponent*>( listPrimitive[slot] ), pSceneDefault, fullRange, grid );
        }
        return grid;
    }

    bool SceneVoxelizer::updateGrid( SceneVoxelGrid& inoutGrid, const PrimitiveRegistry& primitives, Material* pSceneDefault, const vector<uint32>& listChangedSlot )
    {
        using Internal = SceneVoxelizerInternal;
        if ( inoutGrid.isEmpty() )
            return false;
        const AABB                         gridBounds    = inoutGrid.getBounds();
        const vector<PrimitiveComponent*>& listPrimitive = primitives.getAll();
        // 메시 칸 영역이 늘었으면 상자 목록도 늘린다(늘린 칸은 아직 칠하지 않았다).
        if ( inoutGrid._listSourceBox.size() < listPrimitive.size() )
            inoutGrid._listSourceBox.resize( listPrimitive.size(), AABB::empty() );

        // 바뀐 칸마다 옛 상자(칠할 때)와 새 상자(지금)를 모은다. 둘 다 비면(가리개가 아니었고 아니다) 격자와 상관없는 변화다.
        vector<AABB> listDirtyBox;
        for ( const uint32 slot : listChangedSlot )
        {
            if ( slot >= inoutGrid._listSourceBox.size() )
                continue;
            const AABB oldBox              = inoutGrid._listSourceBox[slot];
            const AABB newBox              = Internal::findOccluderBox( slot < listPrimitive.size() ? listPrimitive[slot] : nullptr, pSceneDefault, gridBounds );
            inoutGrid._listSourceBox[slot] = newBox;
            if ( oldBox.isValid() )
                listDirtyBox.push_back( oldBox );
            if ( newBox.isValid() )
                listDirtyBox.push_back( newBox );
        }
        if ( listDirtyBox.empty() )
            return false;

        // 구간마다 칸을 끄고, 그 구간에 닿는 가리개(새 칸 포함 — 상자 목록이 이미 지금 값이다)를 구간 안만 다시 칠한다.
        for ( const AABB& dirtyBox : listDirtyBox )
        {
            SceneVoxelGrid::CellRange range{};
            if ( inoutGrid.findCellRange( dirtyBox, range ) == false )
                continue;
            inoutGrid.clearCells( range );
            // 끈 것은 칸 단위라 상자보다 넓다 — 그 칸들의 상자에 닿는 가리개를 다시 칠해야 가장자리 칸이 비지 않는다.
            const float32 cellSize = inoutGrid._cellSize;
            const AABB    rangeBox{
                inoutGrid._origin + float3{    static_cast<float32>( range._arrFirst[0] ),     static_cast<float32>( range._arrFirst[1] ),     static_cast<float32>( range._arrFirst[2] )}
                  * cellSize,
                inoutGrid._origin + float3{static_cast<float32>( range._arrLast[0] + 1u ), static_cast<float32>( range._arrLast[1] + 1u ), static_cast<float32>( range._arrLast[2] + 1u )}
                  * cellSize
            };
            for ( size_t slot = 0; slot < inoutGrid._listSourceBox.size() && slot < listPrimitive.size(); ++slot )
            {
                const AABB& sourceBox = inoutGrid._listSourceBox[slot];
                if ( sourceBox.isValid() == false || sourceBox.intersects( rangeBox ) == false || listPrimitive[slot] == nullptr )
                    continue;
                Internal::voxelizeMesh( *static_cast<const MeshComponent*>( listPrimitive[slot] ), pSceneDefault, range, inoutGrid );
            }
        }
        return true;
    }
} // namespace sw
