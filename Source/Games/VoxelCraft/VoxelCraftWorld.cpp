#include "pch.h"

#include "Games/VoxelCraft/VoxelCraftWorld.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Framework/GameService.h"
#include "GameFramework/Framework/GameSound.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelTerrain.h"

namespace sw
{
    SW_LOG_CALLER( "VoxelCraftWorld" );

    namespace
    {
        struct VoxelCraftWorldInternal
        {
            static constexpr float32     kPi               = 3.14159265f;
            static constexpr float32     kDegToRad         = kPi / 180.0f;
            static constexpr float32     kMouseSensitivity = 0.0022f;
            static constexpr float32     kPlaceInterval    = 0.25f; ///< 누르고 있을 때 놓는 간격(s)
            static constexpr uint32      kTerrainSeed      = 20261003u;
            static constexpr const utf8* kAtlasPath        = "game/voxelcraft/textures/blocks.dds";

            /** @brief 키트 정점 → 엔진 정점입니다(같은 네 속성). */
            static RHIVertex toRhiVertex( const VoxelMeshVertex& source )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = source._position._x;
                vertex._arrPosition[1] = source._position._y;
                vertex._arrPosition[2] = source._position._z;
                vertex._arrNormal[0]   = source._normal._x;
                vertex._arrNormal[1]   = source._normal._y;
                vertex._arrNormal[2]   = source._normal._z;
                vertex._arrUv[0]       = source._uv._x;
                vertex._arrUv[1]       = source._uv._y;
                vertex._arrColor[0]    = source._color._x;
                vertex._arrColor[1]    = source._color._y;
                vertex._arrColor[2]    = source._color._z;
                vertex._arrColor[3]    = source._color._w;
                return vertex;
            }

            /** @brief 부순 블록에서 얻는 블록입니다(풀 → 흙, 기반암 · 물은 없음). */
            static VoxelBlockIndex findDrop( const VoxelBlockCatalog& catalog, VoxelBlockIndex block )
            {
                const VoxelBlockDef* pBlock = catalog.findBlock( block );
                if ( pBlock == nullptr || pBlock->_bBreakable == SW_FALSE )
                    return kVoxelAirBlock;
                if ( pBlock->_id == hashed_string( "grass" ) )
                    return catalog.findBlockIndex( hashed_string( "dirt" ) );
                if ( pBlock->_id == hashed_string( "stone" ) )
                    return catalog.findBlockIndex( hashed_string( "cobblestone" ) );
                return block;
            }

            static uint32 hashCoord( int32 x, int32 y, int32 z )
            {
                uint32 value = static_cast<uint32>( x ) * 73856093u ^ static_cast<uint32>( y ) * 19349663u ^ static_cast<uint32>( z ) * 83492791u;
                value ^= value >> 13;
                value *= 0x5bd1e995u;
                value ^= value >> 15;
                return value;
            }

            static float3 toCenter( const VoxelCoord& coord )
            {
                return float3{ static_cast<float32>( coord._x ) + 0.5f, static_cast<float32>( coord._y ) + 0.5f, static_cast<float32>( coord._z ) + 0.5f };
            }
        };
    } // namespace

    /** @brief `-gv_voxelAutoPlay=1` — 걷고 뛰고 부수고 놓기를 AI 가 합니다(입력 없이 메싱 · 충돌 · 다시 짓기를 돌려 보는 확인). */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_voxelAutoPlay, 0, "VoxelCraft: 걷기 · 부수기 · 놓기도 AI 가 (1=켜기)", SW_KEEP_IN_SHIPPING );
} // namespace sw

namespace sw
{
    VoxelCraftWorld::VoxelCraftWorld()
        : _stage{}
        , _world{}
        , _body{}
        , _hotbar{}
        , _look{}
        , _scratchMesh{}
        , _listChunkView{}
        , _pCatalog{ nullptr }
        , _target{}
        , _highlight{}
        , _breakProgress{ 0.0f }
        , _placeCooldown{ 0.0f }
        , _autoTimer{ 0.0f }
        , _statusTimer{ 0.0f }
        , _highlightLevel{ -1 }
        , _brokenCount{ 0 }
        , _placedCount{ 0 }
        , _bHasTarget{ SW_FALSE }
        , _bMouseLocked{ SW_FALSE }
        , _bSpawned{ SW_FALSE }
    {
    }

