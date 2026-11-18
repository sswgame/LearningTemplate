#include "pch.h"

#include "Games/VoxelCraft/VoxelGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelTerrain.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/VoxelCraft/VoxelAutoPlayControllerComponent.h"
#include "Games/VoxelCraft/VoxelChunkComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct VoxelGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "VOXL" );
            static constexpr uint32 kStateVersion = 1;

            /** @brief `VoxelWorld::makeChunkKey` 의 거꿀입니다. */
            static int32 toChunkX( uint64 key ) { return static_cast<int32>( static_cast<uint32>( key >> 32 ) ); }
            static int32 toChunkZ( uint64 key ) { return static_cast<int32>( static_cast<uint32>( key ) ); }
            static int32 computeDistance( int32 chunkX, int32 chunkZ, int32 centerX, int32 centerZ )
            {
                return MathUtil::max( MathUtil::abs( chunkX - centerX ), MathUtil::abs( chunkZ - centerZ ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelGameModeComponent::VoxelGameModeComponent()
        : _chunkPrefab{}
        , _autoPlayPrefab{}
        , _player{}
        , _terrainSeed{ 20261003 }
        , _chunkCountX{ 0 }
        , _chunkCountZ{ 0 }
        , _chunkBuildsPerFrame{ 4 }
        , _residentChunkRadius{ 0 }
        , _chunkLoadsPerFrame{ 0 }
        , _statusLogInterval{ 5.0f }
        , _waterBlock{}
        , _stoneBlock{}
        , _grassBlock{}
        , _snowBlock{}
        , _coalOreBlock{}
        , _ironOreBlock{}
        , _snowHeight{ 0 }
        , _coalOreChance{ 0.0f }
        , _ironOreChance{ 0.0f }
        , _ironOreMaxHeight{ 0 }
        , _world{}
        , _mapChunkView{}
        , _autoPlayController{}
        , _statusTimer{ 0.0f }
        , _centerChunkX{ 0 }
        , _centerChunkZ{ 0 }
        , _appliedAutoPlay{ -1 }
        , _bChunkViewsStale{ SW_FALSE }
        , _atlasGate{}
    {
    }

    VoxelGameModeComponent::~VoxelGameModeComponent() = default;

    void VoxelGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "VoxelGameMode.WorldReadyWASDMove", "[Voxel] world is ready - WASD move, mouse look, Space jump, Shift sprint, LMB hold to break, RMB place, 1-9 or wheel hotbar, Esc mouse" );
    }

    void VoxelGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( areViewsSpawned() == false )
            return; // 상태를 저장하기 전에 청크를 정리했습니다. 베이스가 청크를 다시 스폰합니다(블록은 그대로입니다).
        updateResidency();
        scheduleRebuilds();
        SW_PROFILE_COUNT( "GT.VoxelWorld.residentChunks", _world.getResidentChunkCount() ); // 감사 K-12 전후 비교
        SW_PROFILE_COUNT( "GT.VoxelWorld.storedChunks", _world.getStoredChunkCount() );
        logStatus( deltaTime );
    }

    void VoxelGameModeComponent::onViewsDespawned()
    {
        _mapChunkView.clear();
        // 자동 플레이 AI 오브젝트도 정리되었습니다. 다시 스폰할 때 빙의를 다시 맞춥니다.
        _autoPlayController = GameObjectHandle{};
        _appliedAutoPlay    = -1;
    }

    bool VoxelGameModeComponent::hasPendingSpawn() const
    {
        const bool bAtlasCheckWaiting = _atlasGate.isPending() && _mapChunkView.empty() == false; // 머티리얼이 준비될 때까지 플러시에서 다시 본다
        return bAtlasCheckWaiting || _bChunkViewsStale != SW_FALSE || _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 );
    }

    void VoxelGameModeComponent::syncAutoPlayPossession( GameObjectManager& manager )
    {
        const bool     bAutoPlay = isAutoPlayOn();
        PawnComponent* pPawn     = findOn<PawnComponent>( _player );
        _appliedAutoPlay         = bAutoPlay ? 1 : 0; // 폰이 없어도 적용한 것으로 봅니다. 그래야 매 틱 플러시를 예약하지 않습니다.
        if ( pPawn == nullptr )
            return;
        GameObject* pAIObject = manager.resolveGameObject( _autoPlayController );
        if ( bAutoPlay )
        {
            if ( pAIObject == nullptr )
            {
                // 조종자와 그 행동 트리는 프리팹 데이터다. 오브젝트 이름(`VoxelAutoPlay`)은 시나리오가 본다. 스폰한 것은 정리 목록에 든다.
                pAIObject = spawnPrefab( manager, _autoPlayPrefab.path(), "VoxelAutoPlay" );
                if ( pAIObject == nullptr || pAIObject->getComponent<VoxelAutoPlayControllerComponent>() == nullptr )
                {
                    SW_LOG_ERROR_ID( "VoxelGameMode.AutoPlayPrefabInvalid", "Auto play controller prefab '%#' did not spawn a VoxelAutoPlayControllerComponent", _autoPlayPrefab.path() );
                    return;
                }
                _autoPlayController = pAIObject->getHandle();
            }
            VoxelAutoPlayControllerComponent* pAI = pAIObject->getComponent<VoxelAutoPlayControllerComponent>();
            if ( pAI != nullptr && pAI->getPawn() != pPawn->getHandle() )
                pAI->possess( *pPawn );
            SW_LOG_INFO_ID( "VoxelGameMode.AutoPlayTookPlayer", "[Voxel] auto play took the player" );
            return;
        }
        // 끄는 경우입니다. 플레이어 조종자에게 폰을 돌려주고 AI 를 정리합니다. 이미 다른 조종자(플레이어)가 폰을 가지고 있으면 그대로 둡니다.
        if ( ControlSystem::returnPawnToPlayer( manager, *pPawn, _autoPlayController ) )
            SW_LOG_INFO_ID( "VoxelGameMode.PlayerTookBodyBack", "[Voxel] the player took the body back" );
        if ( pAIObject != nullptr )
            destroySpawned( manager, _autoPlayController );
    }

    bool VoxelGameModeComponent::applyBlockEdit( const VoxelCoord& coord, VoxelBlockIndex block )
    {
        // 바뀐 청크는 월드가 표시합니다. 경계면에 맞닿은 이웃 청크도 표시합니다. 표시된 청크는 다음 틱에 다시 메싱하도록 요청합니다.
        return _world.setBlock( coord, block );
    }

    float3 VoxelGameModeComponent::findSpawnPosition() const
    {
        // 가운데에서 시작해 나선으로 찾은, 물이 아닌 첫 땅입니다.
        // 게임이 시작되었다면 카탈로그와 블록 이름은 startGame 이 이미 확인했습니다.
        const VoxelBlockIndex water   = _world.getCatalog()->findBlockIndex( _waterBlock );
        const int32           centerX = _world.getSizeX() / 2;
        const int32           centerZ = _world.getSizeZ() / 2;
        for ( int32 radius = 0; radius < _world.getSizeX() / 2; ++radius )
        {
            for ( int32 dz = -radius; dz <= radius; ++dz )
            {
                for ( int32 dx = -radius; dx <= radius; ++dx )
                {
                    if ( MathUtil::max( MathUtil::abs( dx ), MathUtil::abs( dz ) ) != radius )
                        continue;
                    const int32           x            = centerX + dx;
                    const int32           z            = centerZ + dz;
                    const optional<int32> topY         = _world.findTopSolidY( x, z );
                    const bool            bOpenAirSpot = topY.has_value() && _world.getBlock( x, *topY + 1, z ) != water && _world.getBlock( x, *topY + 1, z ) == kVoxelAirBlock;
                    if ( bOpenAirSpot )
                        return float3{ static_cast<float32>( x ) + 0.5f, static_cast<float32>( *topY + 1 ) + 0.01f, static_cast<float32>( z ) + 0.5f };
                }
            }
        }
        return float3{ static_cast<float32>( centerX ) + 0.5f, static_cast<float32>( kVoxelChunkHeight ) - 2.0f, static_cast<float32>( centerZ ) + 0.5f };
    }

    void VoxelGameModeComponent::serializeState( StateIO& io )
    {
        io.header( VoxelGameModeComponentInternal::kStateTag, VoxelGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        io.value( _world );
        io.end();
    }

    void VoxelGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO_ID( "VoxelGameMode.WorldStateRestoredChunks", "[Voxel] world state restored - %# chunks", _world.getChunkCountX() * _world.getChunkCountZ() );
        else
            SW_LOG_WARNING_ID( "VoxelGameMode.SavedWorldStateDoes", "[Voxel] the saved world state does not match this build - keeping the seeded island" );
    }

    // ------------------------------------------------------------------------------
    // 지형
    // ------------------------------------------------------------------------------
    bool VoxelGameModeComponent::startGame()
    {
        const VoxelBlockCatalog* pCatalog = game::findSubsystem<const VoxelBlockCatalog>();
        if ( pCatalog == nullptr || pCatalog->getBlocks().empty() )
        {
            SW_LOG_ERROR_ID( "VoxelGameMode.BlockCatalogNotLoaded", "[Voxel] the block catalog is not loaded - the world cannot start" );
            return false;
        }
        bool bBlocksKnown = true;
        for ( const hashed_string* pBlockID : { &_waterBlock, &_stoneBlock, &_grassBlock, &_snowBlock, &_coalOreBlock, &_ironOreBlock } )
        {
            if ( pCatalog->findBlockIndex( *pBlockID ) != kVoxelAirBlock )
                continue;
            SW_LOG_ERROR_ID( "VoxelGameMode.DirectorBlockNotBlock", "[Voxel] the game mode's block '%#' is not in the block catalog - the world cannot start", pBlockID->c_str() );
            bBlocksKnown = false;
        }
        if ( bBlocksKnown == false )
            return false;
        if ( _world.initialize( _chunkCountX, _chunkCountZ, pCatalog ) == false )
            return false;
        // 광석 · 눈도 지형 생성기가 같은 씨앗으로 놓는다(감사 K-8). 깊을수록 철이다.
        VoxelTerrainSettings settings;
        settings._seed         = static_cast<uint32>( _terrainSeed );
        settings._stoneBlock   = _stoneBlock;
        settings._grassBlock   = _grassBlock;
        settings._snowBlock    = _snowBlock;
        settings._snowHeight   = _snowHeight;
        settings._listOreLayer = {
            VoxelOreLayer{_coalOreBlock, _coalOreChance, kVoxelChunkHeight},
            VoxelOreLayer{_ironOreBlock, _ironOreChance, _ironOreMaxHeight}
        };
        // 지형은 청크를 올릴 때 생성한다(생성 훅). 처음에는 스폰 자리를 찾는 월드 가운데 둘레를 한 번에 올린다 — 몸을 놓기 전이다.
        _world.setChunkGenerator( VoxelTerrainSettings::makeChunkGenerator( settings ) );
        _centerChunkX = VoxelWorld::toChunkCoord( _world.getSizeX() / 2 );
        _centerChunkZ = VoxelWorld::toChunkCoord( _world.getSizeZ() / 2 );
        (void)_world.updateResidency( _centerChunkX, _centerChunkZ, _residentChunkRadius, _world.getChunkCountX() * _world.getChunkCountZ() );
        SW_LOG_INFO_ID( "VoxelGameMode.TerrainXXHeights", "[Voxel] terrain %# x %# x %# - %# chunks resident within %# of chunk (%#, %#)", _world.getSizeX(), _world.getSizeY(),
                        _world.getSizeZ(), _world.getResidentChunkCount(), _residentChunkRadius, _centerChunkX, _centerChunkZ );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 청크: 스폰은 틱이 끝난 뒤 게임 스레드에서, 다시 메싱하라는 요청은 PrePhysics 에서 합니다.
    // ------------------------------------------------------------------------------
    void VoxelGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews || _bChunkViewsStale != SW_FALSE )
            syncChunkViews( manager );
        if ( _atlasGate.isPending() )
            checkAtlasGrid( manager );
        if ( _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 ) )
            syncAutoPlayPossession( manager );
    }

    void VoxelGameModeComponent::updateResidency()
    {
        // 몸을 놓기 전에는 처음 올린 월드 가운데 둘레를 그대로 둔다. 상주 반경은 몸 · 광선이 닿는 거리보다 넓다(씬 데이터, 최소 2).
        const VoxelPlayerComponent* pPlayer = findOn<VoxelPlayerComponent>( _player );
        if ( pPlayer != nullptr && pPlayer->isBodyPlaced() )
        {
            const float3& position = pPlayer->getBody().getPosition();
            const int32   centerX  = VoxelWorld::toChunkCoord( static_cast<int32>( MathUtil::floor( position._x ) ) );
            const int32   centerZ  = VoxelWorld::toChunkCoord( static_cast<int32>( MathUtil::floor( position._z ) ) );
            if ( centerX != _centerChunkX || centerZ != _centerChunkZ )
                _bChunkViewsStale = SW_TRUE;
            _centerChunkX = centerX;
            _centerChunkZ = centerZ;
        }
        if ( _world.updateResidency( _centerChunkX, _centerChunkZ, _residentChunkRadius, _chunkLoadsPerFrame ) > 0 )
            _bChunkViewsStale = SW_TRUE;
    }

    void VoxelGameModeComponent::checkAtlasGrid( GameObjectManager& manager )
    {
        if ( _mapChunkView.empty() )
            return;
        const GameObject*          pObject = manager.resolveGameObject( _mapChunkView.begin()->second );
        const VoxelChunkComponent* pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
        if ( pChunk == nullptr )
            return;
        MeshComponent* pOpaque = nullptr;
        MeshComponent* pWater  = nullptr;
        pChunk->findMeshes( pOpaque, pWater );
        const Material* pOpaqueMaterial = pOpaque != nullptr ? pOpaque->getMaterial() : nullptr;
        const Material* pWaterMaterial  = pWater != nullptr ? pWater->getMaterial() : nullptr;
        if ( pOpaqueMaterial == nullptr || pWaterMaterial == nullptr )
            return; // 아직 준비되지 않았다 — 다음 플러시에서 다시 본다(그동안 청크는 그리지 않는다)
        // 프로퍼티는 게임 스레드 초기화(`Ready`)가 끝나야 채워진다 — 그 전에는 견줄 값이 없으니 다음 플러시에서 다시 본다.
        const bool bMaterialsReady = pOpaqueMaterial->getInitState() == MaterialInitState::Ready && pWaterMaterial->getInitState() == MaterialInitState::Ready;
        const bool bCanDraw        = _atlasGate.update( *_world.getCatalog(), bMaterialsReady, static_cast<const float4*>( pOpaqueMaterial->getParameterData( "_atlasGrid" ) ),
                                                        static_cast<const float4*>( pWaterMaterial->getParameterData( "_atlasGrid" ) ), pOpaque->getMaterialPath().c_str() );
        if ( bCanDraw == false )
            return;
        for ( const auto& entry : _mapChunkView )
        {
            const GameObject*    pViewObject = manager.resolveGameObject( entry.second );
            VoxelChunkComponent* pView       = pViewObject != nullptr ? pViewObject->getComponent<VoxelChunkComponent>() : nullptr;
            if ( pView != nullptr )
                pView->requestRebuild();
        }
    }

    void VoxelGameModeComponent::syncChunkViews( GameObjectManager& manager )
    {
        // 그리는 청크는 상주 반경보다 한 칸 안쪽이다 — 메싱이 읽는 이웃 청크가 늘 올라와 있어 경계에 가짜 벽이 생기지 않는다.
        _bChunkViewsStale         = SW_FALSE;
        const int32    viewRadius = _residentChunkRadius - 1;
        vector<uint64> listGoneKey;
        for ( const auto& entry : _mapChunkView )
        {
            const int32 chunkX = VoxelGameModeComponentInternal::toChunkX( entry.first );
            const int32 chunkZ = VoxelGameModeComponentInternal::toChunkZ( entry.first );
            const bool  bShown = VoxelGameModeComponentInternal::computeDistance( chunkX, chunkZ, _centerChunkX, _centerChunkZ ) <= viewRadius && _world.isChunkResident( chunkX, chunkZ );
            if ( bShown == false )
                listGoneKey.push_back( entry.first );
        }
        for ( const uint64 key : listGoneKey )
        {
            const auto viewIt = _mapChunkView.find( key );
            destroySpawned( manager, viewIt->second );
            _mapChunkView.erase( viewIt );
        }
        const GameObjectHandle gameMode = getOwner()->getHandle();
        for ( int32 chunkZ = _centerChunkZ - viewRadius; chunkZ <= _centerChunkZ + viewRadius; ++chunkZ )
        {
            for ( int32 chunkX = _centerChunkX - viewRadius; chunkX <= _centerChunkX + viewRadius; ++chunkX )
            {
                const uint64 key = VoxelWorld::makeChunkKey( chunkX, chunkZ );
                if ( _world.isChunkResident( chunkX, chunkZ ) == false || _mapChunkView.contains( key ) )
                    continue;
                GameObject*          pObject = spawnPrefab( manager, _chunkPrefab, "VoxelChunk" );
                VoxelChunkComponent* pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
                MeshComponent*       pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
                if ( pChunk == nullptr || pMesh == nullptr )
                {
                    if ( pObject != nullptr )
                        manager.destroyGameObject( pObject );
                    continue;
                }
                pMesh->setLocalPosition( float3{ static_cast<float32>( chunkX * kVoxelChunkSize ), 0.0f, static_cast<float32>( chunkZ * kVoxelChunkSize ) } );
                pChunk->assignChunk( gameMode, chunkX, chunkZ );
                pChunk->requestRebuild();
                _world.clearChunkDirty( chunkX, chunkZ ); // 방금 메싱을 요청했다
                _mapChunkView[key] = pObject->getHandle();
            }
        }
    }

    void VoxelGameModeComponent::scheduleRebuilds()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        // 몸에서 가까운 청크부터 요청해서, 블록을 부순 곳이 먼저 다시 메싱됩니다. 요청한 청크는 표시를 지웁니다. 청크는 같은 프레임의 뒤쪽 그룹에서 메싱합니다.
        // 뷰가 아직 없는 청크는 건너뛴다 — 플러시가 뷰를 스폰하며 메싱을 요청한다.
        int32 builtCount = 0;
        for ( int32 ring = 0; ring < _residentChunkRadius && builtCount < _chunkBuildsPerFrame; ++ring )
        {
            for ( int32 chunkZ = _centerChunkZ - ring; chunkZ <= _centerChunkZ + ring && builtCount < _chunkBuildsPerFrame; ++chunkZ )
            {
                for ( int32 chunkX = _centerChunkX - ring; chunkX <= _centerChunkX + ring && builtCount < _chunkBuildsPerFrame; ++chunkX )
                {
                    const bool bOnRing = VoxelGameModeComponentInternal::computeDistance( chunkX, chunkZ, _centerChunkX, _centerChunkZ ) == ring;
                    if ( bOnRing == false || _world.isChunkDirty( chunkX, chunkZ ) == false )
                        continue;
                    const auto viewIt = _mapChunkView.find( VoxelWorld::makeChunkKey( chunkX, chunkZ ) );
                    if ( viewIt == _mapChunkView.end() )
                        continue;
                    _world.clearChunkDirty( chunkX, chunkZ );
                    const GameObject*    pObject = pManager->resolveGameObject( viewIt->second );
                    VoxelChunkComponent* pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
                    if ( pChunk != nullptr )
                        pChunk->requestRebuild();
                    ++builtCount;
                }
            }
        }
    }

    void VoxelGameModeComponent::logStatus( float32 deltaTime )
    {
        _statusTimer += deltaTime;
        if ( _statusTimer < _statusLogInterval )
            return;
        _statusTimer                        = 0.0f;
        const VoxelPlayerComponent* pPlayer = findOn<VoxelPlayerComponent>( _player );
        if ( pPlayer == nullptr )
            return;
        const VoxelBody&                      body     = pPlayer->getBody();
        [[maybe_unused]] const float3&        position = body.getPosition();
        [[maybe_unused]] const VoxelBlockDef* pBlock   = _world.getCatalog()->findBlock( pPlayer->getHotbar().getSelectedSlot()._block );
        SW_LOG_INFO_ID( "VoxelGameMode.BrokenPlacedHandX", "[Voxel] at (%#, %#, %#)%#%# · broken %# · placed %# · hand %# x%#", static_cast<int32>( position._x ), static_cast<int32>( position._y ),
                        static_cast<int32>( position._z ), body.isOnGround() ? " on ground" : "", body.isInWater() ? " swimming" : "", pPlayer->getBrokenCount(),
                        pPlayer->getPlacedCount(), pBlock != nullptr ? pBlock->_name.c_str() : "empty", pPlayer->getHotbar().getSelectedSlot()._count );
    }

} // namespace sw
