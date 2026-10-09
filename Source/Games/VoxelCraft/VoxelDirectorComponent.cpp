#include "pch.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"
#include "GameFramework/Base/Actor/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelTerrain.h"

#include "Games/VoxelCraft/VoxelAutoPlayControllerComponent.h"
#include "Games/VoxelCraft/VoxelChunkComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelDirector" );

    namespace
    {
        struct VoxelDirectorComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "VOXL" );
            static constexpr uint32 kStateVersion = 1;

            static uint32 hashCoord( int32 x, int32 y, int32 z )
            {
                uint32 value = static_cast<uint32>( x ) * 73856093u ^ static_cast<uint32>( y ) * 19349663u ^ static_cast<uint32>( z ) * 83492791u;
                value ^= value >> 13;
                value *= 0x5bd1e995u;
                value ^= value >> 15;
                return value;
            }
        };
    } // namespace

    /** @brief `-gv_voxelAutoPlay=1` — 디렉터의 자동 플레이를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도). 걷고 뛰고 부수고 놓기를 AI 가 한다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_voxelAutoPlay, 0, "VoxelCraft: 걷기 · 부수기 · 놓기도 AI 가 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_voxelAutoPlay, "VoxelCraft", "Walk, break and place blocks by AI" );
} // namespace sw

namespace sw
{
    VoxelDirectorComponent::VoxelDirectorComponent()
        : _chunkPrefab{}
        , _player{}
        , _terrainSeed{ 20261003 }
        , _chunkBuildsPerFrame{ 4 }
        , _statusLogInterval{ 5.0f }
        , _world{}
        , _listChunk{}
        , _autoPlayController{}
        , _statusTimer{ 0.0f }
        , _appliedAutoPlay{ -1 }
    {
    }

    VoxelDirectorComponent::~VoxelDirectorComponent() = default;

    void VoxelDirectorComponent::onGameStarted()
    {
        SW_LOG_INFO( "[Voxel] world is ready - WASD move, mouse look, Space jump, Shift sprint, LMB hold to break, RMB place, 1-9 or wheel hotbar, Esc mouse" );
    }

    void VoxelDirectorComponent::tickGame( float32 deltaTime )
    {
        if ( areViewsSpawned() == false )
            return; // 상태 저장 전에 걷었다 — 베이스가 청크를 다시 세운다(블록은 그대로)
        scheduleRebuilds();
        logStatus( deltaTime );
    }

    void VoxelDirectorComponent::onViewsDespawned()
    {
        _listChunk.clear();
        // 자동 플레이 AI 오브젝트도 걷혔다 — 다시 세울 때 빙의를 다시 맞춘다.
        _autoPlayController = GameObjectHandle{};
        _appliedAutoPlay    = -1;
    }

    bool VoxelDirectorComponent::hasPendingSpawn() const
    {
        return _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 );
    }

    void VoxelDirectorComponent::syncAutoPlayPossession( GameObjectManager& manager )
    {
        const bool     bAutoPlay     = isAutoPlayOn();
        GameObject*    pPlayerObject = manager.resolveGameObject( _player );
        PawnComponent* pPawn         = pPlayerObject != nullptr ? pPlayerObject->getComponent<PawnComponent>() : nullptr;
        _appliedAutoPlay             = bAutoPlay ? 1 : 0; // 폰이 없어도 맞춘 것으로 친다 — 매 틱 플러시를 잡지 않게
        if ( pPawn == nullptr )
            return;
        GameObject* pAiObject = manager.resolveGameObject( _autoPlayController );
        if ( bAutoPlay )
        {
            if ( pAiObject == nullptr )
            {
                pAiObject = manager.createGameObject( hashed_string( "VoxelAutoPlay" ) );
                if ( pAiObject == nullptr || pAiObject->addComponent<VoxelAutoPlayControllerComponent>() == nullptr )
                    return;
                trackSpawned( *pAiObject );
                _autoPlayController = pAiObject->getHandle();
            }
            VoxelAutoPlayControllerComponent* pAi = pAiObject->getComponent<VoxelAutoPlayControllerComponent>();
            if ( pAi != nullptr && pAi->getPawn() != pPawn->getHandle() )
                pAi->possess( *pPawn );
            SW_LOG_INFO( "[Voxel] auto play took the player" );
            return;
        }
        // 끔 — 플레이어 조종자에게 돌려주고 AI 를 걷는다. 이미 다른 조종자(플레이어)가 쥐었으면 그대로.
        const VoxelAutoPlayControllerComponent* pAi = pAiObject != nullptr ? pAiObject->getComponent<VoxelAutoPlayControllerComponent>() : nullptr;
        if ( pPawn->isPossessed() == false || ( pAi != nullptr && pAi->getPawn() == pPawn->getHandle() ) )
        {
            PlayerControllerComponent* pPlayer = ControlSystem::findOrCreatePlayerController( manager, 0 );
            if ( pPlayer != nullptr )
                pPlayer->possess( *pPawn );
            SW_LOG_INFO( "[Voxel] the player took the body back" );
        }
        if ( pAiObject != nullptr )
            destroySpawned( manager, _autoPlayController );
    }

    bool VoxelDirectorComponent::applyBlockEdit( const VoxelCoord& coord, VoxelBlockIndex block )
    {
        // 바뀐 청크(경계면 이웃 청크도)는 월드가 표시하고, 다음 틱에 다시 짓기를 맡긴다.
        return _world.setBlock( coord, block );
    }

    float3 VoxelDirectorComponent::findSpawnPosition() const
    {
        // 가운데부터 나선으로 — 물 위가 아닌 첫 땅.
        const VoxelBlockCatalog* pCatalog = _world.getCatalog();
        const VoxelBlockIndex    water    = pCatalog != nullptr ? pCatalog->findBlockIndex( hashed_string( "water" ) ) : kVoxelAirBlock;
        const int32              centerX  = _world.getSizeX() / 2;
        const int32              centerZ  = _world.getSizeZ() / 2;
        for ( int32 radius = 0; radius < _world.getSizeX() / 2; ++radius )
        {
            for ( int32 dz = -radius; dz <= radius; ++dz )
            {
                for ( int32 dx = -radius; dx <= radius; ++dx )
                {
                    if ( MathUtil::max( MathUtil::abs( dx ), MathUtil::abs( dz ) ) != radius )
                        continue;
                    const int32 x    = centerX + dx;
                    const int32 z    = centerZ + dz;
                    const int32 topY = _world.findTopSolidY( x, z );
                    if ( topY >= 0 && _world.getBlock( x, topY + 1, z ) != water && _world.getBlock( x, topY + 1, z ) == kVoxelAirBlock )
                        return float3{ static_cast<float32>( x ) + 0.5f, static_cast<float32>( topY + 1 ) + 0.01f, static_cast<float32>( z ) + 0.5f };
                }
            }
        }
        return float3{ static_cast<float32>( centerX ) + 0.5f, static_cast<float32>( kVoxelChunkHeight ) - 2.0f, static_cast<float32>( centerZ ) + 0.5f };
    }

    void VoxelDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, VoxelDirectorComponentInternal::kStateTag, VoxelDirectorComponentInternal::kStateVersion );
        _world.writeState( outArchive );
    }

    bool VoxelDirectorComponent::readState( Archive& archive )
    {
        if ( StateArchiveUtil::readHeader( archive, VoxelDirectorComponentInternal::kStateTag, VoxelDirectorComponentInternal::kStateVersion ) == false )
            return false;
        return _world.readState( archive ) && archive.getRemainingBytes() == 0;
    }

    void VoxelDirectorComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            SW_LOG_INFO( "[Voxel] world state restored - %# chunks", _world.getChunkCountX() * _world.getChunkCountZ() );
        else
            SW_LOG_WARNING( "[Voxel] the saved world state does not match this build - keeping the seeded island" );
    }

    // ------------------------------------------------------------------------------
    // 지형
    // ------------------------------------------------------------------------------
    bool VoxelDirectorComponent::startGame()
    {
        const VoxelBlockCatalog* pCatalog = game::getService<VoxelBlockCatalog>();
        if ( pCatalog == nullptr || pCatalog->getBlocks().empty() )
        {
            SW_LOG_WARNING( "[Voxel] the block catalog is not loaded - the world cannot start" );
            return false;
        }
        _world.initialize( kChunkCountX, kChunkCountZ, pCatalog );
        VoxelTerrainSettings settings;
        settings._seed                                   = static_cast<uint32>( _terrainSeed );
        [[maybe_unused]] const VoxelTerrainReport report = VoxelTerrainGenerator::fillWorld( _world, settings );
        decorateTerrain();
        SW_LOG_INFO( "[Voxel] terrain %# x %# x %# - heights %#..%#, %# trees", _world.getSizeX(), _world.getSizeY(), _world.getSizeZ(), report._minHeight,
                     report._maxHeight, report._treeCount );
        return true;
    }

    void VoxelDirectorComponent::decorateTerrain()
    {
        // 돌 속 광석(깊을수록 철) · 높은 봉우리의 눈. 지형 생성기는 장르 공통이라 이런 꾸밈은 게임이 얹는다.
        const VoxelBlockCatalog& catalog = *_world.getCatalog();
        const VoxelBlockIndex    stone   = catalog.findBlockIndex( hashed_string( "stone" ) );
        const VoxelBlockIndex    coal    = catalog.findBlockIndex( hashed_string( "coal_ore" ) );
        const VoxelBlockIndex    iron    = catalog.findBlockIndex( hashed_string( "iron_ore" ) );
        const VoxelBlockIndex    grass   = catalog.findBlockIndex( hashed_string( "grass" ) );
        const VoxelBlockIndex    snow    = catalog.findBlockIndex( hashed_string( "snow" ) );
        for ( int32 z = 0; z < _world.getSizeZ(); ++z )
        {
            for ( int32 x = 0; x < _world.getSizeX(); ++x )
            {
                const int32 topY = _world.findTopSolidY( x, z );
                if ( topY >= 34 && _world.getBlock( x, topY, z ) == grass && snow != kVoxelAirBlock )
                    (void)_world.setBlock( x, topY, z, snow );
                for ( int32 y = 1; y < topY - 3; ++y )
                {
                    if ( _world.getBlock( x, y, z ) != stone )
                        continue;
                    const uint32 roll = VoxelDirectorComponentInternal::hashCoord( x, y, z ) % 1000u;
                    if ( roll < 12u && coal != kVoxelAirBlock )
                        (void)_world.setBlock( x, y, z, coal );
                    else if ( roll < 18u && y < 12 && iron != kVoxelAirBlock )
                        (void)_world.setBlock( x, y, z, iron );
                }
            }
        }
    }

    // ------------------------------------------------------------------------------
    // 청크 — 스폰은 틱 뒤 게임 스레드, 다시 짓기 지시는 PrePhysics
    // ------------------------------------------------------------------------------
    void VoxelDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
            spawnChunks( manager );
        if ( hasPendingSpawn() )
            syncAutoPlayPossession( manager );
    }

    void VoxelDirectorComponent::spawnChunks( GameObjectManager& manager )
    {
        // 청크마다 오브젝트 하나 — 처음에는 전부 짓는다(이후는 바뀐 것만 한 프레임에 몇 개씩).
        const GameObjectHandle director = getOwner()->getHandle();
        _listChunk.assign( static_cast<size_t>( kChunkCountX * kChunkCountZ ), GameObjectHandle{} );
        for ( int32 chunkZ = 0; chunkZ < kChunkCountZ; ++chunkZ )
        {
            for ( int32 chunkX = 0; chunkX < kChunkCountX; ++chunkX )
            {
                GameObject*          pObject = spawnPrefab( manager, _chunkPrefab, "VoxelChunk" );
                VoxelChunkComponent* pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
                MeshComponent*       pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
                if ( pChunk == nullptr || pMesh == nullptr )
                {
                    if ( pObject != nullptr )
                        manager.destroyObject( pObject );
                    continue;
                }
                pMesh->setLocalPosition( float3{ static_cast<float32>( chunkX * kVoxelChunkSize ), 0.0f, static_cast<float32>( chunkZ * kVoxelChunkSize ) } );
                pChunk->assignChunk( director, chunkX, chunkZ );
                pChunk->requestRebuild();
                _listChunk[static_cast<size_t>( chunkZ * kChunkCountX + chunkX )] = pObject->getHandle();
            }
        }
        for ( int32 chunkZ = 0; chunkZ < kChunkCountZ; ++chunkZ )
        {
            for ( int32 chunkX = 0; chunkX < kChunkCountX; ++chunkX )
            {
                _world.clearChunkDirty( chunkX, chunkZ ); // 방금 모두 맡겼다
            }
        }
    }

    void VoxelDirectorComponent::scheduleRebuilds()
    {
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        // 몸에서 가까운 청크부터 — 부순 자리가 먼저 다시 지어진다. 맡긴 청크는 표시를 지운다(청크는 같은 프레임 뒤 그룹에서 짓는다).
        const GameObject*           pPlayerObject = pManager->resolveGameObject( _player );
        const VoxelPlayerComponent* pPlayer       = pPlayerObject != nullptr ? pPlayerObject->getComponent<VoxelPlayerComponent>() : nullptr;
        const float3                position      = pPlayer != nullptr ? pPlayer->getBody().getPosition() : float3{ 0.0f, 0.0f, 0.0f };
        const int32                 playerChunkX  = static_cast<int32>( position._x ) / kVoxelChunkSize;
        const int32                 playerChunkZ  = static_cast<int32>( position._z ) / kVoxelChunkSize;
        int32                       builtCount    = 0;
        for ( int32 ring = 0; ring < MathUtil::max( kChunkCountX, kChunkCountZ ) && builtCount < _chunkBuildsPerFrame; ++ring )
        {
            for ( int32 chunkZ = playerChunkZ - ring; chunkZ <= playerChunkZ + ring && builtCount < _chunkBuildsPerFrame; ++chunkZ )
            {
                for ( int32 chunkX = playerChunkX - ring; chunkX <= playerChunkX + ring && builtCount < _chunkBuildsPerFrame; ++chunkX )
                {
                    const bool bOnRing = MathUtil::max( MathUtil::abs( chunkX - playerChunkX ), MathUtil::abs( chunkZ - playerChunkZ ) ) == ring;
                    if ( bOnRing == false || _world.isChunkDirty( chunkX, chunkZ ) == false )
                        continue;
                    _world.clearChunkDirty( chunkX, chunkZ );
                    const GameObject*    pObject = pManager->resolveGameObject( _listChunk[static_cast<size_t>( chunkZ * kChunkCountX + chunkX )] );
                    VoxelChunkComponent* pChunk  = pObject != nullptr ? pObject->getComponent<VoxelChunkComponent>() : nullptr;
                    if ( pChunk != nullptr )
                        pChunk->requestRebuild();
                    ++builtCount;
                }
            }
        }
    }

    void VoxelDirectorComponent::logStatus( float32 deltaTime )
    {
        _statusTimer += deltaTime;
        if ( _statusTimer < _statusLogInterval )
            return;
        _statusTimer                         = 0.0f;
        GameObjectManager*          pManager = getObjectManager();
        const GameObject*           pObject  = pManager != nullptr ? pManager->resolveGameObject( _player ) : nullptr;
        const VoxelPlayerComponent* pPlayer  = pObject != nullptr ? pObject->getComponent<VoxelPlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
            return;
        const VoxelBody&                      body     = pPlayer->getBody();
        [[maybe_unused]] const float3&        position = body.getPosition();
        [[maybe_unused]] const VoxelBlockDef* pBlock   = _world.getCatalog()->findBlock( pPlayer->getHotbar().getSelectedSlot()._block );
        SW_LOG_INFO( "[Voxel] at (%#, %#, %#)%#%# · broken %# · placed %# · hand %# x%#", static_cast<int32>( position._x ), static_cast<int32>( position._y ),
                     static_cast<int32>( position._z ), body.isOnGround() ? " on ground" : "", body.isInWater() ? " swimming" : "", pPlayer->getBrokenCount(),
                     pPlayer->getPlacedCount(), pBlock != nullptr ? pBlock->_name.c_str() : "empty", pPlayer->getHotbar().getSelectedSlot()._count );
    }

} // namespace sw