    VoxelCraftWorld::~VoxelCraftWorld() = default;

    void VoxelCraftWorld::initialize( const VoxelBlockCatalog* pCatalog )
    {
        _pCatalog = pCatalog;
        _world.initialize( kChunkCountX, kChunkCountZ, pCatalog );
        VoxelTerrainSettings settings;
        settings._seed                                   = VoxelCraftWorldInternal::kTerrainSeed;
        [[maybe_unused]] const VoxelTerrainReport report = VoxelTerrainGenerator::fillWorld( _world, settings );
        decorateTerrain();
        SW_LOG_INFO( "[Voxel] terrain %# x %# x %# - heights %#..%#, %# trees", _world.getSizeX(), _world.getSizeY(), _world.getSizeZ(), report._minHeight,
                     report._maxHeight, report._treeCount );

        _body.setWaterBlock( pCatalog != nullptr ? pCatalog->findBlockIndex( hashed_string( "water" ) ) : kVoxelAirBlock );
        _body.setPosition( findSpawnPosition() );
        _look.setAngles( 0.6f, -0.2f );

        // 처음 핫바 — 짓는 블록 몇 가지.
        const utf8* arrStartBlock[] = { "planks", "cobblestone", "brick", "log", "sand", "leaves" };
        for ( const utf8* pId : arrStartBlock )
            (void)_hotbar.addBlock( pCatalog != nullptr ? pCatalog->findBlockIndex( hashed_string( pId ) ) : kVoxelAirBlock, 64 );
    }

