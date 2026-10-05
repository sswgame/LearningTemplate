#include "pch.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Resource/DdsLoader.h"

namespace sw
{
    SW_LOG_CALLER( "TerrainComponent" );

    namespace
    {
        struct TerrainComponentInternal
        {
            /** @brief DXGI 비압축 8 비트 4 채널 — RGBA(UNORM · SRGB) 와 BGRA(UNORM · SRGB). */
            static constexpr uint32 kDxgiRgba8     = 28;
            static constexpr uint32 kDxgiRgba8Srgb = 29;
            static constexpr uint32 kDxgiBgra8     = 87;
            static constexpr uint32 kDxgiBgra8Srgb = 91;
            /** @brief LOD 를 바꾸기 전에 거리가 문턱을 넘어야 하는 비율 — 문턱에 선 카메라가 매 프레임 메시를 다시 만들지 않게. */
            static constexpr float32 kLodHysteresis = 0.1f;

            static const hashed_string& getLayerColorName( uint32 layerIndex )
            {
                static const hashed_string s_arrName[TerrainComponent::kMaxLayerCount] = { hashed_string( "layer0Color" ), hashed_string( "layer1Color" ),
                                                                                           hashed_string( "layer2Color" ), hashed_string( "layer3Color" ) };
                return s_arrName[layerIndex];
            }

            /** @brief 카메라에서 청크 사각형(xz)까지 · 청크 높이 중간까지의 거리입니다. */
            static float32 computeChunkDistance( const TerrainHeightfield& heightfield, const TerrainChunkLayout& layout, uint32 chunkX, uint32 chunkZ, const float3& viewPosition )
            {
                const float3  origin   = heightfield.getOrigin();
                const float2  cellSize = heightfield.getCellSize();
                const float32 minX     = origin._x + static_cast<float32>( chunkX * layout._chunkCells ) * cellSize._x;
                const float32 minZ     = origin._z + static_cast<float32>( chunkZ * layout._chunkCells ) * cellSize._y;
                const float32 maxX     = minX + static_cast<float32>( layout._chunkCells ) * cellSize._x;
                const float32 maxZ     = minZ + static_cast<float32>( layout._chunkCells ) * cellSize._y;
                const float32 deltaX   = MathUtil::max( MathUtil::max( minX - viewPosition._x, viewPosition._x - maxX ), 0.0f );
                const float32 deltaZ   = MathUtil::max( MathUtil::max( minZ - viewPosition._z, viewPosition._z - maxZ ), 0.0f );
                const int32   centerX  = static_cast<int32>( chunkX * layout._chunkCells + layout._chunkCells / 2 );
                const int32   centerZ  = static_cast<int32>( chunkZ * layout._chunkCells + layout._chunkCells / 2 );
                const float32 deltaY   = viewPosition._y - heightfield.getSampleHeight( centerX, centerZ );
                return MathUtil::sqrt( deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TerrainComponent::TerrainComponent()
        : _heightfieldPath{}
        , _materialPath{ "engine/materials/terrain.material" }
        , _splatTexturePath{}
        , _detailTexturePath{}
        , _listLayer{}
        , _size{ 256.0f, 256.0f }
        , _heightMin{ 0.0f }
        , _heightMax{ 32.0f }
        , _detailTiling{ 0.25f }
        , _cliffStart{ 0.7f }
        , _cliffLayer{ -1 }
        , _chunkCells{ 32 }
        , _lodDistance{ 40.0f }
        , _heightfield{}
        , _layout{}
        , _listChunk{}
        , _listWantedLod{}
        , _material{}
        , _pPrimitiveRegistry{ nullptr }
        , _bOriginDirty{ false }
    {
        setCanEverTick( true );
        setTickGroup( TickGroup::PostUpdate );
    }

    TerrainComponent::~TerrainComponent()
    {
        releaseChunks();
    }

    void TerrainComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<TerrainComponent>( this ); // `findTerrainAt` 이 씬을 훑지 않고 본다
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        if ( _heightfieldPath.empty() == false )
            (void)reloadTerrain(); // 실패는 안에서 알린다 — 그리지 않을 뿐이다
    }

    void TerrainComponent::onUnregister( GameObjectManager& manager )
    {
        releaseChunks();
        _material.release();
        _pPrimitiveRegistry = nullptr;
        manager.getComponentRegistry().remove<TerrainComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void TerrainComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        (void)reloadTerrain();
    }

    void TerrainComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        // 켜고 끄기는 기하를 바꾸지 않는다 — 청크를 다시 짓지 않고 빌더가 다시 보게만 한다.
        static const hashed_string s_activeName( "_bActive" );
        if ( propertyName == s_activeName )
        {
            markChunksDirty();
            return;
        }
        // 트랜스폼(위치)이 바뀐 것도 여기로 온다 — 지형 원점이 따라 움직인다.
        (void)reloadTerrain();
    }

