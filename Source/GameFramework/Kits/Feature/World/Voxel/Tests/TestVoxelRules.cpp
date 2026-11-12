#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelBody.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelHotbar.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelRayHit.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelTerrain.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

using namespace sw;

namespace
{
    /** @brief 돌 · 물 · 유리 카탈로그와 2 x 2 청크 월드입니다. y = 10 의 돌 바닥을 깔 수 있습니다. */
    struct VoxelFixture
    {
        VoxelBlockCatalog _catalog;
        VoxelWorld        _world;
        VoxelBlockIndex   _stone{ kVoxelAirBlock };
        VoxelBlockIndex   _water{ kVoxelAirBlock };
        VoxelBlockIndex   _glass{ kVoxelAirBlock };

        VoxelFixture()
        {
            _stone = addBlock( "stone", true, true );
            _water = addBlock( "water", false, false );
            _glass = addBlock( "glass", true, false );
            SW_REQUIRE_TRUE( _world.initialize( 2, 2, &_catalog ) );
        }

        VoxelBlockIndex addBlock( const utf8* pID, bool bSolid, bool bOpaque )
        {
            VoxelBlockDef block;
            block._id      = hashed_string( pID );
            block._name    = pID;
            block._bSolid  = bSolid;
            block._bOpaque = bOpaque;
            return _catalog.addBlock( block );
        }

        void layFloor( int32 y )
        {
            for ( int32 z = 0; z < _world.getSizeZ(); ++z )
            {
                for ( int32 x = 0; x < _world.getSizeX(); ++x )
                {
                    SW_REQUIRE_TRUE( _world.setBlock( x, y, z, _stone ) );
                }
            }
        }

        static void clearDirty( VoxelWorld& world )
        {
            for ( int32 z = 0; z < world.getChunkCountZ(); ++z )
            {
                for ( int32 x = 0; x < world.getChunkCountX(); ++x )
                {
                    world.clearChunkDirty( x, z );
                }
            }
        }

        /** @brief 세이브에서 첫 구간이 시작하는 바이트 자리입니다. 머리 · 청크 수 · 팔레트를 같은 칸 함수로 써서 잰다. */
        static size_t measureRunsOffset( const VoxelWorld& world )
        {
            Archive               prefix;
            StateIO               io( prefix );
            int32                 chunkCountX = world.getChunkCountX();
            int32                 chunkCountZ = world.getChunkCountZ();
            vector<hashed_string> listPaletteID;
            for ( const VoxelBlockDef& block : world.getCatalog()->getBlocks() )
            {
                listPaletteID.push_back( block._id );
            }
            io.header( VoxelWorld::kStateTag, VoxelWorld::kStateVersion );
            io.value( chunkCountX );
            io.value( chunkCountZ );
            io.list( listPaletteID );
            return prefix.getSize();
        }

        static vector<uint8> save( const VoxelWorld& world )
        {
            Archive archive;
            world.writeState( archive );
            return vector<uint8>( archive.getData(), archive.getData() + archive.getSize() );
        }
    };

    /** @brief 몸을 @p seconds 동안 1/60 초씩 움직입니다. */
    struct VoxelBodyTestUtil
    {
        static void run( VoxelBody& body, const VoxelWorld& world, const float3& wish, bool bJump, float32 seconds )
        {
            constexpr float32 kFrame = 1.0f / 60.0f;
            for ( float32 elapsed = 0.0f; elapsed < seconds; elapsed += kFrame )
            {
                body.step( world, wish, bJump, false, kFrame );
            }
        }
    };
} // namespace

// ---- 카탈로그 ----

