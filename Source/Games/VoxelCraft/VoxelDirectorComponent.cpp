#include "pch.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Utility/GameAutoplay.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelBlock.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelTerrain.h"

#include "Games/VoxelCraft/VoxelChunkComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelDirector" );

    namespace
    {
        struct VoxelDirectorComponentInternal
        {
            static constexpr float32 kStatusInterval = 5.0f;

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
    SW_TEST_GLOBAL_VARIABLE_INT( gv_voxelAutoPlay, 0, "VoxelCraft: 걷기 · 부수기 · 놓기도 AI 가 (1=켜기)", SW_KEEP_IN_SHIPPING );
    SW_GAME_AUTOPLAY( gv_voxelAutoPlay, "VoxelCraft", "Walk, break and place blocks by AI" );
} // namespace sw

namespace sw
{
    VoxelDirectorComponent::VoxelDirectorComponent()
        : _chunkPrefab{}
        , _player{}
        , _terrainSeed{ 20261003 }
        , _chunkBuildsPerFrame{ 4 }
        , _bAutoPlay{ false }
        , _world{}
        , _listChunk{}
        , _statusTimer{ 0.0f }
        , _bWorldReady{ SW_FALSE }
        , _bChunksSpawned{ SW_FALSE }
        , _bFlushScheduled{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelDirectorComponent::~VoxelDirectorComponent() = default;

    void VoxelDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 다시 짓기 지시는 앞 그룹 — 청크 · 플레이어가 같은 프레임에 월드를 읽는다.
        setTickGroup( TickGroup::PrePhysics );
        initializeWorld();
        if ( _bWorldReady == SW_FALSE )
            return;
        scheduleFlush();
        SW_LOG_INFO( "[Voxel] world is ready - WASD move, mouse look, Space jump, Shift sprint, LMB hold to break, RMB place, 1-9 or wheel hotbar, Esc mouse" );
    }

    void VoxelDirectorComponent::onEndPlay()
    {
        despawnRuntime();
        _bWorldReady = SW_FALSE;
        Component::onEndPlay();
    }

    void VoxelDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bWorldReady == SW_FALSE )
            return;
        if ( _bChunksSpawned == SW_FALSE )
        {
            scheduleFlush(); // 상태 저장 전에 걷었다 — 청크를 다시 세운다(블록은 그대로)
            return;
        }
        scheduleRebuilds();
        logStatus( deltaTime );
    }

    void VoxelDirectorComponent::despawnRuntime()
    {
        GameObjectManager* pManager = getObjectManager();
        for ( const GameObjectHandle& handle : _listChunk )
        {
            GameObject* pObject = pManager != nullptr ? pManager->resolveGameObject( handle ) : nullptr;
            if ( pObject != nullptr )
                pManager->destroyObject( pObject );
        }
        _listChunk.clear();
        _bChunksSpawned = SW_FALSE;
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

    bool VoxelDirectorComponent::isAutoPlayOn() const
    {
        return _bAutoPlay || GameAutoplay::isOn();
    }

    const VoxelDirectorComponent* VoxelDirectorComponent::resolveDirector( const GameObjectManager& manager, GameObjectHandle director )
    {
        const GameObject* pObject = manager.resolveGameObject( director );
        return pObject != nullptr ? pObject->getComponent<VoxelDirectorComponent>() : nullptr;
    }

    // ------------------------------------------------------------------------------
    // 지형
    // ------------------------------------------------------------------------------
    void VoxelDirectorComponent::initializeWorld()
    {
        const VoxelBlockCatalog* pCatalog = game::getService<VoxelBlockCatalog>();
        if ( pCatalog == nullptr || pCatalog->getBlocks().empty() )
        {
            SW_LOG_WARNING( "[Voxel] the block catalog is not loaded - the world cannot start" );
            return;
        }
        _world.initialize( kChunkCountX, kChunkCountZ, pCatalog );
        VoxelTerrainSettings settings;
        settings._seed                                   = static_cast<uint32>( _terrainSeed );
        [[maybe_unused]] const VoxelTerrainReport report = VoxelTerrainGenerator::fillWorld( _world, settings );
        decorateTerrain();
        _bWorldReady = SW_TRUE;
        SW_LOG_INFO( "[Voxel] terrain %# x %# x %# - heights %#..%#, %# trees", _world.getSizeX(), _world.getSizeY(), _world.getSizeZ(), report._minHeight,
                     report._maxHeight, report._treeCount );
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
    void VoxelDirectorComponent::scheduleFlush()
    {
        if ( _bFlushScheduled == SW_TRUE )
            return;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr )
            return;
        _bFlushScheduled = SW_TRUE;
        // 틱 안이면 틱 뒤로 미뤄진다. 그 사이에 디렉터가 사라질 수 있으니 핸들로 다시 찾는다.
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            VoxelDirectorComponent* pDirector = static_cast<VoxelDirectorComponent*>( pManager->resolveComponent( self ) );
            if ( pDirector != nullptr )
                pDirector->flushPending();
        } );
    }

    void VoxelDirectorComponent::flushPending()
    {
        _bFlushScheduled            = SW_FALSE;
        GameObjectManager* pManager = getObjectManager();
        if ( pManager == nullptr || _bWorldReady == SW_FALSE || _bChunksSpawned == SW_TRUE )
            return;
        spawnChunks( *pManager );
    }

    void VoxelDirectorComponent::spawnChunks( GameObjectManager& manager )
    {
        AssetManager* pAssetManager = game::getService<AssetManager>();
        if ( pAssetManager == nullptr || _chunkPrefab.empty() )
            return;
        // 청크마다 오브젝트 하나 — 처음에는 전부 짓는다(이후는 바뀐 것만 한 프레임에 몇 개씩).
        const GameObjectHandle director = getOwner()->getHandle();
        _listChunk.assign( static_cast<size_t>( kChunkCountX * kChunkCountZ ), GameObjectHandle{} );
        for ( int32 chunkZ = 0; chunkZ < kChunkCountZ; ++chunkZ )
        {
            for ( int32 chunkX = 0; chunkX < kChunkCountX; ++chunkX )
            {
                GameObject*          pObject = pAssetManager->getPrefabCache().spawn( &manager, _chunkPrefab, "VoxelChunk" );
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
                _world.clearChunkDirty( chunkX, chunkZ ); // 방금 모두 맡겼다
        }
        _bChunksSpawned = SW_TRUE;
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
        if ( _statusTimer < VoxelDirectorComponentInternal::kStatusInterval )
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

    GameObjectManager* VoxelDirectorComponent::getObjectManager() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getManager() : nullptr;
    }
} // namespace sw