    void TerrainComponent::onOwnerActiveInHierarchyChanged()
    {
        SceneComponent::onOwnerActiveInHierarchyChanged();
        markChunksDirty();
    }

    void TerrainComponent::markChunksDirty()
    {
        for ( Chunk& chunk : _listChunk )
        {
            if ( chunk._batch != nullptr )
                chunk._batch->markAllEntriesDirty();
        }
    }

    void TerrainComponent::onWorldTransformUpdated()
    {
        SceneComponent::onWorldTransformUpdated();
        if ( _heightfield.isValid() )
            _bOriginDirty.store( true, std::memory_order_relaxed );
    }

    void TerrainComponent::onTick( float32 deltaTime )
    {
        SceneComponent::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && _bOriginDirty.exchange( false, std::memory_order_relaxed ) )
        {
            // 다시 읽기는 배치를 등록부에서 빼고 넣는 구조 변경이다 — 틱 뒤로 미루고, 그 사이 사라졌을 수 있으니 핸들로 다시 찾는다.
            const ComponentHandle self = getHandle();
            pManager->executeOrDeferPostTick( [pManager, self]()
            {
                TerrainComponent* pTerrain = static_cast<TerrainComponent*>( pManager->resolveComponent( self ) );
                if ( pTerrain != nullptr )
                    (void)pTerrain->reloadTerrain();
            } );
            return;
        }
        float3 viewPosition{};
        if ( pManager != nullptr && _listChunk.empty() == false && EnvironmentUtil::findViewPosition( *pManager, viewPosition ) )
            (void)updateLods( viewPosition );
    }

    void TerrainComponent::setHeightRange( float32 heightMin, float32 heightMax )
    {
        _heightMin = heightMin;
        _heightMax = heightMax;
    }

    bool TerrainComponent::reloadTerrain()
    {
        releaseChunks();
        _heightfield.shutdown();
        if ( _heightfieldPath.empty() )
            return false;

        HeightfieldData data;
        if ( data.loadFromResource( _heightfieldPath ) == false )
            return false;
        if ( _heightfield.initialize( data, getWorldPosition(), _size, _heightMin, _heightMax ) == false )
            return false;
        if ( TerrainMeshBuilder::makeLayout( data._resolution, _chunkCells, _layout ) == false )
        {
            SW_LOG_ERROR( "Terrain '%#': chunk size %# must be a power of two dividing resolution - 1 (%#)", _heightfieldPath.c_str(), _chunkCells, data._resolution - 1 );
            _heightfield.shutdown();
            return false;
        }
        loadSplatWeights();
        acquireMaterial();
        createChunks();
        return true;
    }

    void TerrainComponent::loadSplatWeights()
    {
        using Internal = TerrainComponentInternal;
        if ( _splatTexturePath.empty() )
        {
            _heightfield.setSplat( 0, 0, {} );
            return;
        }
        DdsImageData image;
        if ( DdsLoader::loadFromResource( _splatTexturePath, image ) == false )
        {
            SW_LOG_WARNING( "Terrain splat '%#' could not be read - layer queries return layer 0", _splatTexturePath.c_str() );
            _heightfield.setSplat( 0, 0, {} );
            return;
        }
        const bool   bRgba      = image._dxgiFormat == Internal::kDxgiRgba8 || image._dxgiFormat == Internal::kDxgiRgba8Srgb;
        const bool   bBgra      = image._dxgiFormat == Internal::kDxgiBgra8 || image._dxgiFormat == Internal::kDxgiBgra8Srgb;
        const size_t texelCount = static_cast<size_t>( image._width ) * image._height;
        if ( ( bRgba == false && bBgra == false ) || image._bytes.size() < texelCount * 4 )
        {
            SW_LOG_WARNING( "Terrain splat '%#' is DXGI format %# - CPU layer queries need uncompressed RGBA8 (import rule Terrain_Weights)", _splatTexturePath.c_str(),
                            image._dxgiFormat );
            _heightfield.setSplat( 0, 0, {} );
            return;
        }
        vector<uint8> rgbaBytes( image._bytes.begin(), image._bytes.begin() + static_cast<ptrdiff_t>( texelCount * 4 ) );
        if ( bBgra )
        {
            for ( size_t texel = 0; texel < texelCount; ++texel )
                std::swap( rgbaBytes[texel * 4], rgbaBytes[texel * 4 + 2] );
        }
        _heightfield.setSplat( image._width, image._height, rgbaBytes );
    }