// 번호 0 은 공기다. 번호는 저장되므로 겹친 id 가 앞 블록의 번호를 바꾸면 안 된다.
SW_TEST_CASE( VoxelBlockCatalogTest, IndexStartsAfterAirAndDuplicateIsRefused )
{
    VoxelFixture rig;
    SW_EXPECT_EQUAL( 1, static_cast<int32>( rig._stone ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( rig._water ) );
    SW_EXPECT_TRUE( rig._catalog.findBlockIndex( hashed_string( "glass" ) ) == rig._glass );
    SW_EXPECT_TRUE( rig._catalog.findBlockIndex( hashed_string( "nothing" ) ) == kVoxelAirBlock );

    SW_EXPECT_LOG( "GameCatalog.DuplicateID" );
    SW_EXPECT_TRUE( rig.addBlock( "stone", false, false ) == kVoxelAirBlock );
    SW_EXPECT_TRUE( rig._catalog.isSolid( rig._stone ) );
    SW_EXPECT_FALSE( rig._catalog.isSolid( rig._water ) );
    SW_EXPECT_FALSE( rig._catalog.isOpaque( rig._glass ) );
}

// ---- 월드 ----

SW_TEST_CASE( VoxelWorldTest, OutsideReadsAirAndRefusesWrites )
{
    VoxelFixture rig;
    SW_EXPECT_EQUAL( 32, rig._world.getSizeX() );
    SW_EXPECT_TRUE( rig._world.getBlock( -1, 0, 0 ) == kVoxelAirBlock );
    SW_EXPECT_FALSE( rig._world.setBlock( 32, 0, 0, rig._stone ) );
    SW_EXPECT_FALSE( rig._world.setBlock( 0, kVoxelChunkHeight, 0, rig._stone ) );
    SW_EXPECT_TRUE( rig._world.isSolid( 5, -1, 5 ) );   // 월드 아래는 막혔다
    SW_EXPECT_FALSE( rig._world.isSolid( -1, 5, 5 ) );  // 옆은 비었다
    SW_EXPECT_FALSE( rig._world.isSolid( 5, 100, 5 ) ); // 위도 비었다
    SW_EXPECT_FALSE( rig._world.findTopSolidY( 5, 5 ).has_value() );

    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 3, 5, rig._stone ) );
    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 7, 5, rig._water ) ); // 물은 단단하지 않다
    SW_EXPECT_TRUE( rig._world.findTopSolidY( 5, 5 ) == 3 );
    SW_EXPECT_EQUAL( uint32( 1 ), rig._world.countBlocks( rig._water ) );
}

SW_TEST_CASE( VoxelWorldTest, BoundaryBlockAlsoDirtiesItsNeighbor )
{
    VoxelFixture rig;
    VoxelFixture::clearDirty( rig._world );
    const uint32 revision = rig._world.findChunk( 0, 0 )->_revision;

    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 0, 5, rig._stone ) );
    SW_EXPECT_TRUE( rig._world.isChunkDirty( 0, 0 ) );
    SW_EXPECT_FALSE( rig._world.isChunkDirty( 1, 0 ) );
    SW_EXPECT_EQUAL( revision + 1, rig._world.findChunk( 0, 0 )->_revision );

    VoxelFixture::clearDirty( rig._world );
    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 0, 5, rig._stone ) ); // 같은 값은 표시하지 않는다
    SW_EXPECT_FALSE( rig._world.isChunkDirty( 0, 0 ) );

    SW_REQUIRE_TRUE( rig._world.setBlock( kVoxelChunkSize - 1, 0, 3, rig._stone ) );
    SW_EXPECT_TRUE( rig._world.isChunkDirty( 1, 0 ) );
    SW_EXPECT_FALSE( rig._world.isChunkDirty( 0, 1 ) );

    VoxelFixture::clearDirty( rig._world );
    SW_REQUIRE_TRUE( rig._world.setBlock( kVoxelChunkSize, 0, kVoxelChunkSize, rig._stone ) ); // 청크 (1, 1) 의 모서리
    SW_EXPECT_TRUE( rig._world.isChunkDirty( 0, 1 ) );
    SW_EXPECT_TRUE( rig._world.isChunkDirty( 1, 0 ) );
    SW_EXPECT_FALSE( rig._world.isChunkDirty( 0, 0 ) );
}

SW_TEST_CASE( VoxelWorldTest, RefusesEmptyWorld )
{
    SW_EXPECT_LOG( "VoxelWorld.VoxelWorldRefusedChunkCounts" );
    VoxelWorld world;
    SW_EXPECT_FALSE( world.initialize( 0, 1, nullptr ) );
}

