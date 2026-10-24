#include "pch.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"
#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "FoliageComponent" );

    namespace
    {
        struct FoliageComponentInternal
        {
            static const hashed_string& getInfluencerName( uint32 slot )
            {
                static const hashed_string s_arrName[FoliageInfluencerComponent::kMaxInfluencerCount] = {
                    hashed_string( "influencer0" ), hashed_string( "influencer1" ), hashed_string( "influencer2" ), hashed_string( "influencer3" ) };
                return s_arrName[slot];
            }

            /** @brief 메시 원점(인스턴스 위치)에서 정점까지의 가장 먼 거리입니다. 바운드 중심이 인스턴스 위치라 그만큼 덮어야 합니다. */
            static float32 computeOriginRadius( const Mesh& mesh )
            {
                const float3& boundsMin = mesh.getLocalBoundsMin();
                const float3& boundsMax = mesh.getLocalBoundsMax();
                const float3  farCorner{ MathUtil::max( MathUtil::abs( boundsMin._x ), MathUtil::abs( boundsMax._x ) ),
                                        MathUtil::max( MathUtil::abs( boundsMin._y ), MathUtil::abs( boundsMax._y ) ),
                                        MathUtil::max( MathUtil::abs( boundsMin._z ), MathUtil::abs( boundsMax._z ) ) };
                return MathUtil::max( farCorner.getLength(), 0.01f );
            }

            static shared_ptr<Mesh> acquireMesh( const string& meshID )
            {
                return MeshAssetFormat::isMeshAssetPath( meshID ) ? MeshCache::acquire( meshID ) : MeshUtil::acquirePrimitive( meshID );
            }

            /** @brief 레이어 메시들의 비중(배치의 항목 비중)입니다. */
            static vector<float32> makeMeshWeights( const FoliageLayer& layer )
            {
                vector<float32> listWeight;
                listWeight.reserve( layer._listMesh.size() );
                for ( const FoliageMesh& mesh : layer._listMesh )
                {
                    listWeight.push_back( mesh._weight );
                }
                return listWeight;
            }

            /** @brief 레이어의 메시를 얻어 메시마다 원점 반지름과 함께 채웁니다. 얻지 못한 메시는 nullptr 로 두고(그 인스턴스는 건너뛴다) 경고한다. */
            static void acquireLayerMeshes( const FoliageLayer& layer, vector<shared_ptr<Mesh>>& outListMesh, vector<float32>& outListRadius )
            {
                for ( const FoliageMesh& foliageMesh : layer._listMesh )
                {
                    shared_ptr<Mesh> mesh = acquireMesh( foliageMesh._meshID );
                    if ( mesh == nullptr )
                        SW_LOG_WARNING( "Foliage layer '%#': mesh '%#' could not be acquired - its instances are skipped", layer._name.c_str(), foliageMesh._meshID.c_str() );
                    outListRadius.push_back( mesh != nullptr ? computeOriginRadius( *mesh ) : 0.0f );
                    outListMesh.push_back( std::move( mesh ) );
                }
            }

            /** @brief 인스턴스를 메시마다 셀마다 묶습니다. 키는 메시 번호 × 2^48 + 셀 (x, z) 입니다. 메시를 얻지 못한 인스턴스는 뺀다. */
            static unordered_map<uint64, vector<uint32>> groupByCell( const vector<PlacementInstance>& listInstance, const vector<shared_ptr<Mesh>>& listMesh, float32 cellSize )
            {
                unordered_map<uint64, vector<uint32>> mapCellToInstance;
                for ( uint32 instanceIndex = 0; instanceIndex < listInstance.size(); ++instanceIndex )
                {
                    const PlacementInstance& instance            = listInstance[instanceIndex];
                    const bool               bMissingFoliageMesh = instance._entryIndex < 0 || static_cast<size_t>( instance._entryIndex ) >= listMesh.size() || listMesh[static_cast<size_t>( instance._entryIndex )] == nullptr;
                    if ( bMissingFoliageMesh )
                        continue;
                    const int32  cellX = static_cast<int32>( MathUtil::floor( instance._planePosition._x / cellSize ) );
                    const int32  cellZ = static_cast<int32>( MathUtil::floor( instance._planePosition._y / cellSize ) );
                    const uint64 key   = ( static_cast<uint64>( instance._entryIndex ) << 48 ) ^ ( static_cast<uint64>( static_cast<uint16>( cellX ) ) << 16 ) ^
                                       static_cast<uint64>( static_cast<uint16>( cellZ ) );
                    mapCellToInstance[key].push_back( instanceIndex );
                }
                return mapCellToInstance;
            }

            /** @brief 셀 키를 오름차순으로 냅니다. 새 셀을 이 순서로 등록해 등록 순서를 결정적으로 만든다(해시 맵 순회 순서는 실행마다 같다는 보장이 없다). */
            static vector<uint64> sortedKeys( const unordered_map<uint64, vector<uint32>>& mapCellToInstance )
            {
                vector<uint64> listKey;
                listKey.reserve( mapCellToInstance.size() );
                for ( const auto& [key, listIndex] : mapCellToInstance )
                {
                    listKey.push_back( key );
                }
                std::sort( listKey.begin(), listKey.end() );
                return listKey;
            }

            /** @brief 셀 하나의 인스턴스들(@p listIndex 순서)의 내용 해시입니다. 같으면 셀 배치를 다시 만들지 않는다. */
            static uint64 computeCellHash( const vector<PlacementInstance>& listInstance, const vector<uint32>& listIndex )
            {
                static_assert( sizeof( PlacementInstance ) == sizeof( float32 ) * 9 + sizeof( uint32 ) * 2, "PlacementInstance has padding - hash the fields instead of the bytes" );
                uint64 hash = HashUtil::kFnvOffset64;
                for ( const uint32 instanceIndex : listIndex )
                {
                    hash = HashUtil::appendBytes64( hash, &listInstance[instanceIndex], sizeof( PlacementInstance ) );
                }
                return hash;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FoliageComponent::FoliageComponent()
        : _listLayer{}
        , _listExclusion{}
        , _regionSize{ 0.0f, 0.0f }
        , _cellSize{ 32.0f }
        , _listCell{}
        , _listLayerMaterial{}
        , _listLayerCandidate{}
        , _builtRegion{}
        , _pPrimitiveRegistry{ nullptr }
        , _time{ 0.0f }
        , _terrainSettleTime{ 0.0f }
        , _instanceCount{ 0 }
        , _lastRebuiltBatchCount{ 0 }
        , _builtTerrainGeneration{ 0 }
        , _builtSplatRevision{ 0 }
        , _seenTerrainGeneration{ 0 }
        , _seenSplatRevision{ 0 }
        , _bBuiltOnTerrain{ SW_FALSE }
        , _bTerrainRefreshPending{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
        setTickGroup( TickGroup::PostUpdate );
    }

    FoliageComponent::~FoliageComponent()
    {
        releaseCells();
    }

    void FoliageComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
    }

    void FoliageComponent::onUnregister( GameObjectManager& manager )
    {
        releaseCells();
        _listLayerMaterial.clear();
        _listLayerCandidate.clear();
        _pPrimitiveRegistry = nullptr;
        SceneComponent::onUnregister( manager );
    }

    void FoliageComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        rebuildFoliage();
    }

    void FoliageComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        // 로드 순서상 지형이 뒤에 생성되면 처음 계산은 평면 위에서 했다. 모두 생성된 지금 지형을 다시 찾는다. 레지스트리를 바꾸므로 틱 뒤에 한다.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bBuiltOnTerrain == SW_TRUE )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( FoliageComponent& foliage )
        { foliage.rebuildFoliage(); } );
    }

    void FoliageComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        // 켜고 끄기는 배치를 바꾸지 않는다. 셀을 다시 만들지 않고 빌더가 다시 확인하게만 한다.
        static const hashed_string s_activeName( "_bActive" );
        if ( propertyName == s_activeName )
        {
            markCellsDirty();
            return;
        }
        rebuildFoliage();
    }

    void FoliageComponent::onOwnerActiveInHierarchyChanged()
    {
        SceneComponent::onOwnerActiveInHierarchyChanged();
        markCellsDirty();
    }

    void FoliageComponent::markCellsDirty()
    {
        for ( Cell& cell : _listCell )
        {
            if ( cell._batch != nullptr )
                cell._batch->markAllEntriesDirty();
        }
    }

    void FoliageComponent::onTick( float32 deltaTime )
    {
        SceneComponent::onTick( deltaTime );
        if ( EnvironmentUtil::isAnimationEnabled() )
            _time += deltaTime;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        float3             viewPosition{};
        const bool         bCanUpdateView = pManager != nullptr && _listCell.empty() == false && EnvironmentUtil::findViewPosition( *pManager, viewPosition );
        if ( bCanUpdateView )
            updateView( viewPosition );
        if ( pManager != nullptr && _bBuiltOnTerrain == SW_TRUE )
            followTerrainEdits( *pManager, deltaTime );
    }

    void FoliageComponent::followTerrainEdits( GameObjectManager& manager, float32 deltaTime )
    {
        const float3            origin   = getWorldPosition();
        const TerrainComponent* pTerrain = TerrainComponent::findTerrainAt( manager, origin._x, origin._z );
        if ( pTerrain == nullptr )
            return;
        const uint32 generation = pTerrain->getHeightfieldGeneration();
        const uint32 revision   = pTerrain->getSplatRevision();
        if ( generation != _seenTerrainGeneration || revision != _seenSplatRevision )
        {
            _seenTerrainGeneration = generation;
            _seenSplatRevision     = revision;
            _terrainSettleTime     = 0.0f;
            return;
        }
        const bool bStale = generation != _builtTerrainGeneration || revision != _builtSplatRevision;
        _terrainSettleTime += deltaTime;
        const bool bReadyToRefresh = bStale && kTerrainSettleSeconds <= _terrainSettleTime && _bTerrainRefreshPending == SW_FALSE;
        if ( bReadyToRefresh == false )
            return;
        // 같은 편집으로 다시 걸지 않게 표시해 둔다. 계산한 세대는 다시 계산이 적는다(편집 사각형을 그 세대부터 찾는다). 레지스트리를 바꾸므로 틱 뒤에 한다.
        _bTerrainRefreshPending = SW_TRUE;
        manager.executeOrDeferPostTickOnComponent( *this, []( FoliageComponent& foliage )
        { foliage.refreshFromTerrainEdits(); } );
    }

    void FoliageComponent::refreshFromTerrainEdits()
    {
        _bTerrainRefreshPending = SW_FALSE;
        [[maybe_unused]] const Stopwatch stopwatch; // 아래 로그에만 쓴다. Shipping 은 로그가 빠진다.
        const float3                     origin   = getWorldPosition();
        GameObject*                      pOwner   = getOwner();
        const GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const TerrainComponent*          pTerrain = pManager != nullptr ? TerrainComponent::findTerrainAt( *pManager, origin._x, origin._z ) : nullptr;
        // 지형을 다시 읽으면 스플랫 세대가 0 으로 돌아간다. 영역이 바뀌면 후보 위치가 모두 바뀐다. 그때는 들고 있는 판정을 쓸 수 없다.
        const bool bCandidatesUsable = pTerrain != nullptr && _pPrimitiveRegistry != nullptr && _listLayerCandidate.size() == _listLayer.size() &&
                                       _listLayerMaterial.size() == _listLayer.size() && computeRegion().matchesExactly( _builtRegion ) &&
                                       pTerrain->getSplatRevision() >= _builtSplatRevision;
        if ( bCandidatesUsable == false )
        {
            rebuildFoliage();
            return;
        }
        PlacementRegion dirtyArea{};
        const bool      bEdited = pTerrain->findEditedArea( _builtTerrainGeneration, _builtSplatRevision, dirtyArea._min, dirtyArea._max );
        _builtTerrainGeneration = pTerrain->getHeightfieldGeneration();
        _builtSplatRevision     = pTerrain->getSplatRevision();
        _seenTerrainGeneration  = _builtTerrainGeneration;
        _seenSplatRevision      = _builtSplatRevision;
        _lastRebuiltBatchCount  = 0;
        if ( bEdited == false )
            return;
        vector<PlacementInstance> listInstance;
        for ( uint32 layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            computeLayerInstances( layerIndex, pTerrain, &dirtyArea, listInstance );
            applyLayerInstances( layerIndex, listInstance );
        }
        SW_LOG_INFO( "Foliage: refreshed %# of %# cell batches after a terrain edit (%# instances, %# us)", _lastRebuiltBatchCount, static_cast<uint32>( _listCell.size() ),
                     _instanceCount, stopwatch.getElapsedMicroseconds() );
    }

    PlacementRegion FoliageComponent::computeRegion() const
    {
        const float3    origin = getWorldPosition();
        PlacementRegion region;
        if ( _regionSize._x > 0.0f && _regionSize._y > 0.0f )
        {
            region._min = float2{ origin._x - 0.5f * _regionSize._x, origin._z - 0.5f * _regionSize._y };
            region._max = float2{ origin._x + 0.5f * _regionSize._x, origin._z + 0.5f * _regionSize._y };
            return region;
        }
        GameObject*              pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const TerrainComponent*  pTerrain = pManager != nullptr ? TerrainComponent::findTerrainAt( *pManager, origin._x, origin._z ) : nullptr;
        if ( pTerrain != nullptr )
        {
            const TerrainHeightfield& field = pTerrain->getHeightfield();
            region._min                     = float2{ field.getOrigin()._x, field.getOrigin()._z };
            region._max                     = float2{ field.getOrigin()._x + field.getSize()._x, field.getOrigin()._z + field.getSize()._y };
        }
        return region;
    }

    bool FoliageComponent::computeLayerPlacements( uint32 layerIndex, vector<PlacementInstance>& outListInstance ) const
    {
        outListInstance.clear();
        if ( layerIndex >= _listLayer.size() )
            return false;
        const FoliageLayer&      layer    = _listLayer[layerIndex];
        const float3             origin   = getWorldPosition();
        GameObject*              pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const TerrainComponent*  pTerrain = pManager != nullptr ? TerrainComponent::findTerrainAt( *pManager, origin._x, origin._z ) : nullptr;

        const vector<float32> listWeight = FoliageComponentInternal::makeMeshWeights( layer );
        if ( pTerrain != nullptr )
        {
            const TerrainPlacementSurface surface( pTerrain->getHeightfield() );
            PlacementScatter::scatter( layer._rule, computeRegion(), &surface, _listExclusion, listWeight, outListInstance );
            return true;
        }
        // 지형이 없으면 오너 높이의 평면이다.
        PlacementScatter::scatter( layer._rule, computeRegion(), nullptr, _listExclusion, listWeight, outListInstance );
        for ( PlacementInstance& instance : outListInstance )
        {
            instance._height += origin._y;
        }
        return true;
    }

    void FoliageComponent::releaseCells()
    {
        _listCell.clear(); // 배치의 소멸자가 레지스트리에서 뺀다
        _instanceCount = 0;
    }

    void FoliageComponent::rebuildFoliage()
    {
        releaseCells();
        _listLayerCandidate.clear();
        _lastRebuiltBatchCount = 0;
        if ( _pPrimitiveRegistry == nullptr )
            return;
        [[maybe_unused]] const Stopwatch stopwatch; // 아래 로그에만 쓴다. Shipping 은 로그가 빠진다.
        const float3                     origin   = getWorldPosition();
        GameObject*                      pOwner   = getOwner();
        const GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const TerrainComponent*          pTerrain = pManager != nullptr ? TerrainComponent::findTerrainAt( *pManager, origin._x, origin._z ) : nullptr;
        _bBuiltOnTerrain                          = pTerrain != nullptr ? SW_TRUE : SW_FALSE;
        _builtTerrainGeneration                   = pTerrain != nullptr ? pTerrain->getHeightfieldGeneration() : 0u;
        _builtSplatRevision                       = pTerrain != nullptr ? pTerrain->getSplatRevision() : 0u;
        _seenTerrainGeneration                    = _builtTerrainGeneration;
        _seenSplatRevision                        = _builtSplatRevision;
        _builtRegion                              = computeRegion();
        if ( pTerrain != nullptr )
            _listLayerCandidate.resize( _listLayer.size() );

        // 레이어 머티리얼은 레이어마다 자기 인스턴스를 가진다(바람 반응과 페이드가 레이어 값이다).
        _listLayerMaterial.clear();
        _listLayerMaterial.reserve( _listLayer.size() );
        vector<PlacementInstance> listInstance;
        for ( uint32 layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const FoliageLayer& layer = _listLayer[layerIndex];
            _listLayerMaterial.push_back( sw::make_unique<EnvironmentMaterial>() );
            EnvironmentMaterial& material = *_listLayerMaterial.back();
            // 칸이 비면 엔진 기본 풀 · 나무 머티리얼이다(값의 기준은 엔진 기본 에셋 표).
            const string& materialPath = layer._materialPath.empty() ? engine::getSubsystem<EngineDefaultAssets>()._foliageMaterial.get() : layer._materialPath;
            (void)material.acquire( materialPath ); // 못 잡으면 씬 기본 머티리얼(경고는 안에서)
            material.setVector( hashed_string( "tint" ), layer._tint );

            computeLayerInstances( layerIndex, pTerrain, nullptr, listInstance );
            applyLayerInstances( layerIndex, listInstance );
        }
        SW_LOG_INFO( "Foliage: %# instances in %# cell batches across %# layers (%# us, %#)", _instanceCount, static_cast<uint32>( _listCell.size() ),
                     static_cast<uint32>( _listLayer.size() ), stopwatch.getElapsedMicroseconds(), _bBuiltOnTerrain == SW_TRUE ? "on terrain" : "flat" );
    }

    void FoliageComponent::computeLayerInstances( uint32 layerIndex, const TerrainComponent* pTerrain, const PlacementRegion* pDirtyArea,
                                                  vector<PlacementInstance>& outListInstance )
    {
        if ( pTerrain == nullptr )
        {
            (void)computeLayerPlacements( layerIndex, outListInstance ); // 부르는 쪽이 레이어 번호를 범위 안에서 돈다
            return;
        }
        const FoliageLayer&   layer          = _listLayer[layerIndex];
        const vector<float32> listMeshWeight = FoliageComponentInternal::makeMeshWeights( layer );
        // 들고 있는 판정이 이 규칙 · 영역의 것이 아니면(후보 수가 다르다) 처음부터 판정한다.
        const TerrainPlacementSurface surface( pTerrain->getHeightfield() );
        vector<PlacementCandidate>&   listCandidate = _listLayerCandidate[layerIndex];
        const bool                    bResampled    = pDirtyArea != nullptr &&
                                PlacementScatter::resampleCandidates( layer._rule, _builtRegion, &surface, _listExclusion, *pDirtyArea, listCandidate );
        if ( bResampled == false )
            PlacementScatter::sampleCandidates( layer._rule, _builtRegion, &surface, _listExclusion, listCandidate );
        PlacementScatter::resolveCandidates( layer._rule, _builtRegion, listCandidate, listMeshWeight, outListInstance );
    }

    void FoliageComponent::applyLayerInstances( uint32 layerIndex, const vector<PlacementInstance>& listInstance )
    {
        using Internal = FoliageComponentInternal;

        vector<shared_ptr<Mesh>> listMesh;
        vector<float32>          listMeshRadius;
        Internal::acquireLayerMeshes( _listLayer[layerIndex], listMesh, listMeshRadius );
        const unordered_map<uint64, vector<uint32>> mapCellToInstance = Internal::groupByCell( listInstance, listMesh, MathUtil::max( _cellSize, 4.0f ) );

        // 이 레이어의 지금 셀을 키로 찾는다. 내용 해시가 같은 셀은 그대로 두고(레지스트리도 그대로), 다른 셀만 새 배치로 바꾼다.
        unordered_map<uint64, uint32> mapKeyToCell;
        for ( uint32 cellIndex = 0; cellIndex < _listCell.size(); ++cellIndex )
        {
            if ( _listCell[cellIndex]._layerIndex == layerIndex )
                mapKeyToCell.emplace( _listCell[cellIndex]._key, cellIndex );
        }
        vector<uint8> listKeep( _listCell.size(), SW_FALSE );

        vector<Cell> listAddedCell;
        for ( const uint64 key : Internal::sortedKeys( mapCellToInstance ) )
        {
            const vector<uint32>& listIndex   = mapCellToInstance.find( key )->second;
            const uint64          contentHash = Internal::computeCellHash( listInstance, listIndex );
            const auto            iter        = mapKeyToCell.find( key );
            const bool            bUnchanged  = iter != mapKeyToCell.end() && _listCell[iter->second]._contentHash == contentHash;
            if ( bUnchanged )
            {
                listKeep[iter->second] = SW_TRUE;
                continue;
            }
            const size_t meshIndex = static_cast<size_t>( listInstance[listIndex.front()]._entryIndex );
            listAddedCell.push_back( makeCell( layerIndex, key, contentHash, listInstance, listIndex, listMesh[meshIndex], listMeshRadius[meshIndex] ) );
        }

        releaseLayerCells( layerIndex, listKeep );
        _lastRebuiltBatchCount += static_cast<uint32>( listAddedCell.size() );
        for ( Cell& cell : listAddedCell )
        {
            _listCell.push_back( std::move( cell ) );
        }
        _instanceCount = 0;
        for ( const Cell& cell : _listCell )
        {
            _instanceCount += cell._batch->getCount();
        }
    }

    FoliageComponent::Cell FoliageComponent::makeCell( uint32 layerIndex, uint64 key, uint64 contentHash, const vector<PlacementInstance>& listInstance, const vector<uint32>& listIndex,
                                                       const shared_ptr<Mesh>& mesh, float32 meshRadius )
    {
        const FoliageLayer&  layer    = _listLayer[layerIndex];
        EnvironmentMaterial& material = *_listLayerMaterial[layerIndex];
        Cell                 cell;
        cell._layerIndex  = layerIndex;
        cell._key         = key;
        cell._contentHash = contentHash;
        cell._batch       = sw::make_unique<MeshInstanceBatch>( mesh, material.getMaterial(), material.getInstance(), static_cast<uint32>( listIndex.size() ), MeshInstanceWriter::CPU );
        cell._batch->setOwnerComponent( this );
        float3  minimum{ MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat };
        float3  maximum{ MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat };
        float32 maxScale{ 0.0f };
        for ( uint32 entry = 0; entry < listIndex.size(); ++entry )
        {
            const PlacementInstance& instance = listInstance[listIndex[entry]];
            const float4x4           world    = PlacementScatter::makeWorldMatrix( instance, float3::Zero );
            cell._batch->setWorld( entry, world );
            cell._batch->setBoundsRadius( entry, meshRadius );
            // 밝기 흔들기는 인스턴스 색 필드로 한다(머티리얼을 가르지 않는다).
            const float32 shade = 1.0f - layer._tintVariation * PlacementScatter::toUnit( instance._hash );
            cell._batch->setSprite( entry, GPUSpriteInstanceData::make( float4{ 0.0f, 0.0f, 1.0f, 1.0f }, float4{ shade, shade, shade, 1.0f } ) );
            const float3 position = world.getTranslation();
            minimum               = float3{ MathUtil::min( minimum._x, position._x ), MathUtil::min( minimum._y, position._y ), MathUtil::min( minimum._z, position._z ) };
            maximum               = float3{ MathUtil::max( maximum._x, position._x ), MathUtil::max( maximum._y, position._y ), MathUtil::max( maximum._z, position._z ) };
            maxScale              = MathUtil::max( maxScale, instance._scale );
        }
        cell._center = ( minimum + maximum ) * 0.5f;
        cell._radius = ( maximum - minimum ).getLength() * 0.5f + meshRadius * maxScale;
        _pPrimitiveRegistry->addInstanceBatch( cell._batch.get() );
        return cell;
    }

    void FoliageComponent::releaseLayerCells( uint32 layerIndex, const vector<uint8>& listKeep )
    {
        size_t keptCount{ 0 };
        for ( size_t cellIndex = 0; cellIndex < _listCell.size(); ++cellIndex )
        {
            const bool bKeep = _listCell[cellIndex]._layerIndex != layerIndex || listKeep[cellIndex] == SW_TRUE;
            if ( bKeep == false )
                continue;
            if ( keptCount != cellIndex )
                _listCell[keptCount] = std::move( _listCell[cellIndex] );
            ++keptCount;
        }
        _listCell.resize( keptCount );
    }

    void FoliageComponent::updateView( const float3& viewPosition )
    {
        using Internal = FoliageComponentInternal;
        for ( Cell& cell : _listCell )
        {
            const float32 fadeEnd  = _listLayer[cell._layerIndex]._fadeEnd;
            const float32 distance = ( cell._center - viewPosition ).getLength() - cell._radius;
            cell._batch->setVisible( distance < fadeEnd );
        }

        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        WindSettings       wind;
        float4             arrSphere[FoliageInfluencerComponent::kMaxInfluencerCount]{};
        if ( pManager != nullptr )
        {
            (void)WindComponent::findWind( *pManager, wind ); // 없으면 기본 바람
            (void)FoliageInfluencerComponent::collectNearest( *pManager, viewPosition, arrSphere );
        }
        for ( uint32 layerIndex = 0; layerIndex < _listLayerMaterial.size() && layerIndex < _listLayer.size(); ++layerIndex )
        {
            const FoliageLayer&  layer    = _listLayer[layerIndex];
            EnvironmentMaterial& material = *_listLayerMaterial[layerIndex];
            // 머티리얼 값은 VS 가 읽는다(foliage.hlsl). x 와 y 는 방향, z 는 세기 × 반응, w 는 시간이다.
            material.setVector( hashed_string( "windParams" ), float4{ wind._direction._x, wind._direction._y, wind._strength * layer._windResponse, _time } );
            // x = 흔들림 주파수, y = 돌풍 세기 × 반응, z = 돌풍 주파수, w = 흔들림이 다 차는 높이
            material.setVector( hashed_string( "windWave" ), float4{ wind._swayFrequency, wind._gustStrength * layer._windResponse, wind._gustFrequency, layer._swayHeight } );
            // xyz = 카메라, w = 휘게 하는 세기
            material.setVector( hashed_string( "viewParams" ), float4{ viewPosition._x, viewPosition._y, viewPosition._z, layer._bendStrength } );
            material.setVector( hashed_string( "fadeParams" ), float4{ layer._fadeStart, layer._fadeEnd, 0.0f, 0.0f } );
            for ( uint32 slot = 0; slot < FoliageInfluencerComponent::kMaxInfluencerCount; ++slot )
            {
                material.setVector( Internal::getInfluencerName( slot ), arrSphere[slot] );
            }
        }
    }

    uint32 FoliageComponent::getVisibleBatchCount() const
    {
        uint32 count{ 0 };
        for ( const Cell& cell : _listCell )
        {
            count += cell._batch->isVisible() ? 1u : 0u;
        }
        return count;
    }
} // namespace sw