    bool VoxelCraftWorld::spawn()
    {
        if ( _bSpawned != SW_FALSE )
            return true;
        if ( _pCatalog == nullptr || _stage.begin( "VoxelCraft" ) == false )
            return false;

        (void)_stage.createSun( float3{ 0.85f, 0.5f, 0.0f }, 1.4f, 60.0f );
        GameObject* pHighlight = _stage.createPrimitiveObject( "BlockHighlight", "Cube", PrimitiveLook::makeTranslucent( float4{ 1.0f, 1.0f, 1.0f, 0.18f } ),
                                                               float3{ 0.0f }, float3{ 1.02f } );
        _highlight             = pHighlight != nullptr ? pHighlight->getHandle() : GameObjectHandle{};
        _highlightLevel        = -1;

        // 청크 — 처음에는 전부 짓는다(이후는 바뀐 것만 한 프레임에 몇 개씩).
        _listChunkView.assign( static_cast<size_t>( kChunkCountX * kChunkCountZ ), ChunkView{} );
        _world.markAllChunksDirty();
        rebuildDirtyChunks( kChunkCountX * kChunkCountZ );

        InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr && gv_voxelAutoPlay == 0 )
        {
            pInput->setMouseLockMode( MouseLockMode::LockedInCenter );
            pInput->setCursorVisible( false );
            _bMouseLocked = SW_TRUE;
        }
        _bSpawned = SW_TRUE;
        updateCameraAndHighlight();
        SW_LOG_INFO( "[Voxel] world is ready - WASD move, mouse look, Space jump, Shift sprint, LMB hold to break, RMB place, 1-9 or wheel hotbar, Esc mouse" );
        return true;
    }

    void VoxelCraftWorld::despawn()
    {
        InputManager* pInput = game::getService<InputManager>();
        if ( pInput != nullptr && _bMouseLocked != SW_FALSE )
        {
            pInput->setMouseLockMode( MouseLockMode::None );
            pInput->setCursorVisible( true );
        }
        _bMouseLocked = SW_FALSE;
        _stage.clear();
        _listChunkView.clear();
        _bSpawned = SW_FALSE;
    }

    void VoxelCraftWorld::update( float32 deltaTime )
    {
        if ( _stage.isSceneChanged() )
        {
            _stage.forget();
            _listChunkView.clear();
            _bSpawned = SW_FALSE;
        }
        if ( _bSpawned == SW_FALSE && spawn() == false )
            return;
        if ( deltaTime <= 0.0f )
            return;

        updateInput( MathUtil::min( deltaTime, 0.1f ), game::getService<InputManager>() );
        rebuildDirtyChunks( kChunkBuildsPerFrame );
        updateCameraAndHighlight();
        logStatus( deltaTime );
    }

    // ------------------------------------------------------------------------------
    // 지형 · 청크
    // ------------------------------------------------------------------------------
    void VoxelCraftWorld::decorateTerrain()
    {
        // 돌 속 광석(깊을수록 철) · 높은 봉우리의 눈. 지형 생성기는 장르 공통이라 이런 꾸밈은 게임이 얹는다.
        const VoxelBlockIndex stone = _pCatalog->findBlockIndex( hashed_string( "stone" ) );
        const VoxelBlockIndex coal  = _pCatalog->findBlockIndex( hashed_string( "coal_ore" ) );
        const VoxelBlockIndex iron  = _pCatalog->findBlockIndex( hashed_string( "iron_ore" ) );
        const VoxelBlockIndex grass = _pCatalog->findBlockIndex( hashed_string( "grass" ) );
        const VoxelBlockIndex snow  = _pCatalog->findBlockIndex( hashed_string( "snow" ) );
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
                    const uint32 roll = VoxelCraftWorldInternal::hashCoord( x, y, z ) % 1000u;
                    if ( roll < 12u && coal != kVoxelAirBlock )
                        (void)_world.setBlock( x, y, z, coal );
                    else if ( roll < 18u && y < 12 && iron != kVoxelAirBlock )
                        (void)_world.setBlock( x, y, z, iron );
                }
            }
        }
    }

    float3 VoxelCraftWorld::findSpawnPosition() const
    {
        // 가운데부터 나선으로 — 물 위가 아닌 첫 땅.
        const VoxelBlockIndex water   = _pCatalog != nullptr ? _pCatalog->findBlockIndex( hashed_string( "water" ) ) : kVoxelAirBlock;
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

    void VoxelCraftWorld::rebuildDirtyChunks( int32 maxCount )
    {
        if ( _listChunkView.empty() )
            return;
        // 몸에서 가까운 청크부터 — 부순 자리가 먼저 다시 지어진다.
        const int32 playerChunkX = static_cast<int32>( _body.getPosition()._x ) / kVoxelChunkSize;
        const int32 playerChunkZ = static_cast<int32>( _body.getPosition()._z ) / kVoxelChunkSize;
        int32       builtCount   = 0;
        for ( int32 ring = 0; ring < MathUtil::max( kChunkCountX, kChunkCountZ ) && builtCount < maxCount; ++ring )
        {
            for ( int32 chunkZ = playerChunkZ - ring; chunkZ <= playerChunkZ + ring && builtCount < maxCount; ++chunkZ )
            {
                for ( int32 chunkX = playerChunkX - ring; chunkX <= playerChunkX + ring && builtCount < maxCount; ++chunkX )
                {
                    const bool bOnRing = MathUtil::max( MathUtil::abs( chunkX - playerChunkX ), MathUtil::abs( chunkZ - playerChunkZ ) ) == ring;
                    if ( bOnRing == false || _world.isChunkDirty( chunkX, chunkZ ) == false )
                        continue;
                    rebuildChunk( chunkX, chunkZ );
                    ++builtCount;
                }
            }
        }
    }

    void VoxelCraftWorld::rebuildChunk( int32 chunkX, int32 chunkZ )
    {
        VoxelMesher::fillChunkMesh( _world, chunkX, chunkZ, _scratchMesh );
        _world.clearChunkDirty( chunkX, chunkZ );
        ChunkView&   view = _listChunkView[static_cast<size_t>( chunkZ * kChunkCountX + chunkX )];
        const float3 origin{ static_cast<float32>( chunkX * kVoxelChunkSize ), 0.0f, static_cast<float32>( chunkZ * kVoxelChunkSize ) };
        applyChunkMesh( view._opaque, _scratchMesh._listOpaqueVertex, false, origin );
        applyChunkMesh( view._translucent, _scratchMesh._listTranslucentVertex, true, origin );
    }

    void VoxelCraftWorld::applyChunkMesh( GameObjectHandle& inoutHandle, const vector<VoxelMeshVertex>& listVertex, bool bTranslucent, const float3& origin )
    {
        MeshComponent* pMeshComponent = _stage.findMesh( inoutHandle );
        if ( listVertex.empty() )
        {
            if ( pMeshComponent != nullptr )
                pMeshComponent->setVisible( false );
            return;
        }
        // 새 메시를 만들어 건다 — 그리는 중인 정점 버퍼를 덮어쓰지 않는다(옛 것은 컴포넌트가 놓을 때 사라진다).
        vector<RHIVertex> listRhiVertex;
        listRhiVertex.reserve( listVertex.size() );
        for ( const VoxelMeshVertex& vertex : listVertex )
            listRhiVertex.push_back( VoxelCraftWorldInternal::toRhiVertex( vertex ) );
        shared_ptr<Mesh> mesh = Mesh::create();
        if ( mesh == nullptr )
            return;
        mesh->setVertices( std::move( listRhiVertex ) );

        if ( pMeshComponent == nullptr )
        {
            PrimitiveLook look  = bTranslucent ? PrimitiveLook::makeTranslucent( float4{ 1.0f, 1.0f, 1.0f, 1.0f } ) : PrimitiveLook{};
            look._texturePath   = VoxelCraftWorldInternal::kAtlasPath;
            GameObject* pObject = _stage.createMeshObject( bTranslucent ? "VoxelChunkWater" : "VoxelChunk", mesh, look, origin );
            inoutHandle         = pObject != nullptr ? pObject->getHandle() : GameObjectHandle{};
            return;
        }
        pMeshComponent->setMesh( mesh );
        pMeshComponent->setVisible( true );
    }

    // ------------------------------------------------------------------------------
    // 몸 · 손
    // ------------------------------------------------------------------------------
    void VoxelCraftWorld::updateInput( float32 deltaTime, const InputManager* pInput )
    {
        float3 wish{ 0.0f, 0.0f, 0.0f };
        bool   bJump   = false;
        bool   bSprint = false;
        bool   bBreak  = false;
        bool   bPlace  = false;

        if ( gv_voxelAutoPlay != 0 || pInput == nullptr )
            updateAutoPlayer( deltaTime, wish, bJump, bBreak, bPlace );
        else
        {
            if ( pInput->wasKeyPressed( Key::Escape ) )
            {
                _bMouseLocked               = _bMouseLocked != SW_FALSE ? SW_FALSE : SW_TRUE;
                InputManager* pMutableInput = game::getService<InputManager>();
                if ( pMutableInput != nullptr )
                {
                    pMutableInput->setMouseLockMode( _bMouseLocked != SW_FALSE ? MouseLockMode::LockedInCenter : MouseLockMode::None );
                    pMutableInput->setCursorVisible( _bMouseLocked == SW_FALSE );
                }
            }
            if ( _bMouseLocked != SW_FALSE )
            {
                const int2 mouseDelta = pInput->getMouseDelta();
                _look.addMouseDelta( static_cast<float32>( mouseDelta._x ), static_cast<float32>( mouseDelta._y ), VoxelCraftWorldInternal::kMouseSensitivity );
            }
            const float3 forward = _look.getFlatForward();
            const float3 right   = _look.getFlatRight();
            if ( pInput->isKeyDown( Key::W ) )
                wish = wish + forward;
            if ( pInput->isKeyDown( Key::S ) )
                wish = wish - forward;
            if ( pInput->isKeyDown( Key::D ) )
                wish = wish + right;
            if ( pInput->isKeyDown( Key::A ) )
                wish = wish - right;
            bJump   = pInput->isKeyDown( Key::Space );
            bSprint = pInput->isKeyDown( Key::LeftShift );
            bBreak  = pInput->isMouseButtonDown( MouseButton::Left );
            bPlace  = pInput->wasMouseButtonPressed( MouseButton::Right ) || ( pInput->isMouseButtonDown( MouseButton::Right ) && _placeCooldown <= 0.0f );

            constexpr Key kArrSlotKey[VoxelHotbar::kSlotCount] = { Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
                                                                   Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9 };
            const int32   previousSlot                         = _hotbar.getSelectedIndex();
            for ( int32 slotIndex = 0; slotIndex < VoxelHotbar::kSlotCount; ++slotIndex )
            {
                if ( pInput->wasKeyPressed( kArrSlotKey[slotIndex] ) )
                    _hotbar.select( slotIndex );
            }
            const float32 wheel = pInput->getMouseWheel();
            if ( wheel != 0.0f )
                _hotbar.selectRelative( wheel > 0.0f ? -1 : 1 );
            if ( previousSlot != _hotbar.getSelectedIndex() )
            {
                [[maybe_unused]] const VoxelBlockDef* pBlock = _pCatalog->findBlock( _hotbar.getSelectedSlot()._block );
                SW_LOG_INFO( "[Voxel] slot %#: %# x%#", _hotbar.getSelectedIndex() + 1, pBlock != nullptr ? pBlock->_name.c_str() : "empty",
                             _hotbar.getSelectedSlot()._count );
            }
        }

        const bool bWasOnGround = _body.isOnGround();
        _body.step( _world, wish, bJump, bSprint, deltaTime );
        if ( bWasOnGround == false && _body.isOnGround() )
            (void)GameSound::play( "game/voxelcraft/sounds/footstep_grass_000.ogg" );
        // 월드 밖으로 떨어지면 처음 자리로.
        if ( _body.getPosition()._y < -10.0f )
            _body.setPosition( findSpawnPosition() );

        updateTarget();
        updateBreaking( deltaTime, bBreak );
        _placeCooldown -= deltaTime;
        if ( bPlace && _placeCooldown <= 0.0f )
        {
            placeBlock();
            _placeCooldown = VoxelCraftWorldInternal::kPlaceInterval;
        }
    }

    void VoxelCraftWorld::updateAutoPlayer( float32 deltaTime, float3& outWish, bool& outJump, bool& outBreak, bool& outPlace )
    {
        // 앞으로 걷다가 막히면 뛰고, 아래를 조금 보며 몇 초마다 앞 블록을 부수고 놓는다. 가끔 방향을 튼다.
        _autoTimer += deltaTime;
        const float32 cycle = MathUtil::fmod( _autoTimer, 12.0f );
        _look.setAngles( _look.getYaw() + deltaTime * ( cycle < 6.0f ? 0.25f : -0.15f ), -0.35f );
        outWish = _look.getFlatForward();

        const float3 ahead = _body.getPosition() + outWish * 0.6f;
        outJump            = _world.isSolid( static_cast<int32>( MathUtil::floor( ahead._x ) ), static_cast<int32>( MathUtil::floor( _body.getPosition()._y + 0.5f ) ),
                                             static_cast<int32>( MathUtil::floor( ahead._z ) ) ) ||
                  _body.isInWater();
        outBreak = cycle > 3.0f && cycle < 6.5f;
        outPlace = cycle > 9.0f && cycle < 9.0f + deltaTime * 1.5f;
    }

    void VoxelCraftWorld::updateTarget()
    {
        VoxelRayHit hit;
        const bool  bHit = VoxelRaycast::raycast( _world, _body.getEyePosition(), _look.getForward(), kReachDistance, hit );
        if ( bHit == false || hit._block != _target._block || _bHasTarget == SW_FALSE )
            _breakProgress = 0.0f; // 다른 블록을 보면 처음부터
        _bHasTarget = bHit ? SW_TRUE : SW_FALSE;
        if ( bHit )
            _target = hit;
    }

    void VoxelCraftWorld::updateBreaking( float32 deltaTime, bool bBreakHeld )
    {
        if ( bBreakHeld == false || _bHasTarget == SW_FALSE )
        {
            _breakProgress = 0.0f;
            return;
        }
        const VoxelBlockDef* pBlock = _pCatalog->findBlock( _target._blockIndex );
        if ( pBlock == nullptr || pBlock->_bBreakable == SW_FALSE )
            return;
        _breakProgress += deltaTime / MathUtil::max( 0.05f, pBlock->_hardness );
        if ( _breakProgress < 1.0f )
            return;

        const VoxelBlockIndex drop = VoxelCraftWorldInternal::findDrop( *_pCatalog, _target._blockIndex );
        if ( _world.setBlock( _target._block, kVoxelAirBlock ) == false )
            return;
        ++_brokenCount;
        _breakProgress = 0.0f;
        (void)GameSound::play( "game/voxelcraft/sounds/impact_soft_medium_000.ogg" );
        if ( drop != kVoxelAirBlock && _hotbar.addBlock( drop, 1 ) > 0 )
            SW_LOG_INFO( "[Voxel] hotbar is full - the %# is lost", pBlock->_name.c_str() );
        _bHasTarget = SW_FALSE;
    }

    void VoxelCraftWorld::placeBlock()
    {
        if ( _bHasTarget == SW_FALSE )
            return;
        const VoxelCoord cell = _target._previous;
        if ( _body.overlapsBlock( cell ) || _world.isInside( cell._x, cell._y, cell._z ) == false )
            return; // 몸 안 · 월드 밖에는 놓지 않는다
        const VoxelBlockIndex existing = _world.getBlock( cell );
        if ( existing != kVoxelAirBlock && _pCatalog->isSolid( existing ) )
            return;
        VoxelBlockIndex block = kVoxelAirBlock;
        if ( _hotbar.consumeSelected( block ) == false )
            return;
        if ( _world.setBlock( cell, block ) )
        {
            ++_placedCount;
            (void)GameSound::play( "game/voxelcraft/sounds/impact_plank_medium_001.ogg" );
        }
    }

    void VoxelCraftWorld::updateCameraAndHighlight()
    {
        const float3 euler = _look.computeCameraEuler();
        _stage.placeCamerasWithRotation( _body.getEyePosition(), euler, 75.0f * VoxelCraftWorldInternal::kDegToRad, 220.0f );

        MeshComponent* pHighlight = _stage.findMesh( _highlight );
        if ( pHighlight == nullptr )
            return;
        pHighlight->setVisible( _bHasTarget != SW_FALSE );
        if ( _bHasTarget == SW_FALSE )
            return;
        const float3 center = VoxelCraftWorldInternal::toCenter( _target._block );
        pHighlight->setLocalPosition( center );
        // 부수는 동안 진해진다(네 단계 — 인스턴스가 끝없이 늘지 않게).
        const int32 level = static_cast<int32>( MathUtil::clamp( _breakProgress, 0.0f, 0.99f ) * 4.0f );
        if ( level != _highlightLevel )
        {
            _highlightLevel = level;
            _stage.setLook( *pHighlight, PrimitiveLook::makeTranslucent( float4{ 1.0f, 1.0f - 0.2f * static_cast<float32>( level ), 1.0f - 0.2f * static_cast<float32>( level ),
                                                                                 0.18f + 0.15f * static_cast<float32>( level ) } ) );
        }
        // 에디터 게임 뷰에는 테두리도 그린다.
        if ( DebugDrawQueue* pDebugDraw = game::getService<DebugDrawQueue>() )
        {
            const float3 minCorner = center - float3{ 0.505f };
            const float3 maxCorner = center + float3{ 0.505f };
            const float4 color{ 0.05f, 0.05f, 0.05f, 1.0f };
            for ( int32 edge = 0; edge < 4; ++edge )
            {
                const float32 x = ( edge & 1 ) != 0 ? maxCorner._x : minCorner._x;
                const float32 z = ( edge & 2 ) != 0 ? maxCorner._z : minCorner._z;
                pDebugDraw->drawLine( float3{ x, minCorner._y, z }, float3{ x, maxCorner._y, z }, color );
                pDebugDraw->drawLine( float3{ minCorner._x, ( edge & 1 ) != 0 ? maxCorner._y : minCorner._y, ( edge & 2 ) != 0 ? maxCorner._z : minCorner._z },
                                      float3{ maxCorner._x, ( edge & 1 ) != 0 ? maxCorner._y : minCorner._y, ( edge & 2 ) != 0 ? maxCorner._z : minCorner._z }, color );
                pDebugDraw->drawLine( float3{ ( edge & 1 ) != 0 ? maxCorner._x : minCorner._x, ( edge & 2 ) != 0 ? maxCorner._y : minCorner._y, minCorner._z },
                                      float3{ ( edge & 1 ) != 0 ? maxCorner._x : minCorner._x, ( edge & 2 ) != 0 ? maxCorner._y : minCorner._y, maxCorner._z }, color );
            }
        }
    }

    void VoxelCraftWorld::logStatus( float32 deltaTime )
    {
        _statusTimer += deltaTime;
        if ( _statusTimer < 5.0f )
            return;
        _statusTimer                                   = 0.0f;
        [[maybe_unused]] const float3&        position = _body.getPosition();
        [[maybe_unused]] const VoxelBlockDef* pBlock   = _pCatalog->findBlock( _hotbar.getSelectedSlot()._block );
        SW_LOG_INFO( "[Voxel] at (%#, %#, %#)%#%# · broken %# · placed %# · hand %# x%#", static_cast<int32>( position._x ), static_cast<int32>( position._y ),
                     static_cast<int32>( position._z ), _body.isOnGround() ? " on ground" : "", _body.isInWater() ? " swimming" : "", _brokenCount, _placedCount,
                     pBlock != nullptr ? pBlock->_name.c_str() : "empty", _hotbar.getSelectedSlot()._count );
    }
} // namespace sw