SW_TEST_CASE( VoxelWorldTest, StateRoundTripsAndBumpsRevisions )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    SW_REQUIRE_TRUE( rig._world.setBlock( 20, 9, 20, rig._glass ) );
    const vector<uint8> listSaved = VoxelFixture::save( rig._world );

    VoxelFixture restored;
    VoxelFixture::clearDirty( restored._world );
    const uint32 revision = restored._world.findChunk( 1, 1 )->_revision;
    Archive      reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored._world.readState( reading ) );
    SW_EXPECT_TRUE( restored._world.getBlock( 20, 9, 20 ) == restored._glass );
    SW_EXPECT_EQUAL( uint32( 32 * 32 ), restored._world.countBlocks( restored._stone ) );
    SW_EXPECT_TRUE( restored._world.isChunkDirty( 1, 1 ) );
    SW_EXPECT_EQUAL( revision + 1, restored._world.findChunk( 1, 1 )->_revision ); // 이미 만든 메시가 낡은 것이 된다
    SW_EXPECT_TRUE( VoxelFixture::save( restored._world ) == listSaved );
}

SW_TEST_CASE( VoxelWorldTest, ForgedStateLeavesTheWorldAlone )
{
    VoxelFixture        rig;
    const vector<uint8> listSaved = VoxelFixture::save( rig._world );

    // 청크 수가 줄었다(감사 A-6): 밖의 청크가 공기면 같은 청크 좌표로 읽고, 블록이 남으면 버리지 않고 거절한다
    VoxelFixture target;
    VoxelWorld   other;
    SW_REQUIRE_TRUE( other.initialize( 1, 1, &target._catalog ) );
    Archive emptyOutside( listSaved.data(), listSaved.size() );
    SW_EXPECT_TRUE( other.readState( emptyOutside ) );
    VoxelFixture outside;
    SW_REQUIRE_TRUE( outside._world.setBlock( 20, 9, 20, outside._stone ) ); // 청크 (1, 1)
    const vector<uint8> listOutside = VoxelFixture::save( outside._world );
    Archive             wrongSize( listOutside.data(), listOutside.size() );
    SW_EXPECT_FALSE( other.readState( wrongSize ) );

    SW_REQUIRE_TRUE( target._world.setBlock( 1, 1, 1, target._stone ) );

    // 팔레트 밖 번호 — 머리 · 청크 수 · 팔레트 뒤 첫 구간의 번호(uint16 낮은 바이트)다
    const size_t  firstBlockOffset = VoxelFixture::measureRunsOffset( rig._world );
    vector<uint8> listBadBlock     = listSaved;
    listBadBlock[firstBlockOffset] = 200;
    Archive badBlock( listBadBlock.data(), listBadBlock.size() );
    SW_EXPECT_FALSE( target._world.readState( badBlock ) );

    // 옛 판(1) — 머리의 판 칸(태그 uint32 뒤)을 1 로 되돌린다. 판 1 은 머리표가 없던 판이라 올리는 단계가 없다
    SW_EXPECT_LOG( "FormatUpgrade.StepMissing" );
    vector<uint8> listOldVersion     = listSaved;
    listOldVersion[sizeof( uint32 )] = 1;
    Archive oldVersion( listOldVersion.data(), listOldVersion.size() );
    SW_EXPECT_FALSE( target._world.readState( oldVersion ) );

    // 잘린 바이트
    Archive truncated( listSaved.data(), listSaved.size() - 1 );
    SW_EXPECT_FALSE( target._world.readState( truncated ) );

    // 청크를 넘는 구간 — 첫 구간 길이를 부풀린다
    vector<uint8> listLongRun                                               = listSaved;
    listLongRun[firstBlockOffset + sizeof( uint16 ) + sizeof( uint32 ) - 1] = 0x7f;
    Archive longRun( listLongRun.data(), listLongRun.size() );
    SW_EXPECT_FALSE( target._world.readState( longRun ) );

    SW_EXPECT_TRUE( target._world.getBlock( 1, 1, 1 ) == target._stone ); // 거절은 상태를 바꾸지 않는다
}

