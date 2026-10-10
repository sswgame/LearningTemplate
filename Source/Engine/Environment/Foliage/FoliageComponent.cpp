#include "pch.h"

#include "Engine/Environment/Foliage/FoliageComponent.h"

#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"
#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"

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

            /** @brief 메시 원점(인스턴스 위치)에서 정점까지의 가장 먼 거리입니다 — 바운드 중심이 인스턴스 위치라 그만큼 덮어야 한다. */
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
        , _pPrimitiveRegistry{ nullptr }
        , _time{ 0.0f }
        , _instanceCount{ 0 }
        , _bBuiltOnTerrain{ SW_FALSE }
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
        // 로드 순서상 지형이 뒤에 섰으면 처음 계산은 평면 위였다 — 모두 선 지금 지형을 다시 찾는다. 등록부를 바꾸므로 틱 뒤에.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _bBuiltOnTerrain == SW_TRUE )
            return;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            FoliageComponent* pFoliage = static_cast<FoliageComponent*>( pManager->resolveComponent( self ) );
            if ( pFoliage != nullptr )
                pFoliage->rebuildFoliage();
        } );
    }

    void FoliageComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        // 켜고 끄기는 배치를 바꾸지 않는다 — 셀을 다시 짓지 않고 빌더가 다시 보게만 한다.
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
        if ( pManager != nullptr && _listCell.empty() == false && EnvironmentUtil::findViewPosition( *pManager, viewPosition ) )
            updateView( viewPosition );
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

        vector<float32> listWeight;
        listWeight.reserve( layer._listMesh.size() );
        for ( const FoliageMesh& mesh : layer._listMesh )
        {
            listWeight.push_back( mesh._weight );
        }
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
        _listCell.clear(); // 배치의 소멸자가 등록부에서 뺀다
        _instanceCount = 0;
    }

    void FoliageComponent::rebuildFoliage()
    {
        using Internal = FoliageComponentInternal;
        releaseCells();
        if ( _pPrimitiveRegistry == nullptr )
            return;
        [[maybe_unused]] const Stopwatch stopwatch; // 아래 로그에만 쓴다 — Shipping 은 로그가 빠진다
        const float3                     origin   = getWorldPosition();
        GameObject*                      pOwner   = getOwner();
        const GameObjectManager*         pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        _bBuiltOnTerrain                          = ( pManager != nullptr && TerrainComponent::findTerrainAt( *pManager, origin._x, origin._z ) != nullptr ) ? SW_TRUE : SW_FALSE;

        // 레이어 머티리얼 — 레이어마다 자기 인스턴스(바람 반응 · 페이드가 레이어 값이다).
        _listLayerMaterial.clear();
        _listLayerMaterial.reserve( _listLayer.size() );
        const float32             cellSize = MathUtil::max( _cellSize, 4.0f );
        vector<PlacementInstance> listInstance;
        for ( uint32 layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const FoliageLayer& layer = _listLayer[layerIndex];
            _listLayerMaterial.push_back( sw::make_unique<EnvironmentMaterial>() );
            EnvironmentMaterial& material = *_listLayerMaterial.back();
            (void)material.acquire( layer._materialPath ); // 못 잡으면 씬 기본 머티리얼(경고는 안에서)
            material.setVector( hashed_string( "tint" ), layer._tint );

            if ( computeLayerPlacements( layerIndex, listInstance ) == false || listInstance.empty() )
                continue;

            // 메시마다 · 셀마다 묶는다. 키 = 메시 번호 × 2^32 + 셀 (x, z) 해시.
            vector<shared_ptr<Mesh>> listMesh;
            vector<float32>          listMeshRadius;
            for ( const FoliageMesh& foliageMesh : layer._listMesh )
            {
                shared_ptr<Mesh> mesh = Internal::acquireMesh( foliageMesh._meshID );
                if ( mesh == nullptr )
                    SW_LOG_WARNING( "Foliage layer '%#': mesh '%#' could not be acquired - its instances are skipped", layer._name.c_str(), foliageMesh._meshID.c_str() );
                listMeshRadius.push_back( mesh != nullptr ? Internal::computeOriginRadius( *mesh ) : 0.0f );
                listMesh.push_back( std::move( mesh ) );
            }
            unordered_map<uint64, vector<uint32>> mapCellToInstance;
            for ( uint32 instanceIndex = 0; instanceIndex < listInstance.size(); ++instanceIndex )
            {
                const PlacementInstance& instance = listInstance[instanceIndex];
                if ( instance._entryIndex < 0 || static_cast<size_t>( instance._entryIndex ) >= listMesh.size() || listMesh[static_cast<size_t>( instance._entryIndex )] == nullptr )
                    continue;
                const int32  cellX = static_cast<int32>( MathUtil::floor( instance._planePosition._x / cellSize ) );
                const int32  cellZ = static_cast<int32>( MathUtil::floor( instance._planePosition._y / cellSize ) );
                const uint64 key   = ( static_cast<uint64>( instance._entryIndex ) << 48 ) ^ ( static_cast<uint64>( static_cast<uint16>( cellX ) ) << 16 ) ^
                                   static_cast<uint64>( static_cast<uint16>( cellZ ) );
                mapCellToInstance[key].push_back( instanceIndex );
            }

            // 셀 순서를 정렬해 등록 순서를 결정적으로 — 해시 맵 순회 순서는 실행마다 같다는 보장이 없다.
            vector<uint64> listKey;
            listKey.reserve( mapCellToInstance.size() );
            for ( const auto& [key, listIndex] : mapCellToInstance )
            {
                listKey.push_back( key );
            }
            std::sort( listKey.begin(), listKey.end() );
            for ( const uint64 key : listKey )
            {
                const vector<uint32>&    listIndex = mapCellToInstance[key];
                const PlacementInstance& first     = listInstance[listIndex.front()];
                const size_t             meshIndex = static_cast<size_t>( first._entryIndex );
                Cell                     cell;
                cell._layerIndex = layerIndex;
                cell._batch      = sw::make_unique<MeshInstanceBatch>( listMesh[meshIndex], material.getMaterial(), material.getInstance(), static_cast<uint32>( listIndex.size() ) );
                cell._batch->setOwnerComponent( this );
                float3  minimum{ MathUtil::kMaxFloat, MathUtil::kMaxFloat, MathUtil::kMaxFloat };
                float3  maximum{ MathUtil::kMinFloat, MathUtil::kMinFloat, MathUtil::kMinFloat };
                float32 maxScale{ 0.0f };
                for ( uint32 entry = 0; entry < listIndex.size(); ++entry )
                {
                    const PlacementInstance& instance = listInstance[listIndex[entry]];
                    const float4x4           world    = PlacementScatter::makeWorldMatrix( instance, float3::Zero );
                    cell._batch->setWorld( entry, world );
                    cell._batch->setBoundsRadius( entry, listMeshRadius[meshIndex] );
                    // 밝기 흔들기 — 인스턴스 색 칸(머티리얼을 가르지 않는다).
                    const float32 shade = 1.0f - layer._tintVariation * PlacementScatter::toUnit( instance._hash );
                    cell._batch->setSprite( entry, GPUSpriteInstanceData::make( float4{ 0.0f, 0.0f, 1.0f, 1.0f }, float4{ shade, shade, shade, 1.0f } ) );
                    const float3 position = world.getTranslation();
                    minimum               = float3{ MathUtil::min( minimum._x, position._x ), MathUtil::min( minimum._y, position._y ), MathUtil::min( minimum._z, position._z ) };
                    maximum               = float3{ MathUtil::max( maximum._x, position._x ), MathUtil::max( maximum._y, position._y ), MathUtil::max( maximum._z, position._z ) };
                    maxScale              = MathUtil::max( maxScale, instance._scale );
                }
                cell._center = ( minimum + maximum ) * 0.5f;
                cell._radius = ( maximum - minimum ).getLength() * 0.5f + listMeshRadius[meshIndex] * maxScale;
                _instanceCount += static_cast<uint32>( listIndex.size() );
                _pPrimitiveRegistry->addInstanceBatch( cell._batch.get() );
                _listCell.push_back( std::move( cell ) );
            }
        }
        SW_LOG_INFO( "Foliage: %# instances in %# cell batches across %# layers (%# us, %#)", _instanceCount, static_cast<uint32>( _listCell.size() ),
                     static_cast<uint32>( _listLayer.size() ), stopwatch.getElapsedMicroseconds(), _bBuiltOnTerrain == SW_TRUE ? "on terrain" : "flat" );
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
            // 머티리얼 값은 VS 가 읽는다(foliage.hlsl). x · y = 방향, z = 세기 × 반응, w = 시간
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