    void TerrainComponent::acquireMaterial()
    {
        using Internal = TerrainComponentInternal;
        if ( _material.acquire( _materialPath ) == false )
            return;
        for ( uint32 layerIndex = 0; layerIndex < kMaxLayerCount; ++layerIndex )
        {
            const float4 color = layerIndex < _listLayer.size() ? _listLayer[layerIndex]._color : float4{ 1.0f, 1.0f, 1.0f, 1.0f };
            _material.setVector( Internal::getLayerColorName( layerIndex ), color );
        }
        float32 arrDetail[kMaxLayerCount]{};
        float32 arrTriplanar[kMaxLayerCount]{};
        for ( uint32 layerIndex = 0; layerIndex < kMaxLayerCount && layerIndex < _listLayer.size(); ++layerIndex )
        {
            arrDetail[layerIndex]    = _listLayer[layerIndex]._detailStrength;
            arrTriplanar[layerIndex] = _listLayer[layerIndex]._bTriplanar ? 1.0f : 0.0f;
        }
        _material.setVector( hashed_string( "layerDetail" ), float4{ arrDetail[0], arrDetail[1], arrDetail[2], arrDetail[3] } );
        _material.setVector( hashed_string( "layerTriplanar" ), float4{ arrTriplanar[0], arrTriplanar[1], arrTriplanar[2], arrTriplanar[3] } );
        // x = 스플랫 너비(텍셀 중심 맞추기), y = 디테일 반복(1/m), z = 절벽 문턱(노멀 y), w = 절벽 레이어(−1 = 끔)
        const float32 splatWidth = static_cast<float32>( _heightfield.getSplatWidth() );
        _material.setVector( hashed_string( "terrainParams" ), float4{ splatWidth, _detailTiling, _cliffStart, static_cast<float32>( _cliffLayer ) } );
        _material.setTexture( hashed_string( "splatMap" ), _splatTexturePath );
        _material.setTexture( hashed_string( "detailMap" ), _detailTexturePath );
    }

    void TerrainComponent::releaseChunks()
    {
        // 배치의 소멸자가 등록부에서 뺀다.
        _listChunk.clear();
    }

    void TerrainComponent::createChunks()
    {
        releaseChunks();
        const uint32 chunkCount = _layout.getChunkCount();
        _listChunk.resize( chunkCount );
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const uint32 chunkX = chunkIndex % _layout._chunkCountX;
            const uint32 chunkZ = chunkIndex / _layout._chunkCountX;
            Chunk&       chunk  = _listChunk[chunkIndex];
            chunk._lod          = 0;
            for ( uint32& neighborLod : chunk._arrNeighborLod )
                neighborLod = 0;
            chunk._batch = sw::make_unique<MeshInstanceBatch>( Mesh::create(), _material.getMaterial(), _material.getInstance(), 1u );
            chunk._batch->setOwnerComponent( this );
            chunk._batch->setWorld( 0, float4x4::createTranslation( TerrainMeshBuilder::computeChunkTranslation( _heightfield, _layout, chunkX, chunkZ ) ) );
            chunk._batch->setBoundsRadius( 0, TerrainMeshBuilder::computeChunkBoundsRadius( _heightfield, _layout, chunkX, chunkZ ) );
            rebuildChunkMesh( chunkIndex );
            if ( _pPrimitiveRegistry != nullptr )
                _pPrimitiveRegistry->addInstanceBatch( chunk._batch.get() );
        }
    }

    void TerrainComponent::rebuildChunkMesh( uint32 chunkIndex )
    {
        Chunk&            chunk  = _listChunk[chunkIndex];
        const uint32      chunkX = chunkIndex % _layout._chunkCountX;
        const uint32      chunkZ = chunkIndex / _layout._chunkCountX;
        vector<RHIVertex> listVertex;
        TerrainMeshBuilder::buildChunkVertices( _heightfield, _layout, chunkX, chunkZ, chunk._lod, chunk._arrNeighborLod, listVertex );
        chunk._vertexCount    = static_cast<uint32>( listVertex.size() );
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        chunk._batch->setMesh( std::move( mesh ) );
    }