// 세이브는 블록을 id 팔레트로 적는다 — blocks.xml 중간에 블록이 끼어 순서가 바뀌어도 옛 세이브는 같은 블록으로 읽혀야 한다.
SW_TEST_CASE( VoxelWorldTest, SaveSurvivesCatalogReorder )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    SW_REQUIRE_TRUE( rig._world.setBlock( 20, 9, 20, rig._glass ) );
    SW_REQUIRE_TRUE( rig._world.setBlock( 3, 4, 5, rig._water ) );
    const vector<uint8> listSaved = VoxelFixture::save( rig._world );

    // 새 블록을 맨 앞에 넣고 나머지 순서도 뒤집은 카탈로그다
    VoxelBlockCatalog reordered;
    VoxelWorld        world;
    const auto        addBlock = [&reordered]( const utf8* pID )
    {
        VoxelBlockDef block;
        block._id   = hashed_string( pID );
        block._name = pID;
        return reordered.addBlock( block );
    };
    const VoxelBlockIndex sand  = addBlock( "sand" );
    const VoxelBlockIndex glass = addBlock( "glass" );
    const VoxelBlockIndex water = addBlock( "water" );
    const VoxelBlockIndex stone = addBlock( "stone" );
    SW_REQUIRE_TRUE( stone != rig._stone && glass != rig._glass );
    SW_REQUIRE_TRUE( world.initialize( 2, 2, &reordered ) );

    Archive reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( world.readState( reading ) );
    SW_EXPECT_TRUE( world.getBlock( 20, 9, 20 ) == glass );
    SW_EXPECT_TRUE( world.getBlock( 3, 4, 5 ) == water );
    SW_EXPECT_EQUAL( uint32( 32 * 32 ), world.countBlocks( stone ) );
    SW_EXPECT_EQUAL( uint32( 0 ), world.countBlocks( sand ) );
}

// 지금 카탈로그에 없는 블록 id 는 공기로 바꾸지 않고 세이브를 거절한다.
SW_TEST_CASE( VoxelWorldTest, SaveWithUnknownBlockIsRefused )
{
    VoxelFixture        rig;
    const vector<uint8> listSaved = VoxelFixture::save( rig._world );

    VoxelBlockCatalog onlyStone;
    VoxelBlockDef     block;
    block._id   = hashed_string( "stone" );
    block._name = "stone";
    SW_REQUIRE_TRUE( onlyStone.addBlock( block ) != kVoxelAirBlock );
    VoxelWorld world;
    SW_REQUIRE_TRUE( world.initialize( 2, 2, &onlyStone ) );
    Archive reading( listSaved.data(), listSaved.size() );
    SW_EXPECT_FALSE( world.readState( reading ) );
}

// ---- 광선 ----

SW_TEST_CASE( VoxelRaycastTest, HitsTheFirstSolidWithFaceAndPlaceCell )
{
    VoxelFixture rig;
    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 2, 2, rig._stone ) );
    VoxelRayHit hit;
    SW_REQUIRE_TRUE( VoxelRayHit::raycast( rig._world, float3{ 0.5f, 2.5f, 2.5f }, float3{ 1.0f, 0.0f, 0.0f }, 10.0f, hit ) );
    SW_EXPECT_TRUE( hit._block == ( VoxelCoord{ 5, 2, 2 } ) );
    SW_EXPECT_TRUE( hit._previous == ( VoxelCoord{ 4, 2, 2 } ) ); // 블록을 놓을 자리다
    SW_EXPECT_TRUE( hit._normal == ( VoxelCoord{ -1, 0, 0 } ) );
    SW_EXPECT_NEAR( 4.5f, hit._distance, 1.0e-4f );
    SW_EXPECT_TRUE( hit._blockIndex == rig._stone );
}

