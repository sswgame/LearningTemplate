#include "pch.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Environment/Terrain/HeightfieldData.h"
#include "Engine/Environment/Terrain/LayerWeightsData.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    SW_LOG_CALLER( "TerrainComponent" );

    namespace
    {
        struct TerrainComponentInternal
        {
            /** @brief LOD 를 바꾸기 전에 거리가 임계값을 넘어야 하는 비율입니다. 임계값에 선 카메라가 매 프레임 메시를 다시 만들지 않게 합니다. */
            static constexpr float32 kLODHysteresis = 0.1f;
            /**
             * @brief 높이 쓰기로 낡은 청크 메시를 한 프레임(쓰기 한 번 또는 틱 한 번)에 다시 만드는 최대 수입니다. 브러시 한 번이 닿는 청크(넷 이하)는 바로 끝난다.
             * @details Debug 측정으로 32 칸 청크 하나가 약 4.4 ms 이고(`TerrainBenchTest.WriteHeightsRectSizes`), 257 샘플 전체(청크 64)는 288 ms 였다(정점 만들기를 워커로
             *          나누기 전의 값). 넷은 워커로 나눠 만든다(`rebuildChunkMeshes`). Release 로 다시 재어 GT 2 ms 에 맞게 고친다.
             */
            static constexpr uint32 kMaxChunkMeshRebuildsPerFrame = 4;
            /** @brief 청크 메시 정점 만들기를 워커로 나누는 최소 청크 수입니다. 청크 하나가 디스패치 비용(수십 us)보다 훨씬 크다. */
            static constexpr uint32 kParallelChunkMeshCount = 2;
            /** @brief `findEditedArea` 가 바뀐 청크 둘레로 더 넓히는 칸 수입니다. 높이 질의는 셀의 두 샘플을, 노멀은 그 이웃 샘플을 읽는다. */
            static constexpr uint32 kEditedAreaMarginCells = 2;
            /** @brief `writeState` 형식 버전입니다. 형식을 바꾸면 올린다. */
            static constexpr uint32 kStateVersion = 2; ///< 2: 구멍 마스크를 더했고, 높이의 해상도가 다시 읽은 것과 달라도 복원한다(크기 바꾸기)

            /** @brief 지형마다의 스플랫 텍스처 경로(`cpu/terrain/<번호>`)를 새로 냅니다. 놓인 경로를 다시 쓰지 않도록 번호는 프로세스 안에서 늘기만 한다. */
            static string makeCPUSplatPath()
            {
                static atomic<uint32> s_serial{ 0 };
                return string{ TextureCache::kCPUTexturePrefix } + "terrain/splat" + to_string( s_serial.fetch_add( 1u, std::memory_order_relaxed ) );
            }

            static const hashed_string& getLayerColorName( uint32 layerIndex )
            {
                static const hashed_string s_arrName[TerrainComponent::kMaxLayerCount] = { hashed_string( "layer0Color" ), hashed_string( "layer1Color" ),
                                                                                           hashed_string( "layer2Color" ), hashed_string( "layer3Color" ) };
                return s_arrName[layerIndex];
            }

            /**
             * @brief 샘플 사각형 @p rect 의 샘플을 하나라도 쓰는 청크의 사각형(청크 좌표)입니다.
             * @details 청크 c 는 샘플 c × 칸 .. (c + 1) × 칸 을 쓴다. 그래서 경계 샘플은 양쪽 청크에 든다.
             */
            static TerrainSampleRect findChunksOfSamples( const TerrainChunkLayout& layout, const TerrainSampleRect& rect )
            {
                const uint32      cells = layout._chunkCells;
                TerrainSampleRect chunkRect{};
                chunkRect.include( rect._minX > 0 ? ( rect._minX - 1 ) / cells : 0u, rect._minZ > 0 ? ( rect._minZ - 1 ) / cells : 0u );
                chunkRect.include( MathUtil::min( rect._maxX / cells, layout._chunkCountX - 1 ), MathUtil::min( rect._maxZ / cells, layout._chunkCountZ - 1 ) );
                return chunkRect;
            }

            /** @brief 카메라에서 청크 사각형(xz)까지의 거리와 청크 높이 중간까지의 거리입니다. */
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

            /** @brief 다시 만들 청크 하나의 LOD 와 이웃 LOD 입니다(워커에 넘기는 값). */
            struct ChunkMeshRequest
            {
                uint32 _chunkIndex{ 0 };
                uint32 _lod{ 0 };
                uint32 _arrNeighborLOD[static_cast<uint32>( TerrainChunkSide::Count )]{};
            };

            /** @brief 청크 메시 정점 만들기 한 덩어리입니다. 워커는 높이장을 읽기만 하고 자기 구간의 정점 배열만 씁니다. */
            struct ChunkVertexJob
            {
                const TerrainHeightfield* _pHeightfield{ nullptr };
                const TerrainChunkLayout* _pLayout{ nullptr };
                const ChunkMeshRequest*   _pRequest{ nullptr };
                vector<RHIVertex>*        _pListVertex{ nullptr };

                void run( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        const ChunkMeshRequest& request = _pRequest[index];
                        const uint32            chunkX  = request._chunkIndex % _pLayout->_chunkCountX;
                        const uint32            chunkZ  = request._chunkIndex / _pLayout->_chunkCountX;
                        TerrainMeshBuilder::makeChunkVertices( *_pHeightfield, *_pLayout, chunkX, chunkZ, request._lod, request._arrNeighborLOD, _pListVertex[index] );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    TerrainComponent::TerrainComponent()
        : _heightfieldPath{}
        , _materialPath{}
        , _layerWeightsPath{}
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
        , _listWantedLOD{}
        , _listChunkGeneration{}
        , _listChunkSplatRevision{}
        , _listStaleChunk{}
        , _material{}
        , _pPrimitiveRegistry{ nullptr }
        , _heightfieldGeneration{ 0 }
        , _splatRevision{ 0 }
        , _bOriginDirty{ false }
        , _bHeightsWritten{ false }
        , _bHolesWritten{ false }
        , _bResolutionReplaced{ false }
        , _loadedHeightfieldPath{}
    {
        setCanEverTick( true );
        setTickGroup( TickGroup::PostUpdate );
    }

    TerrainComponent::~TerrainComponent()
    {
        releaseChunks();
        releaseCPUSplat();
    }

    void TerrainComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<TerrainComponent>( this ); // `findTerrainAt` 이 씬을 훑지 않고 본다
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        if ( _heightfieldPath.empty() == false )
            (void)reloadTerrain(); // 실패는 안에서 알린다. 그리지 않을 뿐이다.
    }

    void TerrainComponent::onUnregister( GameObjectManager& manager )
    {
        releaseChunks();
        _material.release();
        releaseCPUSplat();
        _pPrimitiveRegistry = nullptr;
        manager.getComponentRegistry().remove<TerrainComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void TerrainComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        (void)reloadTerrain(); // 경로가 없으면 지형 없이 남고, 읽기 실패는 HeightfieldData 가 오류로 남긴다
    }

    void TerrainComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        // 켜고 끄기는 기하를 바꾸지 않는다. 청크를 다시 만들지 않고 빌더가 다시 확인하게만 한다.
        static const hashed_string s_activeName( "_bActive" );
        if ( propertyName == s_activeName )
        {
            markChunksDirty();
            return;
        }
        // 트랜스폼(위치)이 바뀐 것도 여기로 온다. 지형 원점이 따라 움직인다. 같은 에셋이면 저장하지 않은 편집은 넘긴다.
        (void)reloadTerrainKeepingEdits();
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
            // 다시 읽기는 배치를 레지스트리에서 빼고 넣는 구조 변경이다. 틱 뒤로 미루고, 그 사이 사라졌을 수 있으니 핸들로 다시 찾는다.
            pManager->executeOrDeferPostTickOnComponent( *this, []( TerrainComponent& terrain )
            {
                (void)terrain.reloadTerrainKeepingEdits(); // 경로가 없으면 지형 없이 남고, 읽기 실패는 HeightfieldData 가 오류로 남긴다
            } );
            return;
        }
        (void)rebuildStaleChunkMeshes( TerrainComponentInternal::kMaxChunkMeshRebuildsPerFrame );
        float3     viewPosition{};
        const bool bHasChunks = pManager != nullptr && _listChunk.empty() == false;
        if ( bHasChunks && EnvironmentUtil::findViewPosition( *pManager, viewPosition ) )
            (void)updateLODs( viewPosition );
    }

    uint32 TerrainComponent::rebuildStaleChunkMeshes( uint32 maxCount )
    {
        vector<uint32> listRebuild;
        size_t         index{ 0 };
        for ( ; index < _listStaleChunk.size() && listRebuild.size() < maxCount; ++index )
        {
            const uint32 chunkIndex = _listStaleChunk[index];
            // LOD 교체가 먼저 다시 만들었으면 이미 최신이다.
            if ( chunkIndex >= _listChunk.size() || _listChunk[chunkIndex]._bMeshStale == false )
                continue;
            listRebuild.push_back( chunkIndex );
        }
        _listStaleChunk.erase( _listStaleChunk.begin(), _listStaleChunk.begin() + static_cast<ptrdiff_t>( index ) );
        rebuildChunkMeshes( listRebuild );
        return static_cast<uint32>( listRebuild.size() );
    }

    void TerrainComponent::setHeightRange( float32 heightMin, float32 heightMax )
    {
        _heightMin = heightMin;
        _heightMax = heightMax;
    }

    bool TerrainComponent::reloadTerrainKeepingEdits()
    {
        // 같은 에셋을 다시 읽으면 저장하지 않은 편집을 넘긴다(헤더 설명). 다시 읽은 크기가 맞지 않으면 restoreState 가 알리고 디스크 값으로 둔다.
        vector<uint8> listCarriedEdit;
        const bool    bCarryEdits = hasWrittenEdits() && _heightfieldPath == _loadedHeightfieldPath;
        if ( bCarryEdits )
        {
            Archive archive;
            writeState( archive );
            listCarriedEdit.assign( archive.getData(), archive.getData() + archive.getSize() );
        }
        const bool bLoaded = reloadTerrain();
        if ( bLoaded && listCarriedEdit.empty() == false )
            restoreState( std::move( listCarriedEdit ) );
        return bLoaded;
    }

    bool TerrainComponent::reloadTerrain()
    {
        releaseChunks();
        _heightfield.shutdown();
        _listChunkGeneration.clear();
        _listChunkSplatRevision.clear();
        ++_heightfieldGeneration; // 실패해도 오른다 — 비워진 높이장으로 만든 바디도 낡았다
        _splatRevision       = 0;
        _bHeightsWritten     = false;
        _bHolesWritten       = false;
        _bResolutionReplaced = false;
        releaseCPUSplat(); // 칠한 것은 버리고 디스크 가중치로 다시 만든다
        if ( _heightfieldPath.empty() )
            return false;

        HeightfieldData data;
        _loadedHeightfieldPath = _heightfieldPath;
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
        if ( loadSplatWeights() == false )
        {
            _heightfield.shutdown();
            return false;
        }
        acquireMaterial();
        createChunks();
        _listChunkGeneration.assign( _layout.getChunkCount(), _heightfieldGeneration );
        _listChunkSplatRevision.assign( _layout.getChunkCount(), 0u );
        return true;
    }

    bool TerrainComponent::writeHeights( const vector<float32>& listHeight, const TerrainSampleRect& rect )
    {
        if ( _listChunk.empty() || _heightfield.writeHeightSamples( listHeight, rect ) == false )
            return false;
        using Internal   = TerrainComponentInternal;
        _bHeightsWritten = true;
        // 충돌은 바뀐 샘플을 가진 청크만 낡는다(청크 경계 샘플은 양쪽 청크가 쓴다). 메시는 노멀이 이웃 샘플을 읽으므로 한 샘플 바깥까지 다시 만든다.
        const uint32      last = _heightfield.getResolution() - 1;
        TerrainSampleRect grown{};
        grown.include( rect._minX > 0 ? rect._minX - 1 : 0, rect._minZ > 0 ? rect._minZ - 1 : 0 );
        grown.include( MathUtil::min( rect._maxX + 1, last ), MathUtil::min( rect._maxZ + 1, last ) );
        markChunksEdited( Internal::findChunksOfSamples( _layout, rect ), Internal::findChunksOfSamples( _layout, grown ), rect );
        return true;
    }

    bool TerrainComponent::writeHoles( const vector<uint8>& listHole, const TerrainSampleRect& cellRect )
    {
        if ( _listChunk.empty() || _heightfield.writeHoleCells( listHole, cellRect ) == false )
            return false;
        _bHolesWritten = true;
        // 셀 c 는 청크 c / 칸 하나에만 든다 — 충돌과 메시 모두 그 청크만 낡는다(노멀은 구멍을 보지 않는다).
        const uint32      cells = _layout._chunkCells;
        TerrainSampleRect chunkRect{};
        chunkRect.include( cellRect._minX / cells, cellRect._minZ / cells );
        chunkRect.include( cellRect._maxX / cells, cellRect._maxZ / cells );
        TerrainSampleRect sampleRect{};
        sampleRect.include( cellRect._minX, cellRect._minZ );
        sampleRect.include( cellRect._maxX + 1, cellRect._maxZ + 1 );
        markChunksEdited( chunkRect, chunkRect, sampleRect );
        return true;
    }

    void TerrainComponent::markChunksEdited( const TerrainSampleRect& generationChunks, const TerrainSampleRect& meshChunks, const TerrainSampleRect& sampleRect )
    {
        using Internal = TerrainComponentInternal;
        ++_heightfieldGeneration;
        for ( uint32 chunkZ = generationChunks._minZ; chunkZ <= generationChunks._maxZ; ++chunkZ )
        {
            for ( uint32 chunkX = generationChunks._minX; chunkX <= generationChunks._maxX; ++chunkX )
            {
                _listChunkGeneration[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX] = _heightfieldGeneration;
            }
        }
        for ( uint32 chunkZ = meshChunks._minZ; chunkZ <= meshChunks._maxZ; ++chunkZ )
        {
            for ( uint32 chunkX = meshChunks._minX; chunkX <= meshChunks._maxX; ++chunkX )
            {
                const uint32 chunkIndex = chunkZ * _layout._chunkCountX + chunkX;
                Chunk&       chunk      = _listChunk[chunkIndex];
                chunk._batch->setBoundsRadius( 0, TerrainMeshBuilder::computeChunkBoundsRadius( _heightfield, _layout, chunkX, chunkZ ) );
                if ( chunk._bMeshStale == false )
                    _listStaleChunk.push_back( chunkIndex );
                chunk._bMeshStale = true;
            }
        }
        // 큰 사각형은 청크 메시를 프레임마다 나눠 다시 만든다(남은 것은 틱이 이어서). 브러시 한 번(청크 넷 이하)은 바로 끝난다.
        (void)rebuildStaleChunkMeshes( Internal::kMaxChunkMeshRebuildsPerFrame );
        invalidateNavigation( sampleRect );
    }

    void TerrainComponent::invalidateNavigation( const TerrainSampleRect& sampleRect )
    {
        // 내비메시는 바뀐 자리의 타일만 워커가 다시 베이크한다(타일은 XZ 로 고르므로 높이는 범위 전체로 둔다).
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const float3 low  = _heightfield.getSamplePosition( sampleRect._minX, sampleRect._minZ );
        const float3 high = _heightfield.getSamplePosition( sampleRect._maxX, sampleRect._maxZ );
        const AABB   box{
            float3{ low._x, _heightfield.getOrigin()._y + _heightMin,  low._z},
            float3{high._x, _heightfield.getOrigin()._y + _heightMax, high._z}
        };
        pManager->getSubsystem<SceneNavigation>().invalidateArea( box, true );
    }

    bool TerrainComponent::isResolutionUsable( uint32 resolution ) const
    {
        TerrainChunkLayout layout{};
        return resolution >= 2 && TerrainMeshBuilder::makeLayout( resolution, _chunkCells, layout );
    }

    bool TerrainComponent::replaceHeightfield( uint32 resolution, const vector<float32>& listHeight, const vector<uint8>& listHole )
    {
        TerrainChunkLayout layout{};
        const bool         bLayoutFits = resolution >= 2 && TerrainMeshBuilder::makeLayout( resolution, _chunkCells, layout );
        if ( _listChunk.empty() )
            return false;
        if ( bLayoutFits == false )
        {
            SW_LOG_ERROR( "Terrain '%#': resolution %# does not divide into chunks of %# cells (resolution - 1 must be a multiple)", _heightfieldPath.c_str(), resolution, _chunkCells );
            return false;
        }
        if ( _heightfield.replaceSamples( resolution, listHeight, listHole ) == false )
        {
            SW_LOG_ERROR( "Terrain '%#': %# heights and %# hole cells do not fit resolution %#", _heightfieldPath.c_str(), static_cast<uint32>( listHeight.size() ),
                          static_cast<uint32>( listHole.size() ), resolution );
            return false;
        }
        _layout              = layout;
        _bHeightsWritten     = true;
        _bHolesWritten       = true;
        _bResolutionReplaced = true;
        ++_heightfieldGeneration;
        createChunks();
        _listChunkGeneration.assign( _layout.getChunkCount(), _heightfieldGeneration );
        // 청크 배치가 바뀌었다 — 스플랫으로 만든 것도 모든 청크가 바뀐 것으로 친다(`findEditedArea`).
        _listChunkSplatRevision.assign( _layout.getChunkCount(), _splatRevision );
        TerrainSampleRect all{};
        all.include( 0, 0 );
        all.include( resolution - 1, resolution - 1 );
        invalidateNavigation( all );
        return true;
    }

    bool TerrainComponent::resampleHeightfield( uint32 resolution )
    {
        const uint32 current = _heightfield.getResolution();
        if ( _heightfield.isValid() == false || resolution == current )
            return false;
        vector<float32> listHeight;
        vector<uint8>   listHole;
        if ( TerrainHeightfield::resampleHeights( _heightfield.getHeightSamples(), current, resolution, listHeight ) == false ||
             TerrainHeightfield::resampleHoles( _heightfield.getHoleCells(), current, resolution, listHole ) == false )
            return false;
        return replaceHeightfield( resolution, listHeight, listHole );
    }

    bool TerrainComponent::writeLayerWeights( const vector<float4>& listWeight, const TerrainSampleRect& rect )
    {
        if ( _heightfield.writeSplatWeights( listWeight, rect ) == false )
            return false;
        ++_splatRevision;
        markSplatChunks( rect );
        // 엔진 서비스가 없으면(CPU 테스트) 텍스처가 없고 CPU 값만 바뀐다.
        if ( _cpuSplatPath.empty() )
            return true;
        TextureCache&        textures   = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
        const uint32         splatWidth = _heightfield.getSplatWidth();
        const vector<uint8>& listByte   = _heightfield.getSplatBytes();
        // 바뀐 사각형만 한 번에 올린다(렌더 스레드가 다음 프레임 기록 전에 올린다).
        const uint32  width  = rect._maxX - rect._minX + 1;
        const uint32  height = rect._maxZ - rect._minZ + 1;
        vector<uint8> rgbaBytes( static_cast<size_t>( width ) * height * 4 );
        for ( uint32 row = 0; row < height; ++row )
        {
            const size_t source = ( static_cast<size_t>( rect._minZ + row ) * splatWidth + rect._minX ) * 4;
            Memory::copy( rgbaBytes.data() + static_cast<size_t>( row ) * width * 4, listByte.data() + source, static_cast<size_t>( width ) * 4 );
        }
        textures.requestRegionUpdate( _cpuSplatPath, rect._minX, rect._minZ, width, height, std::move( rgbaBytes ) );
        return true;
    }

    void TerrainComponent::markSplatChunks( const TerrainSampleRect& texelRect )
    {
        using Internal           = TerrainComponentInternal;
        const uint32 chunkCount  = _layout.getChunkCount();
        const uint32 splatWidth  = _heightfield.getSplatWidth();
        const uint32 splatHeight = _heightfield.getSplatHeight();
        const bool   bTracked    = chunkCount > 0 && _listChunkSplatRevision.size() == chunkCount && splatWidth > 0 && splatHeight > 0 && texelRect.isEmpty() == false;
        if ( bTracked == false )
            return;
        // 쌍선형은 한 텍셀 바깥까지 읽는다. 텍셀 t 는 지형 정규 좌표 t / (너비 - 1) 이고 샘플로는 그 × (해상도 - 1) 이다.
        const uint32      lastSample = _heightfield.getResolution() - 1;
        const uint32      lastTexelX = splatWidth - 1;
        const uint32      lastTexelZ = splatHeight - 1;
        const uint32      minTexelX  = texelRect._minX > 0 ? texelRect._minX - 1 : 0u;
        const uint32      minTexelZ  = texelRect._minZ > 0 ? texelRect._minZ - 1 : 0u;
        const uint32      maxTexelX  = MathUtil::min( texelRect._maxX + 1, lastTexelX );
        const uint32      maxTexelZ  = MathUtil::min( texelRect._maxZ + 1, lastTexelZ );
        TerrainSampleRect sampleRect{};
        sampleRect.include( lastTexelX > 0 ? minTexelX * lastSample / lastTexelX : 0u, lastTexelZ > 0 ? minTexelZ * lastSample / lastTexelZ : 0u );
        sampleRect.include( lastTexelX > 0 ? ( maxTexelX * lastSample + lastTexelX - 1 ) / lastTexelX : lastSample,
                            lastTexelZ > 0 ? ( maxTexelZ * lastSample + lastTexelZ - 1 ) / lastTexelZ : lastSample );
        const TerrainSampleRect chunkRect = Internal::findChunksOfSamples( _layout, sampleRect );
        for ( uint32 chunkZ = chunkRect._minZ; chunkZ <= chunkRect._maxZ; ++chunkZ )
        {
            for ( uint32 chunkX = chunkRect._minX; chunkX <= chunkRect._maxX; ++chunkX )
            {
                _listChunkSplatRevision[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX] = _splatRevision;
            }
        }
    }

    bool TerrainComponent::findEditedArea( uint32 sinceGeneration, uint32 sinceSplatRevision, float2& outMin, float2& outMax ) const
    {
        using Internal          = TerrainComponentInternal;
        const uint32 chunkCount = _layout.getChunkCount();
        const bool   bTracked   = _heightfield.isValid() && chunkCount > 0 && _listChunkGeneration.size() == chunkCount && _listChunkSplatRevision.size() == chunkCount;
        if ( bTracked == false )
            return false;
        TerrainSampleRect chunkRect{};
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const bool bEdited = _listChunkGeneration[chunkIndex] > sinceGeneration || _listChunkSplatRevision[chunkIndex] > sinceSplatRevision;
            if ( bEdited )
                chunkRect.include( chunkIndex % _layout._chunkCountX, chunkIndex / _layout._chunkCountX );
        }
        if ( chunkRect.isEmpty() )
            return false;
        const float3  origin   = _heightfield.getOrigin();
        const float2  cellSize = _heightfield.getCellSize();
        const float32 cells    = static_cast<float32>( _layout._chunkCells );
        const float32 margin   = static_cast<float32>( Internal::kEditedAreaMarginCells );
        outMin                 = float2{ origin._x + ( static_cast<float32>( chunkRect._minX ) * cells - margin ) * cellSize._x,
                         origin._z + ( static_cast<float32>( chunkRect._minZ ) * cells - margin ) * cellSize._y };
        outMax                 = float2{ origin._x + ( static_cast<float32>( chunkRect._maxX + 1 ) * cells + margin ) * cellSize._x,
                         origin._z + ( static_cast<float32>( chunkRect._maxZ + 1 ) * cells + margin ) * cellSize._y };
        return true;
    }

    void TerrainComponent::releaseCPUSplat()
    {
        if ( _cpuSplatPath.empty() )
            return;
        if ( engine::areEngineServicesBound() )
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().releaseCPUTexture( _cpuSplatPath );
        _cpuSplatPath.clear();
    }

    bool TerrainComponent::saveHeightfield( string_view filePath ) const
    {
        if ( _heightfield.isValid() == false )
            return false;
        HeightfieldData data;
        _heightfield.makeHeightfieldData( _heightMin, _heightMax, data );
        return data.saveToFile( filePath );
    }

    bool TerrainComponent::saveSplat( string_view filePath ) const
    {
        if ( _heightfield.getSplatWidth() == 0 )
            return false;
        LayerWeightsData data;
        data._width  = _heightfield.getSplatWidth();
        data._height = _heightfield.getSplatHeight();
        data._bytes  = _heightfield.getSplatBytes();
        return data.saveToFile( filePath );
    }

    void TerrainComponent::writeState( Archive& outArchive ) const
    {
        outArchive << TerrainComponentInternal::kStateVersion;
        const bool bHeights = _bHeightsWritten && _heightfield.isValid();
        outArchive << bHeights;
        if ( bHeights )
        {
            const vector<float32>& listHeight = _heightfield.getHeightSamples();
            outArchive << _heightfield.getResolution();
            outArchive << _bResolutionReplaced;
            outArchive << static_cast<uint32>( listHeight.size() );
            outArchive.writeBytes( listHeight.data(), listHeight.size() * sizeof( float32 ) );
        }
        const bool bHoles = _bHolesWritten && _heightfield.isValid();
        outArchive << bHoles;
        if ( bHoles )
        {
            outArchive << _heightfield.getResolution();
            outArchive << _heightfield.getHoleCells(); // 비었으면 구멍 없음
        }
        const bool bSplat = _splatRevision > 0 && _heightfield.getSplatWidth() > 0;
        outArchive << bSplat;
        if ( bSplat )
        {
            outArchive << _heightfield.getSplatWidth();
            outArchive << _heightfield.getSplatHeight();
            outArchive << _heightfield.getSplatBytes();
        }
    }

    void TerrainComponent::restoreState( vector<uint8>&& bytes )
    {
        Archive archive( bytes.data(), static_cast<uint64>( bytes.size() ) );
        uint32  version{ 0 };
        bool    bHeights{ false };
        archive >> version >> bHeights;
        if ( archive.isError() || version != TerrainComponentInternal::kStateVersion )
        {
            SW_LOG_WARNING( "Terrain '%#': unsaved edit state (version %#) did not read - keeping the asset values", _heightfieldPath.c_str(), version );
            return;
        }
        if ( bHeights )
        {
            uint32 resolution{ 0 };
            uint32 sampleCount{ 0 };
            bool   bResized{ false };
            archive >> resolution >> bResized >> sampleCount;
            // 크기를 먼저 비교한다 — 맞지 않는 수로 버퍼를 잡지 않는다. 해상도가 달라도 되는 것은 크기를 바꾼 편집(`replaceHeightfield`)뿐이다 —
            // 디스크 에셋을 다른 해상도로 다시 가져왔으면 낡은 편집이 그것을 덮지 않는다.
            const uint32    current    = _heightfield.getResolution();
            const bool      bSameSize  = resolution == current;
            const bool      bSizeKnown = bSameSize || ( bResized && isResolutionUsable( resolution ) );
            const bool      bFits      = archive.isOk() && bSizeKnown && static_cast<uint64>( sampleCount ) == static_cast<uint64>( resolution ) * resolution;
            vector<float32> listHeight( bFits ? sampleCount : 0u );
            const bool      bRead    = bFits && archive.readBytes( listHeight.data(), static_cast<uint64>( sampleCount ) * sizeof( float32 ) );
            bool            bWritten = false;
            if ( bRead && bSameSize )
            {
                TerrainSampleRect all{};
                all.include( 0, 0 );
                all.include( resolution - 1, resolution - 1 );
                bWritten = writeHeights( listHeight, all );
            }
            else if ( bRead )
            {
                vector<uint8> listHole;
                bWritten = TerrainHeightfield::resampleHoles( _heightfield.getHoleCells(), current, resolution, listHole ) && replaceHeightfield( resolution, listHeight, listHole );
            }
            if ( bWritten == false )
            {
                SW_LOG_WARNING( "Terrain '%#': unsaved height edits (%#x%#) do not fit the reloaded heightfield - keeping the asset values", _heightfieldPath.c_str(), resolution, resolution );
                return;
            }
        }
        bool bHoles{ false };
        archive >> bHoles;
        if ( bHoles )
        {
            uint32        resolution{ 0 };
            vector<uint8> listHole;
            archive >> resolution >> listHole;
            const uint32 cells    = resolution >= 2 ? resolution - 1 : 0u;
            const bool   bSizeOk  = listHole.empty() || listHole.size() == static_cast<size_t>( cells ) * cells;
            const bool   bFits    = archive.isOk() && cells > 0 && resolution == _heightfield.getResolution() && bSizeOk;
            bool         bWritten = false;
            if ( bFits )
            {
                // 빈 마스크는 구멍 없음이다 — 다시 읽은 에셋에 구멍이 있어도 모두 0 으로 쓴다.
                vector<uint8>     listAll = listHole.empty() ? vector<uint8>( static_cast<size_t>( cells ) * cells, 0u ) : std::move( listHole );
                TerrainSampleRect all{};
                all.include( 0, 0 );
                all.include( cells - 1, cells - 1 );
                bWritten = writeHoles( listAll, all );
            }
            if ( bWritten == false )
            {
                SW_LOG_WARNING( "Terrain '%#': unsaved hole edits (%#x%#) do not fit the reloaded heightfield - keeping the asset values", _heightfieldPath.c_str(), resolution, resolution );
                return;
            }
        }
        bool bSplat{ false };
        archive >> bSplat;
        if ( bSplat == false )
            return;
        uint32        width{ 0 };
        uint32        height{ 0 };
        vector<uint8> splatBytes;
        archive >> width >> height >> splatBytes;
        const size_t texelCount = static_cast<size_t>( width ) * height;
        const bool   bFits      = archive.isOk() && texelCount > 0 && width == _heightfield.getSplatWidth() && height == _heightfield.getSplatHeight();
        if ( bFits == false || splatBytes.size() != texelCount * 4 )
        {
            SW_LOG_WARNING( "Terrain '%#': unsaved splat edits (%#x%#) do not fit the reloaded splat - keeping the asset values", _heightfieldPath.c_str(), width, height );
            return;
        }
        // 바이트를 그대로 되돌리도록 가중치는 정규화하지 않는다(`writeLayerWeights` 는 x 255 를 반올림한다).
        vector<float4> listWeight( texelCount );
        for ( size_t texel = 0; texel < texelCount; ++texel )
        {
            const uint8* pTexel = &splatBytes[texel * 4];
            listWeight[texel]   = float4{ static_cast<float32>( pTexel[0] ) / 255.0f, static_cast<float32>( pTexel[1] ) / 255.0f, static_cast<float32>( pTexel[2] ) / 255.0f,
                                        static_cast<float32>( pTexel[3] ) / 255.0f };
        }
        TerrainSampleRect all{};
        all.include( 0, 0 );
        all.include( width - 1, height - 1 );
        if ( writeLayerWeights( listWeight, all ) == false )
            SW_LOG_WARNING( "Terrain '%#': could not write back the unsaved splat edits", _heightfieldPath.c_str() );
    }

    bool TerrainComponent::loadSplatWeights()
    {
        _heightfield.setSplat( 0, 0, {} );
        // 경로가 비었으면 이 지형은 레이어를 칠하지 않는다(데이터가 정한 것이다). 정한 가중치를 읽지 못하면 레이어 0 으로 두지 않고 지형을 읽지 않는다.
        if ( _layerWeightsPath.empty() )
            return true;
        LayerWeightsData data;
        if ( data.loadFromResource( _layerWeightsPath ) == false )
        {
            SW_LOG_ERROR( "Terrain '%#': layer weights '%#' did not load - the terrain is not loaded (expected a %# asset)", _heightfieldPath.c_str(), _layerWeightsPath.c_str(),
                          string( LayerWeightsData::kExtension ).c_str() );
            return false;
        }
        _heightfield.setSplat( data._width, data._height, data._bytes );
        // 그리는 호스트는 이 바이트로 자기 GPU 텍스처를 만든다(칠하기도 이 텍스처를 고친다). 엔진 서비스가 없으면(CPU 테스트) CPU 값만 둔다.
        if ( engine::areEngineServicesBound() == false )
            return true;
        string path = TerrainComponentInternal::makeCPUSplatPath();
        if ( engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().createCPUTexture( path, data._width, data._height, std::move( data._bytes ) ) == false )
        {
            SW_LOG_ERROR( "Terrain '%#': could not create the splat texture for '%#' (%#x%#)", _heightfieldPath.c_str(), _layerWeightsPath.c_str(), data._width, data._height );
            return false;
        }
        _cpuSplatPath = std::move( path );
        return true;
    }

    void TerrainComponent::acquireMaterial()
    {
        using Internal = TerrainComponentInternal;
        // 칸이 비면 엔진 기본 지형 머티리얼이다(값의 기준은 엔진 기본 에셋 표).
        const string& materialPath = _materialPath.empty() ? engine::getSubsystem<EngineDefaultAssets>()._terrainMaterial.get() : _materialPath;
        if ( _material.acquire( materialPath ) == false )
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
        // x 는 스플랫 너비(텍셀 중심 맞추기), y 는 디테일 반복(1/m), z 는 절벽 임계값(노멀 y), w 는 절벽 레이어(-1 이면 끔)다.
        const float32 splatWidth = static_cast<float32>( _heightfield.getSplatWidth() );
        _material.setVector( hashed_string( "terrainParams" ), float4{ splatWidth, _detailTiling, _cliffStart, static_cast<float32>( _cliffLayer ) } );
        _material.setTexture( hashed_string( "splatMap" ), _cpuSplatPath );
        _material.setTexture( hashed_string( "detailMap" ), _detailTexturePath );
    }

    void TerrainComponent::releaseChunks()
    {
        // 배치의 소멸자가 레지스트리에서 뺀다.
        _listChunk.clear();
        _listStaleChunk.clear();
    }

    void TerrainComponent::createChunks()
    {
        releaseChunks();
        const uint32 chunkCount = _layout.getChunkCount();
        _listChunk.resize( chunkCount );
        vector<uint32> listAllChunk( chunkCount );
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const uint32 chunkX = chunkIndex % _layout._chunkCountX;
            const uint32 chunkZ = chunkIndex / _layout._chunkCountX;
            Chunk&       chunk  = _listChunk[chunkIndex];
            chunk._lod          = 0;
            for ( uint32& neighborLOD : chunk._arrNeighborLOD )
            {
                neighborLOD = 0;
            }
            chunk._batch = sw::make_unique<MeshInstanceBatch>( Mesh::create(), _material.getMaterial(), _material.getInstance(), 1u, MeshInstanceWriter::CPU );
            chunk._batch->setOwnerComponent( this );
            chunk._batch->setWorld( 0, float4x4::makeTranslation( TerrainMeshBuilder::computeChunkTranslation( _heightfield, _layout, chunkX, chunkZ ) ) );
            chunk._batch->setBoundsRadius( 0, TerrainMeshBuilder::computeChunkBoundsRadius( _heightfield, _layout, chunkX, chunkZ ) );
            listAllChunk[chunkIndex] = chunkIndex;
        }
        // 메시를 모두 채운 뒤에 등록한다 — 빈 메시 배치가 레지스트리에 한 번도 보이지 않는다.
        rebuildChunkMeshes( listAllChunk );
        if ( _pPrimitiveRegistry == nullptr )
            return;
        for ( Chunk& chunk : _listChunk )
        {
            _pPrimitiveRegistry->addInstanceBatch( chunk._batch.get() );
        }
    }

    void TerrainComponent::rebuildChunkMeshes( const vector<uint32>& listChunkIndex )
    {
        using Internal            = TerrainComponentInternal;
        const uint32 rebuildCount = static_cast<uint32>( listChunkIndex.size() );
        if ( rebuildCount == 0 )
            return;
        vector<Internal::ChunkMeshRequest> listRequest( rebuildCount );
        vector<vector<RHIVertex>>          listChunkVertex( rebuildCount );
        for ( uint32 index = 0; index < rebuildCount; ++index )
        {
            const Chunk&                chunk   = _listChunk[listChunkIndex[index]];
            Internal::ChunkMeshRequest& request = listRequest[index];
            request._chunkIndex                 = listChunkIndex[index];
            request._lod                        = chunk._lod;
            for ( uint32 side = 0; side < static_cast<uint32>( TerrainChunkSide::Count ); ++side )
            {
                request._arrNeighborLOD[side] = chunk._arrNeighborLOD[side];
            }
        }
        Internal::ChunkVertexJob job{};
        job._pHeightfield = &_heightfield;
        job._pLayout      = &_layout;
        job._pRequest     = listRequest.data();
        job._pListVertex  = listChunkVertex.data();
        engine::runParallel( rebuildCount, Internal::kParallelChunkMeshCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &Internal::ChunkVertexJob::run, &job ) );

        // 메시 생성과 배치 교체는 레지스트리 더티 표시를 건드리므로 이 스레드에서 한다.
        for ( uint32 index = 0; index < rebuildCount; ++index )
        {
            Chunk& chunk          = _listChunk[listChunkIndex[index]];
            chunk._vertexCount    = static_cast<uint32>( listChunkVertex[index].size() );
            chunk._bMeshStale     = false;
            shared_ptr<Mesh> mesh = Mesh::create();
            mesh->setVertices( std::move( listChunkVertex[index] ) );
            chunk._batch->setMesh( std::move( mesh ) );
        }
    }

    void TerrainComponent::fillNeighborLODs( uint32 chunkX, uint32 chunkZ, const vector<uint32>& listLOD, uint32 ( &outArrLOD )[4] ) const
    {
        const uint32 ownLOD                                       = listLOD[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX];
        outArrLOD[static_cast<uint32>( TerrainChunkSide::West )]  = chunkX > 0 ? listLOD[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX - 1] : ownLOD;
        outArrLOD[static_cast<uint32>( TerrainChunkSide::East )]  = chunkX + 1 < _layout._chunkCountX ? listLOD[static_cast<size_t>( chunkZ ) * _layout._chunkCountX + chunkX + 1] : ownLOD;
        outArrLOD[static_cast<uint32>( TerrainChunkSide::South )] = chunkZ > 0 ? listLOD[static_cast<size_t>( chunkZ - 1 ) * _layout._chunkCountX + chunkX] : ownLOD;
        outArrLOD[static_cast<uint32>( TerrainChunkSide::North )] = chunkZ + 1 < _layout._chunkCountZ ? listLOD[static_cast<size_t>( chunkZ + 1 ) * _layout._chunkCountX + chunkX] : ownLOD;
    }

    uint32 TerrainComponent::updateLODs( const float3& viewPosition )
    {
        using Internal          = TerrainComponentInternal;
        const uint32 chunkCount = static_cast<uint32>( _listChunk.size() );
        _listWantedLOD.resize( chunkCount );
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const uint32  chunkX     = chunkIndex % _layout._chunkCountX;
            const uint32  chunkZ     = chunkIndex / _layout._chunkCountX;
            const float32 distance   = Internal::computeChunkDistance( _heightfield, _layout, chunkX, chunkZ, viewPosition );
            const uint32  currentLOD = _listChunk[chunkIndex]._lod;
            uint32        wantedLOD  = TerrainMeshBuilder::selectLOD( distance, _lodDistance, _layout._maxLOD );
            // 임계값 근처에서 흔들리지 않게 한다. 바꾸는 쪽으로 10 % 더 넘어야 바꾼다.
            if ( wantedLOD != currentLOD )
            {
                const float32 margin = wantedLOD > currentLOD ? ( 1.0f - Internal::kLODHysteresis ) : ( 1.0f + Internal::kLODHysteresis );
                if ( TerrainMeshBuilder::selectLOD( distance * margin, _lodDistance, _layout._maxLOD ) != wantedLOD )
                    wantedLOD = currentLOD;
            }
            _listWantedLOD[chunkIndex] = wantedLOD;
        }

        vector<uint32> listRebuild;
        for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            Chunk&       chunk  = _listChunk[chunkIndex];
            const uint32 chunkX = chunkIndex % _layout._chunkCountX;
            const uint32 chunkZ = chunkIndex / _layout._chunkCountX;
            uint32       arrNeighborLOD[4]{};
            fillNeighborLODs( chunkX, chunkZ, _listWantedLOD, arrNeighborLOD );
            bool bChanged = chunk._lod != _listWantedLOD[chunkIndex];
            for ( uint32 side = 0; side < 4; ++side )
            {
                // 이웃이 이쪽보다 고우면 접는 것은 이웃의 일이다. 그 변은 이쪽 메시에 영향이 없다.
                const uint32 oldEffective   = MathUtil::max( chunk._arrNeighborLOD[side], chunk._lod );
                const uint32 newEffective   = MathUtil::max( arrNeighborLOD[side], _listWantedLOD[chunkIndex] );
                bChanged                    = bChanged || oldEffective != newEffective;
                chunk._arrNeighborLOD[side] = arrNeighborLOD[side];
            }
            chunk._lod = _listWantedLOD[chunkIndex];
            if ( bChanged == false )
                continue;
            listRebuild.push_back( chunkIndex );
        }
        // 고리를 넘는 프레임에 바뀐 청크를 한꺼번에 나눠 만든다(이웃 이음매가 같은 프레임에 맞는다).
        rebuildChunkMeshes( listRebuild );
        return static_cast<uint32>( listRebuild.size() );
    }

    uint32 TerrainComponent::getChunkLOD( uint32 chunkX, uint32 chunkZ ) const
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