    void TerrainComponent::fillNeighborLods( uint32 chunkX, uint32 chunkZ, const vector<uint32>& listLod, uint32 ( &outArrLod )[4] ) const
    {
        const uint32 ownLod                                       = listLod[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX];
        outArrLod[static_cast<uint32>( TerrainChunkSide::West )]  = chunkX > 0 ? listLod[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX - 1] : ownLod;
        outArrLod[static_cast<uint32>( TerrainChunkSide::East )]  = chunkX + 1 < _layout._chunkCountX ? listLod[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX + 1] : ownLod;
        outArrLod[static_cast<uint32>( TerrainChunkSide::South )] = chunkZ > 0 ? listLod[static_cast<size_t>( chunkZ - 1 ) * _layout._chunkCountX + chunkX] : ownLod;
        outArrLod[static_cast<uint32>( TerrainChunkSide::North )] = chunkZ + 1 < _layout._chunkCountZ ? listLod[static_cast<size_t>( chunkZ + 1 ) * _layout._chunkCountX + chunkX] : ownLod;
    }

    uint32 TerrainComponent::updateLods( const float3& viewPosition )
    {
        using Internal          = TerrainComponentInternal;
        const uint32 chunkCount = static_cast<uint32>( _listChunk.size() );
        _listWantedLod.resize( chunkCount );
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const uint32  chunkX     = chunkIndex % _layout._chunkCountX;
            const uint32  chunkZ     = chunkIndex / _layout._chunkCountX;
            const float32 distance   = Internal::computeChunkDistance( _heightfield, _layout, chunkX, chunkZ, viewPosition );
            const uint32  currentLod = _listChunk[chunkIndex]._lod;
            uint32        wantedLod  = TerrainMeshBuilder::selectLod( distance, _lodDistance, _layout._maxLod );
            // 문턱 근처에서 흔들리지 않게 — 바꾸는 쪽으로 10 % 더 넘어야 바꾼다.
            if ( wantedLod != currentLod )
            {
                const float32 margin = wantedLod > currentLod ? ( 1.0f - Internal::kLodHysteresis ) : ( 1.0f + Internal::kLodHysteresis );
                if ( TerrainMeshBuilder::selectLod( distance * margin, _lodDistance, _layout._maxLod ) != wantedLod )
                    wantedLod = currentLod;
            }
            _listWantedLod[chunkIndex] = wantedLod;
        }

        uint32 rebuiltCount{ 0 };
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            Chunk&       chunk  = _listChunk[chunkIndex];
            const uint32 chunkX = chunkIndex % _layout._chunkCountX;
            const uint32 chunkZ = chunkIndex / _layout._chunkCountX;
            uint32       arrNeighborLod[4]{};
            fillNeighborLods( chunkX, chunkZ, _listWantedLod, arrNeighborLod );
            bool bChanged = chunk._lod != _listWantedLod[chunkIndex];
            for ( uint32 side = 0; side < 4; ++side )
            {
                // 이웃이 이쪽보다 고우면 접는 것은 이웃의 일이다 — 그 변은 이쪽 메시에 영향이 없다.
                const uint32 oldEffective   = MathUtil::max( chunk._arrNeighborLod[side], chunk._lod );
                const uint32 newEffective   = MathUtil::max( arrNeighborLod[side], _listWantedLod[chunkIndex] );
                bChanged                    = bChanged || oldEffective != newEffective;
                chunk._arrNeighborLod[side] = arrNeighborLod[side];
            }
            chunk._lod = _listWantedLod[chunkIndex];
            if ( bChanged == false )
                continue;
            rebuildChunkMesh( chunkIndex );
            ++rebuiltCount;
        }
        return rebuiltCount;
    }

    uint32 TerrainComponent::getChunkLod( uint32 chunkX, uint32 chunkZ ) const
    {
        const size_t chunkIndex = static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX;
        return ( chunkX < _layout._chunkCountX && chunkIndex < _listChunk.size() ) ? _listChunk[chunkIndex]._lod : 0u;
    }

    uint32 TerrainComponent::getChunkVertexCount( uint32 chunkX, uint32 chunkZ ) const
    {
        const size_t chunkIndex = static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX;
        return ( chunkX < _layout._chunkCountX && chunkIndex < _listChunk.size() ) ? _listChunk[chunkIndex]._vertexCount : 0u;
    }

    TerrainComponent* TerrainComponent::findTerrainAt( const GameObjectManager& manager, float32 worldX, float32 worldZ )
    {
        for ( TerrainComponent* pTerrain : manager.getComponentRegistry().getAll<TerrainComponent>() )
        {
            if ( pTerrain->isPendingDestroy() || pTerrain->_heightfield.isValid() == false )
                continue;
            const float3 origin  = pTerrain->_heightfield.getOrigin();
            const float2 size    = pTerrain->_heightfield.getSize();
            const bool   bInside = origin._x <= worldX && worldX <= origin._x + size._x && origin._z <= worldZ && worldZ <= origin._z + size._y;
            if ( bInside )
                return pTerrain;
        }
        return nullptr;
    }
} // namespace sw