SW_TEST_CASE( VoxelRaycastTest, RangeAndBadInputsMiss )
{
    VoxelFixture rig;
    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 2, 2, rig._stone ) );
    VoxelRayHit  hit;
    const float3 origin{ 0.5f, 2.5f, 2.5f };
    SW_EXPECT_FALSE( VoxelRayHit::raycast( rig._world, origin, float3{ 1.0f, 0.0f, 0.0f }, 4.0f, hit ) ); // 닿기 전에 끝난다
    SW_EXPECT_FALSE( VoxelRayHit::raycast( rig._world, origin, float3{ -1.0f, 0.0f, 0.0f }, 10.0f, hit ) );
    SW_EXPECT_FALSE( VoxelRayHit::raycast( rig._world, origin, float3{ 0.0f, 0.0f, 0.0f }, 10.0f, hit ) );
    SW_EXPECT_FALSE( VoxelRayHit::raycast( rig._world, origin, float3{ 1.0f, 0.0f, 0.0f }, 0.0f, hit ) );
    SW_EXPECT_FALSE( VoxelRayHit::raycast( rig._world, float3{ 0.5f, 90.0f, 0.5f }, float3{ 0.0f, 1.0f, 0.0f }, 100.0f, hit ) ); // 위로 빠져나간다
}

SW_TEST_CASE( VoxelRaycastTest, WaterStopsOnlyWhenAsked )
{
    VoxelFixture rig;
    SW_REQUIRE_TRUE( rig._world.setBlock( 2, 2, 2, rig._water ) );
    SW_REQUIRE_TRUE( rig._world.setBlock( 4, 2, 2, rig._stone ) );
    const float3 origin{ 0.5f, 2.5f, 2.5f };
    VoxelRayHit  hit;
    SW_REQUIRE_TRUE( VoxelRayHit::raycast( rig._world, origin, float3{ 1.0f, 0.0f, 0.0f }, 10.0f, hit ) );
    SW_EXPECT_EQUAL( 4, hit._block._x );
    SW_REQUIRE_TRUE( VoxelRayHit::raycast( rig._world, origin, float3{ 1.0f, 0.0f, 0.0f }, 10.0f, hit, true ) );
    SW_EXPECT_EQUAL( 2, hit._block._x );
}

SW_TEST_CASE( VoxelRaycastTest, StartingInsideABlockHasNoFace )
{
    VoxelFixture rig;
    SW_REQUIRE_TRUE( rig._world.setBlock( 1, 1, 1, rig._stone ) );
    VoxelRayHit hit;
    SW_REQUIRE_TRUE( VoxelRayHit::raycast( rig._world, float3{ 1.5f, 1.5f, 1.5f }, float3{ 0.0f, 1.0f, 0.0f }, 5.0f, hit ) );
    SW_EXPECT_TRUE( hit._normal == ( VoxelCoord{ 0, 0, 0 } ) );
    SW_EXPECT_NEAR( 0.0f, hit._distance, 1.0e-6f );
}

// ---- 몸 ----

SW_TEST_CASE( VoxelBodyTest, FallsOntoTheFloorAndStands )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    VoxelBody body;
    body.setPosition( float3{ 5.5f, 8.0f, 5.5f } );
    VoxelBodyTestUtil::run( body, rig._world, float3{}, false, 2.0f );
    SW_EXPECT_TRUE( body.isOnGround() );
    SW_EXPECT_NEAR( 3.0f, body.getPosition()._y, 0.01f );
    SW_EXPECT_NEAR( 0.0f, body.getVelocity()._y, 0.5f );
    SW_EXPECT_FALSE( body.isBlockedAt( rig._world, body.getPosition() ) );
}

// 빠르게 떨어져도 한 걸음이 짧아 얇은 바닥을 뚫지 않는다.
SW_TEST_CASE( VoxelBodyTest, LongFrameDoesNotTunnelThroughTheFloor )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    VoxelBody body;
    body.setPosition( float3{ 5.5f, 60.0f, 5.5f } );
    for ( int32 frame = 0; frame < 40; ++frame )
    {
        body.step( rig._world, float3{}, false, false, 0.25f );
    }
    SW_EXPECT_NEAR( 3.0f, body.getPosition()._y, 0.01f );
    SW_EXPECT_TRUE( body.isOnGround() );
}

