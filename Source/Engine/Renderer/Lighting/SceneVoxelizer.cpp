#include "pch.h"

#include "Engine/Renderer/Lighting/SceneVoxelizer.h"

#include "Core/Container/unordered_map.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/PackUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include <algorithm>

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

            /** @brief 레지스트리 칸 하나를 지금 값으로 모읍니다. 격자에 칠할 가리개가 아니면(빈 칸 · 가리개 아님 · 격자 밖 · 그릴 구간 없음) 빈 원천입니다. */
            static SceneVoxelSource makeSource( const PrimitiveComponent* pPrimitive, Material* pSceneDefault, const AABB& gridBounds )
            {
                SceneVoxelSource source{};
                // 레지스트리의 실제 타입은 메시 컴포넌트 계열뿐이다(PrimitiveComponent.h 의 불변식).
                const MeshComponent* pMesh = static_cast<const MeshComponent*>( pPrimitive );
                if ( pMesh == nullptr || isOccluder( *pMesh, pSceneDefault ) == false )
                    return source;
                AABB worldBox{};
                if ( pMesh->getWorldBox( worldBox ) == false || worldBox.intersects( gridBounds ) == false )
                    return source;
                uint32 first = 0;
                uint32 count = 0;
                if ( pMesh->findDrawSection( first, count ) == false )
                    return source; // 구간 이름이 메시에 없다 — 그리지 않는 것은 가리지도 않는다
                const uint32 meshIndexCount = pMesh->getRawMesh()->getIndexCount();
                if ( count == 0 )
                    count = meshIndexCount; // 구간 이름이 비면 (0, 0) — 메시 전체
                if ( count < 3 || first > meshIndexCount || count > meshIndexCount - first )
                    return source; // LOD 인덱스 흐름의 범위 — 복셀은 LOD 0 만 안다
                source._mesh           = pMesh->getMesh();
                source._contentID      = source._mesh->getContentID();
                source._world          = pMesh->getWorldMatrix();
                source._box            = worldBox;
                source._firstIndex     = first;
                source._indexCount     = count - count % 3u;
                source._packedEmission = PackUtil::packRGB9E5( findEmission( *pMesh, pSceneDefault ) );
                return source;
            }

            /** @brief 정수 넷을 float4 의 비트로 싣습니다(셰이더는 asuint 로 읽는다). */
            static float4 packBits( uint32 x, uint32 y, uint32 z, uint32 w )
            {
                return float4{ BitUtil::bitsToFloat( x ), BitUtil::bitsToFloat( y ), BitUtil::bitsToFloat( z ), BitUtil::bitsToFloat( w ) };
            }

            /** @brief 지우기 · 칠하기 일 하나의 앞 다섯 행(첫 스레드 · 토막 수 · 자리 · 격자 · 구간)을 붙입니다. 배치는 scene_voxelize.hlsl 머리 주석이다. */
            static void appendJobRows( const SceneVoxelPaintTarget& target, const SceneVoxelGrid::CellRange& range, uint32 firstThread, uint32 sliceCount, uint32 emissionRank, uint32 firstIndex,
                                       uint32 firstVertex, vector<float4>& inoutListRow )
            {
                const SceneVoxelGrid& grid = target._pScene->_grid;
                inoutListRow.push_back( packBits( firstThread, sliceCount, target._wordOffset, target._emissionOffset ) );
                inoutListRow.push_back( packBits( grid._dimX, grid._dimY, grid._dimZ, emissionRank ) );
                inoutListRow.push_back( packBits( range._arrFirst[0], range._arrFirst[1], range._arrFirst[2], firstIndex ) );
                inoutListRow.push_back( packBits( range._arrLast[0], range._arrLast[1], range._arrLast[2], firstVertex ) );
                inoutListRow.push_back( float4{ grid._origin._x, grid._origin._y, grid._origin._z, grid._cellSize } );
            }

            /** @brief 월드 행렬 네 행(행 벡터 규약 — 셰이더는 mul( float4( p, 1 ), world ))을 붙입니다. 지우기는 읽지 않으므로 단위 행렬이다. */
            static void appendMatrixRows( const float4x4& world, vector<float4>& inoutListRow )
            {
                inoutListRow.push_back( float4{ world._11, world._12, world._13, world._14 } );
                inoutListRow.push_back( float4{ world._21, world._22, world._23, world._24 } );
                inoutListRow.push_back( float4{ world._31, world._32, world._33, world._34 } );
                inoutListRow.push_back( float4{ world._41, world._42, world._43, world._44 } );
            }

            /** @brief 되돌리기 일의 행렬 자리 — 첫 행에 정적 사본의 점유 · 발광 첫 단어, 나머지는 읽지 않는다. */
            static void appendRestoreRows( const SceneVoxelPaintTarget& target, vector<float4>& inoutListRow )
            {
                inoutListRow.push_back( packBits( target._staticWordOffset, target._staticEmissionOffset, 0u, 0u ) );
                inoutListRow.push_back( float4{} );
                inoutListRow.push_back( float4{} );
                inoutListRow.push_back( float4{} );
            }

            /** @brief 격자 전체 구간 하나로 바꿉니다. */
            static void setFullRange( const SceneVoxelGrid& grid, vector<SceneVoxelGrid::CellRange>& outListRange )
            {
                outListRange.clear();
                outListRange.push_back( grid.getFullRange() );
            }

            /** @brief 메시의 정점 · 인덱스 자리를 얻습니다(처음 본 메시면 끝에 붙인다). */
            static SceneVoxelPaintPlan::MeshSpan acquireMeshSpan( const shared_ptr<Mesh>& mesh, unordered_map<const Mesh*, uint32>& inoutMapSpan, SceneVoxelPaintPlan& inoutPlan )
            {
                const auto it = inoutMapSpan.find( mesh.get() );
                if ( it != inoutMapSpan.end() )
                    return inoutPlan._listMesh[it->second];
                inoutMapSpan[mesh.get()] = static_cast<uint32>( inoutPlan._listMesh.size() );
                inoutPlan._listMesh.push_back( SceneVoxelPaintPlan::MeshSpan{ mesh, inoutPlan._vertexCount, inoutPlan._indexCount } );
                inoutPlan._vertexCount += mesh->getVertexCount();
                inoutPlan._indexCount += mesh->getIndexCount();
                return inoutPlan._listMesh.back();
            }

            /** @brief 구간 @p range 에 닿는 원천 하나를 칠할 일을 덧붙입니다. 칠하지 않는 원천 · 닿지 않는 원천이면 덧붙이지 않습니다. */
            static void appendPaintJob( const SceneVoxelPaintTarget& target, const SceneVoxelGrid::CellRange& range, const AABB& rangeBox, const SceneVoxelSource& source,
                                        unordered_map<const Mesh*, uint32>& inoutMapSpan, SceneVoxelPaintPlan& inoutPlan, vector<float4>& inoutListRow, uint32& inoutThreadCount, uint32& inoutJobCount )
            {
                if ( source.isPainted() == false || source._box.intersects( rangeBox ) == false )
                    return;
                const SceneVoxelPaintPlan::MeshSpan span          = acquireMeshSpan( source._mesh, inoutMapSpan, inoutPlan );
                const uint32                        triangleCount = source._indexCount / 3u;
                const uint32                        sliceCount    = SceneVoxelizer::computeSliceCount( target._pScene->_grid, range, source._box, triangleCount );
                appendJobRows( target, range, inoutThreadCount, sliceCount, target._pScene->findEmissionRank( source._packedEmission ), span._firstIndex + source._firstIndex, span._firstVertex,
                               inoutListRow );
                appendMatrixRows( source._world, inoutListRow );
                inoutThreadCount += triangleCount * sliceCount;
                ++inoutJobCount;
            }

            /** @brief 원천의 발광입니다(칠하지 않으면 0). */
            static uint32 getEmission( const SceneVoxelSource& source ) { return source.isPainted() ? source._packedEmission : 0u; }
        };
    } // namespace

    SceneVoxelScene SceneVoxelizer::makeScene( const PrimitiveRegistry& primitives, Material* pSceneDefault, const AABB& bounds, float32 cellSize )
    {
        SceneVoxelScene scene;
        scene._grid = SceneVoxelGrid::make( bounds, cellSize );
        if ( scene._grid.isEmpty() )
            return scene;
        const AABB               gridBounds = scene._grid.getBounds();
        vector<SceneVoxelSource> listSource;
        listSource.reserve( primitives.getAll().size() );
        for ( const PrimitiveComponent* pPrimitive : primitives.getAll() )
        {
            listSource.push_back( SceneVoxelizerInternal::makeSource( pPrimitive, pSceneDefault, gridBounds ) );
        }
        scene.setStaticSources( std::move( listSource ) );
        scene.rebuildEmissionPalette();
        return scene;
    }

    bool SceneVoxelizer::updateScene( SceneVoxelScene& inoutScene, const PrimitiveRegistry& primitives, Material* pSceneDefault, const vector<uint32>& listChangedSlot )
    {
        if ( inoutScene._grid.isEmpty() )
            return false;
        using Internal                                   = SceneVoxelizerInternal;
        const AABB                         gridBounds    = inoutScene._grid.getBounds();
        const vector<PrimitiveComponent*>& listPrimitive = primitives.getAll();
        const SceneVoxelSource             emptySource{};
        vector<SceneVoxelSource>           listStatic; // 정적 층이 바뀔 때만 베낀다
        bool                               bStaticChanged   = false;
        bool                               bMovingChanged   = false;
        bool                               bEmissionChanged = false;
        for ( const uint32 slot : listChangedSlot )
        {
            const PrimitiveComponent* const pPrimitive = slot < listPrimitive.size() ? listPrimitive[slot] : nullptr;
            SceneVoxelSource                current    = Internal::makeSource( pPrimitive, pSceneDefault, gridBounds );
            auto                            moving     = std::lower_bound( inoutScene._listMovingSource.begin(), inoutScene._listMovingSource.end(), slot, []( const SceneVoxelMovingSource& entry, uint32 value )
                                           { return entry._slot < value; } );
            if ( moving != inoutScene._listMovingSource.end() && moving->_slot == slot )
            {
                if ( current.isSamePaint( moving->_source ) == false )
                {
                    bEmissionChanged = bEmissionChanged || Internal::getEmission( current ) != Internal::getEmission( moving->_source );
                    bMovingChanged   = true;
                    moving->_source  = std::move( current );
                }
                // 빈 칸은 움직이는 층에서 나간다 — 다음 물체가 이 칸을 쓰면 정적으로 다시 시작한다. 격자 밖으로 나간 물체는 남는다(드나들 때마다 정적 층을 오가지 않는다).
                if ( pPrimitive == nullptr )
                    (void)inoutScene._listMovingSource.erase( moving );
                continue;
            }
            const vector<SceneVoxelSource>& listKept = bStaticChanged ? listStatic : inoutScene.getStaticSources();
            const SceneVoxelSource&         kept     = slot < listKept.size() ? listKept[slot] : emptySource;
            if ( current.isSamePaint( kept ) )
                continue;
            bEmissionChanged = bEmissionChanged || Internal::getEmission( current ) != Internal::getEmission( kept );
            // 칠해 둔 것이 바뀌었으면(움직임 · 다시 지은 메시) 이제부터 움직이는 층이다. 드나든 것은 정적 층이다.
            const bool bMovesNow = kept.isPainted() && current.isPainted();
            if ( bStaticChanged == false )
                listStatic = inoutScene.getStaticSources();
            bStaticChanged = true;
            if ( listStatic.size() < listPrimitive.size() )
                listStatic.resize( listPrimitive.size() ); // 메시 칸 영역이 늘었다(늘린 칸은 아직 칠하지 않았다)
            if ( slot >= listStatic.size() )
                continue;
            listStatic[slot] = bMovesNow ? SceneVoxelSource{} : current;
            if ( bMovesNow == false )
                continue;
            (void)inoutScene._listMovingSource.insert( moving, SceneVoxelMovingSource{ slot, std::move( current ) } );
            inoutScene._bMovingLayer = SW_TRUE;
        }
        if ( bStaticChanged )
            inoutScene.setStaticSources( std::move( listStatic ) );
        if ( bEmissionChanged )
            inoutScene.rebuildEmissionPalette();
        return bStaticChanged || bMovingChanged;
    }

    void SceneVoxelizer::collectDirtyRanges( const SceneVoxelScene& painted, const SceneVoxelScene& scene, vector<SceneVoxelGrid::CellRange>& outListRange )
    {
        outListRange.clear();
        if ( scene._grid.isEmpty() )
            return;
        // 격자 모양이 다르면 칸 번호가, 발광 목록이 다르면 이미 칠한 순위의 뜻이 다르다.
        bool                            bWhole    = painted._grid.hasSameShape( scene._grid ) == false || painted._listEmissionPalette != scene._listEmissionPalette;
        uint64                          cellCount = 0;
        const SceneVoxelSource          emptySource{};
        const vector<SceneVoxelSource>& listBefore = painted.getStaticSources();
        const vector<SceneVoxelSource>& listAfter  = scene.getStaticSources();
        // 정적 층을 나눠 든 장면은 볼 것이 없다(움직이는 것만 바뀐 프레임).
        const size_t slotCount = painted._pListStaticSource == scene._pListStaticSource ? size_t{ 0 } : MathUtil::max( listBefore.size(), listAfter.size() );
        for ( size_t slot = 0; bWhole == false && slot < slotCount; ++slot )
        {
            const SceneVoxelSource& before = slot < listBefore.size() ? listBefore[slot] : emptySource;
            const SceneVoxelSource& after  = slot < listAfter.size() ? listAfter[slot] : emptySource;
            if ( before.isSamePaint( after ) )
                continue;
            for ( const SceneVoxelSource* pSource : { &before, &after } )
            {
                SceneVoxelGrid::CellRange range{};
                if ( pSource->isPainted() == false || scene._grid.findCellRange( pSource->_box, range ) == false )
                    continue;
                outListRange.push_back( range );
                cellCount += range.getCellCount();
            }
            bWhole = cellCount > scene._grid.getCellCount();
        }
        if ( bWhole )
            SceneVoxelizerInternal::setFullRange( scene._grid, outListRange );
    }

    void SceneVoxelizer::appendMovingRanges( const SceneVoxelScene& painted, const SceneVoxelScene& scene, vector<SceneVoxelGrid::CellRange>& inoutListRange )
    {
        if ( scene._grid.isEmpty() )
            return;
        const auto appendRange = [&scene, &inoutListRange]( const SceneVoxelSource& source )
        {
            SceneVoxelGrid::CellRange range{};
            if ( source.isPainted() && scene._grid.findCellRange( source._box, range ) )
                inoutListRange.push_back( range );
        };
        // 두 목록은 칸 번호 오름차순이다 — 나란히 훑어 같은 칸끼리 견준다.
        const vector<SceneVoxelMovingSource>& listBefore  = painted._listMovingSource;
        const vector<SceneVoxelMovingSource>& listAfter   = scene._listMovingSource;
        size_t                                beforeIndex = 0;
        size_t                                afterIndex  = 0;
        while ( beforeIndex < listBefore.size() || afterIndex < listAfter.size() )
        {
            const uint32 beforeSlot = beforeIndex < listBefore.size() ? listBefore[beforeIndex]._slot : constant::invalid_index::kUint32;
            const uint32 afterSlot  = afterIndex < listAfter.size() ? listAfter[afterIndex]._slot : constant::invalid_index::kUint32;
            const bool   bSameSlot  = beforeSlot == afterSlot;
            if ( bSameSlot && listBefore[beforeIndex]._source.isSamePaint( listAfter[afterIndex]._source ) )
            {
                ++beforeIndex;
                ++afterIndex;
                continue;
            }
            if ( beforeSlot <= afterSlot )
            {
                appendRange( listBefore[beforeIndex]._source );
                ++beforeIndex;
            }
            if ( afterSlot <= beforeSlot )
            {
                appendRange( listAfter[afterIndex]._source );
                ++afterIndex;
            }
        }
        uint64 cellCount = 0;
        for ( const SceneVoxelGrid::CellRange& range : inoutListRange )
        {
            cellCount += range.getCellCount();
        }
        if ( cellCount > scene._grid.getCellCount() )
            SceneVoxelizerInternal::setFullRange( scene._grid, inoutListRange );
    }

    uint32 SceneVoxelizer::computeSliceCount( const SceneVoxelGrid& grid, const SceneVoxelGrid::CellRange& range, const AABB& box, uint32 triangleCount )
    {
        SceneVoxelGrid::CellRange boxRange{};
        if ( triangleCount == 0 || grid.findCellRange( box, boxRange ) == false )
            return 1;
        uint32 longest = 1;
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            const uint32 first = MathUtil::max( boxRange._arrFirst[axis], range._arrFirst[axis] );
            const uint32 last  = MathUtil::min( boxRange._arrLast[axis], range._arrLast[axis] );
            if ( first <= last )
                longest = MathUtil::max( longest, last - first + 1u );
        }
        return static_cast<uint32>( MathUtil::ceil( MathUtil::max( static_cast<float32>( longest ) / MathUtil::sqrt( static_cast<float32>( triangleCount ) ), 1.0f ) ) );
    }

    SceneVoxelPaintPlan SceneVoxelizer::planPaint( const vector<SceneVoxelPaintTarget>& listTarget )
    {
        using Internal = SceneVoxelizerInternal;
        SceneVoxelPaintPlan                plan;
        unordered_map<const Mesh*, uint32> mapSpan;
        vector<SceneVoxelGrid::CellRange>  listRange;
        const float4x4                     identity{};
        uint32                             clearJobCount   = 0;
        uint32                             paintJobCount   = 0;
        uint32                             restoreJobCount = 0;
        uint32                             movingJobCount  = 0;
        for ( vector<float4>* pListRow : { &plan._listClearRow, &plan._listPaintRow, &plan._listRestoreRow, &plan._listMovingRow } )
        {
            pListRow->push_back( float4{} ); // 머리 행 — 수를 다 센 뒤에 채운다
        }
        for ( const SceneVoxelPaintTarget& target : listTarget )
        {
            const SceneVoxelScene& scene   = *target._pScene;
            const bool             bLayers = target._staticWordOffset != kNoStaticCopy;
            SW_ASSERT( bLayers || scene._listMovingSource.empty() ); // 움직이는 층이 있는 장면은 정적 사본 자리를 받는다(`RealtimeProbeUpdater::planLayout`)
            if ( target._pPainted != nullptr )
                collectDirtyRanges( *target._pPainted, scene, listRange );
            else
                Internal::setFullRange( scene._grid, listRange );
            // 정적 층 — 움직이는 층이 있으면 정적 사본에, 없으면 읽을 자리에 바로 칠한다.
            SceneVoxelPaintTarget staticTarget = target;
            if ( bLayers )
            {
                staticTarget._wordOffset     = target._staticWordOffset;
                staticTarget._emissionOffset = target._staticEmissionOffset;
            }
            for ( const SceneVoxelGrid::CellRange& range : listRange )
            {
                Internal::appendJobRows( staticTarget, range, plan._clearThreadCount, 1u, 0u, 0u, 0u, plan._listClearRow );
                Internal::appendMatrixRows( identity, plan._listClearRow );
                plan._clearThreadCount += range.getCellCount();
                ++clearJobCount;
                // 끈 것은 칸 단위라 바뀐 상자보다 넓다 — 그 칸들의 상자에 닿는 원천을 다시 칠해야 가장자리 칸이 비지 않는다.
                const AABB rangeBox = scene._grid.computeRangeBounds( range );
                for ( const SceneVoxelSource& source : scene.getStaticSources() )
                {
                    Internal::appendPaintJob( staticTarget, range, rangeBox, source, mapSpan, plan, plan._listPaintRow, plan._paintThreadCount, paintJobCount );
                }
            }
            if ( bLayers == false )
                continue;
            // 움직이는 층 — 정적 층이 바뀐 구간과 움직인 것의 낡은 · 새 구간을 정적 사본으로 되돌리고, 거기 닿는 움직이는 메시만 칠한다.
            if ( target._pPainted != nullptr )
                appendMovingRanges( *target._pPainted, scene, listRange );
            for ( const SceneVoxelGrid::CellRange& range : listRange )
            {
                Internal::appendJobRows( target, range, plan._restoreThreadCount, 1u, 0u, 0u, 0u, plan._listRestoreRow );
                Internal::appendRestoreRows( target, plan._listRestoreRow );
                plan._restoreThreadCount += range.getCellCount();
                ++restoreJobCount;
                const AABB rangeBox = scene._grid.computeRangeBounds( range );
                for ( const SceneVoxelMovingSource& moving : scene._listMovingSource )
                {
                    Internal::appendPaintJob( target, range, rangeBox, moving._source, mapSpan, plan, plan._listMovingRow, plan._movingThreadCount, movingJobCount );
                }
            }
        }
        plan._listClearRow[0]   = Internal::packBits( clearJobCount, plan._clearThreadCount, kJobModeClear, kThreadsPerRow );
        plan._listPaintRow[0]   = Internal::packBits( paintJobCount, plan._paintThreadCount, kJobModePaint, kThreadsPerRow );
        plan._listRestoreRow[0] = Internal::packBits( restoreJobCount, plan._restoreThreadCount, kJobModeRestore, kThreadsPerRow );
        plan._listMovingRow[0]  = Internal::packBits( movingJobCount, plan._movingThreadCount, kJobModePaint, kThreadsPerRow );
        return plan;
    }
} // namespace sw
