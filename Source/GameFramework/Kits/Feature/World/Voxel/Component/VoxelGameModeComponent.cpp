#include "pch.h"

#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelAutoPlayControllerComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelPlayerComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelTerrain.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct VoxelGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "VOXL" );
            static constexpr uint32 kStateVersion = 1;
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelGameModeComponent::VoxelGameModeComponent()
        : _autoPlayPrefab{}
        , _player{}
        , _terrainSeed{ 20261003 }
        , _chunkCountX{ 0 }
        , _chunkCountZ{ 0 }
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
        , _autoPlayController{}
        , _statusTimer{ 0.0f }
        , _centerChunkX{ 0 }
        , _centerChunkZ{ 0 }
        , _appliedAutoPlay{ -1 }
    {
    }

    VoxelGameModeComponent::~VoxelGameModeComponent() = default;

    void VoxelGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "VoxelGameMode.WorldReadyWASDMove", "[Voxel] world is ready - WASD move, mouse look, Space jump, Shift sprint, LMB hold to break, RMB place, 1-9 or wheel hotbar, Esc mouse" );
    }

    void VoxelGameModeComponent::updateGame( float32 deltaTime )
    {
        updateResidency();
        SW_PROFILE_COUNT( "GT.VoxelWorld.residentChunks", _world.getResidentChunkCount() ); // 감사 K-12 전후 비교
        SW_PROFILE_COUNT( "GT.VoxelWorld.storedChunks", _world.getStoredChunkCount() );
        logStatus( deltaTime );
    }

    void VoxelGameModeComponent::onViewsDespawned()
    {
        // 자동 플레이 AI 오브젝트도 정리되었습니다. 다시 스폰할 때 빙의를 다시 맞춥니다.
        _autoPlayController = GameObjectHandle{};
        _appliedAutoPlay    = -1;
    }

    bool VoxelGameModeComponent::hasPendingSpawn() const
    {
        return _appliedAutoPlay != ( isAutoPlayOn() ? 1 : 0 );
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
        // 바뀐 청크와 경계면에 맞닿은 이웃 청크의 판 번호는 월드가 올립니다. 뷰가 그 번호를 보고 다시 메싱합니다.
        return _world.setBlock( coord, block );
    }

    float3 VoxelGameModeComponent::findSpawnPosition() const
    {
        // 가운데에서 시작해 나선으로 찾은, 물이 아닌 첫 땅입니다.
        // 게임이 시작되었다면 카탈로그와 블록 이름은 startGame 이 이미 확인했습니다.
        const VoxelBlockIndex water   = _world.getCatalog()->findBlockIndex( _waterBlock.key() ).value(); // startGame 이 확인했다 — 없으면 멈춘다
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

    void VoxelGameModeComponent::serializeGameState( StateIO& io )
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
        for ( const CatalogKey<VoxelBlockDef>* pBlockID : { &_waterBlock, &_stoneBlock, &_grassBlock, &_snowBlock, &_coalOreBlock, &_ironOreBlock } )
        {
            if ( pCatalog->findBlockIndex( pBlockID->key() ).has_value() )
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
        settings._stoneBlock   = _stoneBlock.key();
        settings._grassBlock   = _grassBlock.key();
        settings._snowBlock    = _snowBlock.key();
        settings._snowHeight   = _snowHeight;
        settings._listOreLayer = {
            VoxelOreLayer{_coalOreBlock.key(), _coalOreChance, kVoxelChunkHeight},
            VoxelOreLayer{_ironOreBlock.key(), _ironOreChance, _ironOreMaxHeight}
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
    // 청크 상주와 플러시(틱 뒤 게임 스레드)
    // ------------------------------------------------------------------------------
    void VoxelGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)bRespawnViews; // 스폰하는 것은 자동 플레이 조종자뿐이고 적용 상태로 안다
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
            _centerChunkX          = VoxelWorld::toChunkCoord( static_cast<int32>( MathUtil::floor( position._x ) ) );
            _centerChunkZ          = VoxelWorld::toChunkCoord( static_cast<int32>( MathUtil::floor( position._z ) ) );
        }
        (void)_world.updateResidency( _centerChunkX, _centerChunkZ, _residentChunkRadius, _chunkLoadsPerFrame ); // 바뀐 수는 쓰지 않는다 — 뷰는 올라온 청크를 직접 본다
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
