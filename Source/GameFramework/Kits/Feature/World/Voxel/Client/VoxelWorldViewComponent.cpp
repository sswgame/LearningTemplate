#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Client/VoxelWorldViewComponent.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Feature/World/Voxel/Client/VoxelChunkComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        /** @brief 세운 청크를 모든 게임의 상태 저장 전에 걷는다(게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<VoxelWorldViewComponent> s_viewRegistration{ "VoxelWorldViewComponent" };

        struct VoxelWorldViewComponentInternal
        {
            /** @brief 청크가 그릴 범위의 가운데에서 떨어진 체비쇼프 거리입니다. */
            static int32 computeDistance( const VoxelGameModeComponent& director, int32 chunkX, int32 chunkZ )
            {
                return MathUtil::max( MathUtil::abs( chunkX - director.getCenterChunkX() ), MathUtil::abs( chunkZ - director.getCenterChunkZ() ) );
            }
        };
    } // namespace

    VoxelWorldViewComponent::VoxelWorldViewComponent()
        : _chunkPrefab{}
        , _chunkBuildsPerFrame{ 4 }
        , _director{}
        , _mapChunkView{}
        , _atlasGate{}
        , _bPrefabInvalid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelWorldViewComponent::~VoxelWorldViewComponent() = default;

    void VoxelWorldViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        const GameObject* const             pOwner    = getOwner();
        const VoxelGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<VoxelGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "VoxelWorldView.NoGameMode", "Voxel world view on '%#': no VoxelGameModeComponent in front of it on the same object - no chunk is drawn",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<VoxelGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<VoxelGameModeComponent>( pDirector );
    }

    void VoxelWorldViewComponent::onEndPlay()
    {
        despawnViews();
        Component::onEndPlay();
    }

    void VoxelWorldViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        context.deferPostTickOnComponent( *this, []( VoxelWorldViewComponent& view, GameObjectManager& manager )
        { view.updateViews( manager ); } );
    }

    void VoxelWorldViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            for ( const auto& entry : _mapChunkView )
            {
                GameObject* const pObject = pManager->resolveGameObject( entry.second._object );
                if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
            }
        }
        _mapChunkView.clear();
    }

    void VoxelWorldViewComponent::updateViews( GameObjectManager& manager )
    {
        const VoxelGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        removeHiddenViews( manager, *pDirector );
        if ( _atlasGate.isPending() )
            updateAtlasGate( manager, *pDirector );

        // 몸에서 가까운 청크부터 본다 — 블록을 부순 곳이 먼저 다시 메싱된다. 처음 짓는 청크는 한도에 들지 않는다.
        const VoxelWorld& world        = pDirector->getWorld();
        const int32       centerX      = pDirector->getCenterChunkX();
        const int32       centerZ      = pDirector->getCenterChunkZ();
        int32             rebuiltCount = 0;
        for ( int32 ring = 0; ring <= pDirector->getViewChunkRadius(); ++ring )
        {
            for ( int32 chunkZ = centerZ - ring; chunkZ <= centerZ + ring; ++chunkZ )
            {
                for ( int32 chunkX = centerX - ring; chunkX <= centerX + ring; ++chunkX )
                {
                    const bool              bOnRing = VoxelWorldViewComponentInternal::computeDistance( *pDirector, chunkX, chunkZ ) == ring;
                    const VoxelChunk* const pChunk  = bOnRing ? world.findChunk( chunkX, chunkZ ) : nullptr;
                    if ( pChunk == nullptr )
                        continue;
                    const uint64 key = VoxelWorld::makeChunkKey( chunkX, chunkZ );
                    if ( _mapChunkView.contains( key ) == false )
                        spawnView( manager, *pDirector, chunkX, chunkZ );
                    const auto viewIt = _mapChunkView.find( key );
                    if ( viewIt == _mapChunkView.end() || _atlasGate.canDraw() == false )
                        continue;
                    ChunkView& view        = viewIt->second;
                    const bool bFirstBuild = view._builtRevision.has_value() == false;
                    const bool bUpToDate   = bFirstBuild == false && *view._builtRevision == pChunk->_revision;
                    if ( bUpToDate || ( bFirstBuild == false && rebuiltCount >= _chunkBuildsPerFrame ) )
                        continue;
                    view._builtRevision                   = pChunk->_revision;
                    VoxelChunkComponent* const pViewChunk = findChunk( manager, view );
                    if ( pViewChunk != nullptr )
                        pViewChunk->requestRebuild();
                    if ( bFirstBuild == false )
                        ++rebuiltCount;
                }
            }
        }
    }

    void VoxelWorldViewComponent::removeHiddenViews( GameObjectManager& manager, const VoxelGameModeComponent& director )
    {
        // 지우면 사전이 원소를 옮기므로 키를 먼저 모은다. 사라질 청크가 없는 프레임은 할당이 없다.
        vector<uint64> listGoneKey;
        for ( const auto& entry : _mapChunkView )
        {
            const int32 chunkX = VoxelWorld::toChunkX( entry.first );
            const int32 chunkZ = VoxelWorld::toChunkZ( entry.first );
            const bool  bNear  = VoxelWorldViewComponentInternal::computeDistance( director, chunkX, chunkZ ) <= director.getViewChunkRadius();
            if ( bNear == false || director.getWorld().isChunkResident( chunkX, chunkZ ) == false )
                listGoneKey.push_back( entry.first );
        }
        for ( const uint64 key : listGoneKey )
        {
            const auto        viewIt  = _mapChunkView.find( key );
            GameObject* const pObject = manager.resolveGameObject( viewIt->second._object );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            _mapChunkView.erase( viewIt );
        }
    }

    void VoxelWorldViewComponent::spawnView( GameObjectManager& manager, const VoxelGameModeComponent& director, int32 chunkX, int32 chunkZ )
    {
        AssetManager* const pAssets = game::findSubsystem<AssetManager>();
        if ( _bPrefabInvalid == SW_TRUE || pAssets == nullptr )
            return;
        GameObject* const          pObject = pAssets->getAssetCache<PrefabCache>().spawn( &manager, _chunkPrefab.path(), "VoxelChunk" );
        VoxelChunkComponent* const pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
        MeshComponent* const       pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pChunk == nullptr || pMesh == nullptr )
        {
            _bPrefabInvalid = SW_TRUE;
            SW_LOG_ERROR_ID( "VoxelWorldView.ChunkPrefabInvalid", "Voxel world view: chunk prefab '%#' did not spawn a VoxelChunkComponent with a MeshComponent - no chunk is drawn", _chunkPrefab.path() );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            return;
        }
        pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( float3{ static_cast<float32>( chunkX * kVoxelChunkSize ), 0.0f, static_cast<float32>( chunkZ * kVoxelChunkSize ) } ) );
        pChunk->assignChunk( director.getOwner()->getHandle(), chunkX, chunkZ );
        _mapChunkView[VoxelWorld::makeChunkKey( chunkX, chunkZ )]._object = pObject->getHandle();
    }

    void VoxelWorldViewComponent::updateAtlasGate( const GameObjectManager& manager, const VoxelGameModeComponent& director )
    {
        if ( _mapChunkView.empty() )
            return;
        const GameObject* const          pObject = manager.resolveGameObject( _mapChunkView.begin()->second._object );
        const VoxelChunkComponent* const pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
        if ( pChunk == nullptr )
            return;
        MeshComponent* pOpaque = nullptr;
        MeshComponent* pWater  = nullptr;
        pChunk->findMeshes( pOpaque, pWater );
        const Material* const pOpaqueMaterial = pOpaque != nullptr ? pOpaque->getMaterial() : nullptr;
        const Material* const pWaterMaterial  = pWater != nullptr ? pWater->getMaterial() : nullptr;
        if ( pOpaqueMaterial == nullptr || pWaterMaterial == nullptr )
            return; // 아직 준비되지 않았다 — 다음 프레임에 다시 본다(그동안 청크는 그리지 않는다)
        // 프로퍼티는 게임 스레드 초기화(`Ready`)가 끝나야 채워진다 — 그 전에는 견줄 값이 없으니 다음 프레임에 다시 본다.
        const bool bMaterialsReady = pOpaqueMaterial->getInitState() == MaterialInitState::Ready && pWaterMaterial->getInitState() == MaterialInitState::Ready;
        (void)_atlasGate.update( *director.getWorld().getCatalog(), bMaterialsReady, static_cast<const float4*>( pOpaqueMaterial->getParameterData( "_atlasGrid" ) ),
                                 static_cast<const float4*>( pWaterMaterial->getParameterData( "_atlasGrid" ) ), pOpaque->getMaterialPath().c_str() ); // 결과는 `canDraw` 로 읽는다
    }

    VoxelChunkComponent* VoxelWorldViewComponent::findChunk( GameObjectManager& manager, const ChunkView& view )
    {
        GameObject* const pObject = manager.resolveGameObject( view._object );
        return pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
    }
} // namespace sw