SW_TEST_CASE( VoxelBodyTest, JumpNeedsTheGroundAndClearsOneBlock )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    VoxelBody body;
    body.setPosition( float3{ 5.5f, 3.5f, 5.5f } ); // 공중이다
    body.step( rig._world, float3{}, true, false, 1.0f / 60.0f );
    SW_EXPECT_TRUE( body.getVelocity()._y <= 0.0f ); // 공중에서는 뛰지 못한다

    VoxelBodyTestUtil::run( body, rig._world, float3{}, false, 1.0f );
    SW_REQUIRE_TRUE( body.isOnGround() );
    float32 peak = 0.0f;
    body.step( rig._world, float3{}, true, false, 1.0f / 60.0f );
    for ( int32 frame = 0; frame < 60; ++frame )
    {
        body.step( rig._world, float3{}, false, false, 1.0f / 60.0f );
        peak = MathUtil::max( peak, body.getPosition()._y );
    }
    SW_EXPECT_TRUE( peak > 4.0f ); // 블록 하나를 넘는다
}

SW_TEST_CASE( VoxelBodyTest, WallStopsOneAxisAndSlidesOnTheOther )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    for ( int32 z = 0; z < 32; ++z )
    {
        SW_REQUIRE_TRUE( rig._world.setBlock( 8, 3, z, rig._stone ) );
        SW_REQUIRE_TRUE( rig._world.setBlock( 8, 4, z, rig._stone ) );
    }
    VoxelBody body;
    body.setPosition( float3{ 6.5f, 3.0f, 5.5f } );
    VoxelBodyTestUtil::run( body, rig._world, float3{ 0.7f, 0.0f, 0.7f }, false, 1.5f );
    SW_EXPECT_TRUE( body.getPosition()._x < 8.0f - body.getSettings()._halfWidth + 0.01f );
    SW_EXPECT_TRUE( body.getPosition()._x > 7.5f );
    SW_EXPECT_NEAR( 0.0f, body.getVelocity()._x, 1.0e-3f );
    SW_EXPECT_TRUE( body.getPosition()._z > 7.0f ); // 벽을 따라 미끄러졌다
}

SW_TEST_CASE( VoxelBodyTest, WaterSlowsAndJumpSwimsUp )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    for ( int32 y = 3; y < 20; ++y )
    {
        for ( int32 z = 0; z < 16; ++z )
        {
            for ( int32 x = 0; x < 16; ++x )
            {
                SW_REQUIRE_TRUE( rig._world.setBlock( x, y, z, rig._water ) );
            }
        }
    }
    VoxelBody body;
    body.setWaterBlock( rig._water );
    body.setPosition( float3{ 5.5f, 10.0f, 5.5f } );
    VoxelBodyTestUtil::run( body, rig._world, float3{}, false, 0.2f );
    SW_REQUIRE_TRUE( body.isInWater() );
    SW_EXPECT_TRUE( body.getVelocity()._y >= -body.getSettings()._swimSpeed );

    const float32 startY = body.getPosition()._y;
    VoxelBodyTestUtil::run( body, rig._world, float3{}, true, 1.0f );
    SW_EXPECT_TRUE( body.getPosition()._y > startY );

    VoxelBody dry; // 물 블록을 알려 주지 않은 몸은 헤엄치지 않는다
    dry.setPosition( float3{ 5.5f, 10.0f, 5.5f } );
    VoxelBodyTestUtil::run( dry, rig._world, float3{}, false, 0.2f );
    SW_EXPECT_FALSE( dry.isInWater() );
}

SW_TEST_CASE( VoxelBodyTest, OverlapIgnoresTheTouchingFace )
{
    VoxelBody body;
    body.setPosition( float3{ 0.5f, 1.0f, 0.5f } );
    SW_EXPECT_TRUE( body.overlapsBlock( VoxelCoord{ 0, 1, 0 } ) );
    SW_EXPECT_TRUE( body.overlapsBlock( VoxelCoord{ 0, 2, 0 } ) );
    SW_EXPECT_FALSE( body.overlapsBlock( VoxelCoord{ 0, 0, 0 } ) ); // 발밑 블록에 닿기만 한다
    SW_EXPECT_FALSE( body.overlapsBlock( VoxelCoord{ 1, 1, 0 } ) );
}

// ---- 핫바 ----

SW_TEST_CASE( VoxelHotbarTest, AddFillsSameBlockThenEmptySlotsAndReturnsTheRest )
{
    VoxelHotbar hotbar{ nullptr };
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 1, 60 ) );
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 2, 1 ) );
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 1, 10 ) ); // 첫 칸을 64 로 채우고 나머지 6 은 빈 칸으로
    SW_EXPECT_EQUAL( 64, hotbar.getSlot( 0 )._count );
    SW_EXPECT_EQUAL( 6, hotbar.getSlot( 2 )._count );
    SW_EXPECT_EQUAL( 70, hotbar.countBlock( 1 ) );

    SW_EXPECT_EQUAL( 0, hotbar.addBlock( kVoxelAirBlock, 5 ) );
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 3, -5 ) );
    SW_EXPECT_EQUAL( 0, hotbar.countBlock( 3 ) );

    VoxelHotbar full{ nullptr };
    const int32 capacity = VoxelHotbar::kSlotCount * VoxelHotbar::kMaxStack;
    SW_EXPECT_EQUAL( 5, full.addBlock( 1, capacity + 5 ) );
    SW_EXPECT_EQUAL( capacity, full.countBlock( 1 ) );
}

SW_TEST_CASE( VoxelHotbarTest, ConsumeEmptiesTheSlotAndSelectWraps )
{
    VoxelHotbar     hotbar{ nullptr };
    VoxelBlockIndex block = kVoxelAirBlock;
    SW_EXPECT_FALSE( hotbar.consumeSelected( block ) );
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( 7, 1 ) );
    SW_REQUIRE_TRUE( hotbar.consumeSelected( block ) );
    SW_EXPECT_EQUAL( 7, static_cast<int32>( block ) );
    SW_EXPECT_TRUE( hotbar.getSelectedSlot().isEmpty() );
    SW_EXPECT_FALSE( hotbar.consumeSelected( block ) );

    hotbar.selectRelative( -1 );
    SW_EXPECT_EQUAL( VoxelHotbar::kSlotCount - 1, hotbar.getSelectedIndex() );
    hotbar.selectRelative( 1 );
    SW_EXPECT_EQUAL( 0, hotbar.getSelectedIndex() );
    hotbar.selectRelative( VoxelHotbar::kSlotCount * 3 + 2 );
    SW_EXPECT_EQUAL( 2, hotbar.getSelectedIndex() );
    hotbar.select( 99 );
    SW_EXPECT_EQUAL( VoxelHotbar::kSlotCount - 1, hotbar.getSelectedIndex() );
    SW_EXPECT_TRUE( hotbar.getSlot( -5 ).isEmpty() ); // 범위 밖 번호도 끝 칸으로 읽는다
}

SW_TEST_CASE( VoxelHotbarTest, StateRoundTripsAndRefusesBadCounts )
{
    VoxelFixture rig;
    VoxelHotbar  hotbar{ &rig._catalog };
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( rig._glass, 12 ) );
    hotbar.select( 3 );
    Archive writing;
    hotbar.writeState( writing );
    const vector<uint8> listSaved( writing.getData(), writing.getData() + writing.getSize() );

    VoxelHotbar restored{ &rig._catalog };
    Archive     reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 12, restored.countBlock( rig._glass ) );
    SW_EXPECT_EQUAL( 3, restored.getSelectedIndex() );

    // 첫 슬롯은 머리 뒤 블록 id 글 + 개수 int32 다. 같은 칸 함수로 자리를 재고 첫 개수의 높은 바이트를 부풀린다.
    Archive       prefix;
    StateIO       prefixIO( prefix );
    hashed_string glassID = hashed_string( "glass" );
    prefixIO.header( VoxelHotbar::kStateTag, VoxelHotbar::kStateVersion );
    prefixIO.value( glassID );
    vector<uint8> listBadCount                           = listSaved;
    listBadCount[prefix.getSize() + sizeof( int32 ) - 1] = 0x7f;
    VoxelHotbar target{ &rig._catalog };
    Archive     badCount( listBadCount.data(), listBadCount.size() );
    SW_EXPECT_FALSE( target.readState( badCount ) );
    SW_EXPECT_EQUAL( 0, target.countBlock( rig._glass ) );

    vector<uint8> listBadSelect             = listSaved;
    listBadSelect[listBadSelect.size() - 1] = 0x7f; // 고른 칸의 높은 바이트
    Archive badSelect( listBadSelect.data(), listBadSelect.size() );
    SW_EXPECT_FALSE( target.readState( badSelect ) );

    SW_EXPECT_LOG( "FormatUpgrade.StepMissing" ); // 판 1 은 머리표가 없던 판이라 올리는 단계가 없다
    vector<uint8> listOldVersion     = listSaved;
    listOldVersion[sizeof( uint32 )] = 1; // 머리의 판 칸(태그 uint32 뒤)
    Archive oldVersion( listOldVersion.data(), listOldVersion.size() );
    SW_EXPECT_FALSE( target.readState( oldVersion ) );
}

// 핫바도 블록을 id 로 적는다 — 카탈로그 순서가 바뀌어도 같은 블록이고, 없는 블록은 거절한다.
SW_TEST_CASE( VoxelHotbarTest, SaveSurvivesCatalogReorder )
{
    VoxelFixture rig;
    VoxelHotbar  hotbar{ &rig._catalog };
    SW_EXPECT_EQUAL( 0, hotbar.addBlock( rig._glass, 5 ) );
    Archive writing;
    hotbar.writeState( writing );
    const vector<uint8> listSaved( writing.getData(), writing.getData() + writing.getSize() );

    VoxelBlockCatalog reordered;
    for ( const utf8* pID : { "sand", "glass" } )
    {
        VoxelBlockDef block;
        block._id   = hashed_string( pID );
        block._name = pID;
        SW_REQUIRE_TRUE( reordered.addBlock( block ) != kVoxelAirBlock );
    }
    const VoxelBlockIndex glass = reordered.findBlockIndex( hashed_string( "glass" ) );
    SW_REQUIRE_TRUE( glass != rig._glass );
    VoxelHotbar restored{ &reordered };
    Archive     reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 5, restored.countBlock( glass ) );

    VoxelBlockCatalog onlySand;
    VoxelBlockDef     sand;
    sand._id   = hashed_string( "sand" );
    sand._name = "sand";
    SW_REQUIRE_TRUE( onlySand.addBlock( sand ) != kVoxelAirBlock );
    VoxelHotbar missing{ &onlySand };
    Archive     readingMissing( listSaved.data(), listSaved.size() );
    SW_EXPECT_FALSE( missing.readState( readingMissing ) );
}

// ---- 지형 ----

SW_TEST_CASE( VoxelTerrainTest, SameSeedSameWorldAndColumnsAreLayered )
{
    VoxelFixture          rig;
    const VoxelBlockIndex grass   = rig.addBlock( "grass", true, true );
    const VoxelBlockIndex dirt    = rig.addBlock( "dirt", true, true );
    const VoxelBlockIndex bedrock = rig.addBlock( "bedrock", true, true );
    (void)rig.addBlock( "sand", true, true );

    VoxelTerrainSettings settings;
    settings._treeChance            = 0.0f;
    const VoxelTerrainReport report = VoxelTerrainSettings::fillWorld( rig._world, settings );
    SW_EXPECT_TRUE( report._minHeight <= report._maxHeight );
    SW_EXPECT_EQUAL( uint32( 0 ), report._treeCount );
    SW_EXPECT_TRUE( rig._world.getBlock( 3, 0, 3 ) == bedrock );
    SW_EXPECT_TRUE( rig._world.countBlocks( grass ) > 0 );
    SW_EXPECT_TRUE( rig._world.countBlocks( dirt ) > 0 );

    const int32 surface = VoxelTerrainSettings::computeSurfaceHeight( 3, 3, settings );
    SW_EXPECT_TRUE( report._minHeight <= surface && surface <= report._maxHeight );
    SW_EXPECT_TRUE( rig._world.findTopSolidY( 3, 3 ) == surface ); // 나무가 없으면 맨 위 단단한 블록이 지면이다

    VoxelFixture again;
    (void)again.addBlock( "grass", true, true );
    (void)again.addBlock( "dirt", true, true );
    (void)again.addBlock( "bedrock", true, true );
    (void)again.addBlock( "sand", true, true );
    (void)VoxelTerrainSettings::fillWorld( again._world, settings );
    SW_EXPECT_TRUE( VoxelFixture::save( again._world ) == VoxelFixture::save( rig._world ) );
}
