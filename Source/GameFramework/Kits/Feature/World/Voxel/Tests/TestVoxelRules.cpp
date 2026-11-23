#include "pch.h"

#include "Core/Math/MathUtil.h"
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
    /** @brief 돌 · 물 · 유리 카탈로그와 2 x 2 청크 월드입니다(공기 생성 훅으로 모든 청크가 올라와 있다). 돌 바닥을 깔 수 있습니다. */
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
            loadAll( _world );
        }

        /** @brief 공기 생성 훅을 걸고(저장본이 없는 청크는 공기) 모든 청크를 올립니다. */
        static void loadAll( VoxelWorld& world )
        {
            world.setChunkGenerator( []( const VoxelWorld&, int32, int32, vector<VoxelBlockIndex>& ) {} );
            const int32 chunkCount = world.getChunkCountX() * world.getChunkCountZ();
            SW_REQUIRE_TRUE( world.updateResidency( 0, 0, MathUtil::max( world.getChunkCountX(), world.getChunkCountZ() ), chunkCount ) == static_cast<uint32>( chunkCount ) );
        }

        /** @brief 블록을 넣습니다. 넣지 못하면 멈춘다(픽스처 준비는 성공해야 한다). */
        VoxelBlockIndex addBlock( const utf8* pID, bool bSolid, bool bOpaque ) { return tryAddBlock( pID, bSolid, bOpaque ).value(); }

        optional<VoxelBlockIndex> tryAddBlock( const utf8* pID, bool bSolid, bool bOpaque )
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

        /** @brief 2 x 2 청크의 판 번호입니다 — 바뀜을 앞뒤로 견준다. */
        struct ChunkRevisionSnapshot
        {
            uint32 _arrRevision[2][2]{};

            explicit ChunkRevisionSnapshot( const VoxelWorld& world )
            {
                for ( int32 z = 0; z < 2; ++z )
                {
                    for ( int32 x = 0; x < 2; ++x )
                    {
                        _arrRevision[z][x] = world.findChunk( x, z )->_revision;
                    }
                }
            }

            /** @brief 찍은 뒤 청크 ( @p x, @p z ) 가 새 판이 됐으면 true 입니다. */
            bool hasChanged( const VoxelWorld& world, int32 x, int32 z ) const { return world.findChunk( x, z )->_revision > _arrRevision[z][x]; }
        };

        /** @brief 세이브에서 첫 청크의 첫 구간이 시작하는 바이트 자리입니다. 머리 · 청크 수 · 팔레트 · 실린 청크 수 · 첫 청크 좌표를 같은 칸 함수로 써서 잰다. */
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
            uint32 savedChunkCount = 1;
            int32  firstChunkX     = 0;
            int32  firstChunkZ     = 0;
            io.value( savedChunkCount );
            io.value( firstChunkX );
            io.value( firstChunkZ );
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

// 모르는 id 는 공기(0)가 아니라 비어 있다 — 공기를 가짜 정상 값으로 쓰지 못하게.
SW_TEST_CASE( VoxelBlockCatalogTest, UnknownIDFindsNothing )
{
    VoxelFixture rig;
    SW_EXPECT_FALSE( rig._catalog.findBlockIndex( hashed_string( "nothing" ) ).has_value() );
    SW_EXPECT_FALSE( rig._catalog.findBlockIndex( hashed_string{} ).has_value() );
}

// 번호 0 은 공기다. 번호는 저장되므로 겹친 id 가 앞 블록의 번호를 바꾸면 안 된다.
SW_TEST_CASE( VoxelBlockCatalogTest, IndexStartsAfterAirAndDuplicateIsRefused )
{
    VoxelFixture rig;
    SW_EXPECT_EQUAL( 1, static_cast<int32>( rig._stone ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( rig._water ) );
    SW_EXPECT_TRUE( rig._catalog.findBlockIndex( hashed_string( "glass" ) ).value() == rig._glass );

    SW_EXPECT_LOG( "GameCatalog.DuplicateID" );
    SW_EXPECT_FALSE( rig.tryAddBlock( "stone", false, false ).has_value() ); // 겹친 id 는 공기 0 이 아니라 비어 있다
    SW_EXPECT_TRUE( rig._catalog.isSolid( rig._stone ) );
    SW_EXPECT_FALSE( rig._catalog.isSolid( rig._water ) );
    SW_EXPECT_FALSE( rig._catalog.isOpaque( rig._glass ) );
}

/** @brief [VoxelBlockCatalogTest] 얻는 블록(`drop`)은 같은 카탈로그의 블록이어야 한다 — 참조 표가 읽기를 막는다(쿠킹도 같은 표로 실패한다) */
SW_TEST_CASE( VoxelBlockCatalogTest, UnknownDropFailsTheLoad )
{
    VoxelBlockCatalog known;
    SW_EXPECT_TRUE( known.loadFromXMLText( R"(<VoxelBlockCatalog><Block id="grass" name="Grass" tile="0" drop="dirt"/><Block id="dirt" name="Dirt" tile="2"/></VoxelBlockCatalog>)", "known" ) );
    VoxelBlockCatalog unknown;
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    SW_EXPECT_FALSE( unknown.loadFromXMLText( R"(<VoxelBlockCatalog><Block id="grass" name="Grass" tile="0" drop="dirt"/></VoxelBlockCatalog>)", "unknown" ) );
}

// 블록 머티리얼의 아틀라스 칸(_atlasGrid)은 카탈로그를 따라야 한다 — 메셔는 카탈로그 열 수로 타일 칸 UV 를 싣고 셰이더는 머티리얼 값으로 푼다.
// 검사는 월드당 한 번: 머티리얼이 준비되기 전에는 그리지 않고, 다르면 오류 한 줄 뒤 계속 그리지 않는다(맞춰 쓰지 않는다).
SW_TEST_CASE( VoxelBlockCatalogTest, MaterialAtlasGridMustMatchTheCatalog )
{
    VoxelFixture rig;
    const float4 catalogGrid{ static_cast<float32>( rig._catalog.getAtlasColumns() ), static_cast<float32>( rig._catalog.getAtlasRows() ),
                              static_cast<float32>( rig._catalog.getTileTexels() ), 0.0f };
    const float4 otherGrid{ catalogGrid._x + 1.0f, catalogGrid._y, catalogGrid._z, 0.0f };

    VoxelAtlasGridGate same;
    SW_EXPECT_FALSE( same.update( rig._catalog, false, nullptr, nullptr, "same.material" ) ); // 준비 전 — 견주지 않고 그리지 않는다
    SW_EXPECT_TRUE( same.isPending() );
    SW_EXPECT_TRUE( same.update( rig._catalog, true, &catalogGrid, &catalogGrid, "same.material" ) );

    SW_EXPECT_LOG( "VoxelBlockCatalog.AtlasGridMismatch" ); // 한 줄 — 아래에서 거듭 불러도 다시 견주지 않는다
    VoxelAtlasGridGate other;
    SW_EXPECT_FALSE( other.update( rig._catalog, true, &otherGrid, &otherGrid, "other.material" ) );
    SW_EXPECT_FALSE( other.update( rig._catalog, true, &catalogGrid, &catalogGrid, "other.material" ) );
    SW_EXPECT_FALSE( other.isPending() );

    VoxelAtlasGridGate missing; // 머티리얼에 칸이 없으면 다른 것이다
    SW_EXPECT_FALSE( missing.update( rig._catalog, true, &catalogGrid, nullptr, "missing.material" ) );
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

SW_TEST_CASE( VoxelWorldTest, BoundaryBlockAlsoAdvancesItsNeighborRevision )
{
    VoxelFixture                              rig;
    const VoxelFixture::ChunkRevisionSnapshot loaded( rig._world );
    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 0, 5, rig._stone ) );
    SW_EXPECT_TRUE( loaded.hasChanged( rig._world, 0, 0 ) );
    SW_EXPECT_FALSE( loaded.hasChanged( rig._world, 1, 0 ) );

    const VoxelFixture::ChunkRevisionSnapshot inner( rig._world );
    SW_REQUIRE_TRUE( rig._world.setBlock( 5, 0, 5, rig._stone ) ); // 같은 값은 판을 올리지 않는다
    SW_EXPECT_FALSE( inner.hasChanged( rig._world, 0, 0 ) );

    SW_REQUIRE_TRUE( rig._world.setBlock( kVoxelChunkSize - 1, 0, 3, rig._stone ) );
    SW_EXPECT_TRUE( inner.hasChanged( rig._world, 1, 0 ) );
    SW_EXPECT_FALSE( inner.hasChanged( rig._world, 0, 1 ) );

    const VoxelFixture::ChunkRevisionSnapshot edge( rig._world );
    SW_REQUIRE_TRUE( rig._world.setBlock( kVoxelChunkSize, 0, kVoxelChunkSize, rig._stone ) ); // 청크 (1, 1) 의 모서리
    SW_EXPECT_TRUE( edge.hasChanged( rig._world, 0, 1 ) );
    SW_EXPECT_TRUE( edge.hasChanged( rig._world, 1, 0 ) );
    SW_EXPECT_FALSE( edge.hasChanged( rig._world, 0, 0 ) );
}

// 내렸다 다시 올리거나 월드를 다시 연 청크는 앞 판의 번호를 되풀이하지 않는다 — 뷰가 옛 메시를 같은 판으로 보지 않게.
SW_TEST_CASE( VoxelWorldTest, RevisionIsNeverReused )
{
    VoxelFixture                              rig;
    const VoxelFixture::ChunkRevisionSnapshot loaded( rig._world );
    rig._world.unloadChunk( 0, 0 );
    SW_REQUIRE_TRUE( rig._world.loadChunk( 0, 0 ) );
    SW_EXPECT_TRUE( loaded.hasChanged( rig._world, 0, 0 ) );

    const VoxelFixture::ChunkRevisionSnapshot reloaded( rig._world );
    SW_REQUIRE_TRUE( rig._world.initialize( 2, 2, &rig._catalog ) );
    VoxelFixture::loadAll( rig._world );
    SW_EXPECT_TRUE( reloaded.hasChanged( rig._world, 1, 1 ) );
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

    VoxelFixture                              restored;
    const VoxelFixture::ChunkRevisionSnapshot before( restored._world );
    Archive                                   reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored._world.readState( reading ) );
    SW_EXPECT_TRUE( restored._world.getBlock( 20, 9, 20 ) == restored._glass );
    SW_EXPECT_EQUAL( uint32( 32 * 32 ), restored._world.countBlocks( restored._stone ) );
    SW_EXPECT_TRUE( before.hasChanged( restored._world, 1, 1 ) ); // 이미 만든 메시가 낡은 것이 된다
    SW_EXPECT_TRUE( VoxelFixture::save( restored._world ) == listSaved );
}

SW_TEST_CASE( VoxelWorldTest, CraftedStateLeavesTheWorldAlone )
{
    VoxelFixture rig;
    SW_REQUIRE_TRUE( rig._world.setBlock( 1, 1, 1, rig._stone ) ); // 청크 (0, 0) 만 바뀌었다 — 세이브에 청크 하나
    const vector<uint8> listSaved = VoxelFixture::save( rig._world );

    // 청크 수가 줄었다(감사 A-6): 실린 청크가 줄어든 월드 안이거나 밖이어도 공기면 읽고, 밖에 블록이 남으면 버리지 않고 거절한다
    VoxelFixture target;
    VoxelWorld   other;
    SW_REQUIRE_TRUE( other.initialize( 1, 1, &target._catalog ) );
    Archive inside( listSaved.data(), listSaved.size() );
    SW_EXPECT_TRUE( other.readState( inside ) );
    VoxelFixture outsideAir;
    SW_REQUIRE_TRUE( outsideAir._world.setBlock( 20, 9, 20, outsideAir._stone ) ); // 청크 (1, 1) 을 바꿨다가 되돌린다 — 실리지만 공기다
    SW_REQUIRE_TRUE( outsideAir._world.setBlock( 20, 9, 20, kVoxelAirBlock ) );
    const vector<uint8> listOutsideAir = VoxelFixture::save( outsideAir._world );
    Archive             emptyOutside( listOutsideAir.data(), listOutsideAir.size() );
    SW_EXPECT_TRUE( other.readState( emptyOutside ) );
    VoxelFixture outside;
    SW_REQUIRE_TRUE( outside._world.setBlock( 20, 9, 20, outside._stone ) ); // 청크 (1, 1)
    const vector<uint8> listOutside = VoxelFixture::save( outside._world );
    Archive             wrongSize( listOutside.data(), listOutside.size() );
    SW_EXPECT_FALSE( other.readState( wrongSize ) );

    SW_REQUIRE_TRUE( target._world.setBlock( 2, 2, 2, target._stone ) );

    // 팔레트 밖 번호 — 머리 · 청크 수 · 팔레트 · 실린 청크 수 · 첫 청크 좌표 뒤 첫 구간의 번호(uint16 낮은 바이트)다
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

    // 월드 밖 청크 좌표 — 첫 청크 x 를 저장된 청크 수(2)로 바꾼다
    vector<uint8> listBadChunk                                         = listSaved;
    listBadChunk[firstBlockOffset - sizeof( int32 ) - sizeof( int32 )] = 2;
    Archive badChunk( listBadChunk.data(), listBadChunk.size() );
    SW_EXPECT_FALSE( target._world.readState( badChunk ) );

    SW_EXPECT_TRUE( target._world.getBlock( 2, 2, 2 ) == target._stone ); // 거절은 상태를 바꾸지 않는다
}

// 청크 사전: 가운데에서 반경 안만 올라오고, 한 번에 올리는 수는 한도를 지키며 가까운 청크부터다. 올라오지 않은 청크는 공기로 읽히고 쓰기를 거절한다.
SW_TEST_CASE( VoxelWorldTest, ResidencyLoadsOnlyTheRadiusNearestFirst )
{
    VoxelFixture rig;
    VoxelWorld   world;
    SW_REQUIRE_TRUE( world.initialize( 8, 8, &rig._catalog ) );
    world.setChunkGenerator( [&rig]( const VoxelWorld&, int32, int32, vector<VoxelBlockIndex>& outBlocks )
    { outBlocks[0] = rig._stone; } ); // 청크마다 모서리 블록 하나
    SW_EXPECT_EQUAL( uint32( 3 ), world.updateResidency( 4, 4, 2, 3 ) );
    SW_EXPECT_EQUAL( uint32( 3 ), world.getResidentChunkCount() );
    SW_EXPECT_TRUE( world.isChunkResident( 4, 4 ) ); // 가운데가 먼저다
    SW_EXPECT_EQUAL( uint32( 25 - 3 ), world.updateResidency( 4, 4, 2, 100 ) );
    SW_EXPECT_EQUAL( uint32( 25 ), world.getResidentChunkCount() );
    SW_EXPECT_FALSE( world.isChunkResident( 7, 4 ) );
    SW_EXPECT_TRUE( world.getBlock( 4 * kVoxelChunkSize, 0, 4 * kVoxelChunkSize ) == rig._stone );
    SW_EXPECT_TRUE( world.getBlock( 7 * kVoxelChunkSize, 0, 4 * kVoxelChunkSize ) == kVoxelAirBlock );
    SW_EXPECT_FALSE( world.setBlock( 7 * kVoxelChunkSize, 5, 4 * kVoxelChunkSize, rig._stone ) );
    SW_EXPECT_EQUAL( uint32( 0 ), world.updateResidency( 4, 4, 2, 100 ) ); // 바뀐 것이 없다

    // 가운데가 옮겨 가면 밖의 청크는 내려간다(바뀌지 않았으니 저장본은 없다)
    (void)world.updateResidency( 0, 0, 1, 100 );
    SW_EXPECT_EQUAL( uint32( 4 ), world.getResidentChunkCount() ); // 월드 모서리라 2 x 2
    SW_EXPECT_EQUAL( uint32( 0 ), world.getStoredChunkCount() );
}

// 내릴 때는 바뀐 청크만 저장본에 남고, 다시 올리면 생성 훅 대신 저장본을 푼다. 세이브에도 바뀐 청크만 실린다.
SW_TEST_CASE( VoxelWorldTest, UnloadStoresOnlyChangedChunksAndReloadRestoresThem )
{
    VoxelFixture rig;
    VoxelWorld   world;
    int32        generatedCount = 0;
    SW_REQUIRE_TRUE( world.initialize( 4, 4, &rig._catalog ) );
    world.setChunkGenerator( [&rig, &generatedCount]( const VoxelWorld&, int32, int32, vector<VoxelBlockIndex>& outBlocks )
    {
        ++generatedCount;
        outBlocks[0] = rig._stone;
    } );
    (void)world.updateResidency( 1, 1, 1, 100 );
    SW_REQUIRE_EQUAL( 9, generatedCount );
    SW_REQUIRE_TRUE( world.setBlock( 20, 7, 20, rig._glass ) ); // 청크 (1, 1)

    (void)world.updateResidency( 3, 3, 0, 100 ); // 아홉 청크 모두 내린다
    SW_EXPECT_FALSE( world.isChunkResident( 1, 1 ) );
    SW_EXPECT_EQUAL( uint32( 1 ), world.getStoredChunkCount() );

    generatedCount = 0;
    (void)world.updateResidency( 1, 1, 1, 100 );
    SW_EXPECT_EQUAL( 8, generatedCount ); // (1, 1) 은 저장본에서 왔다
    SW_EXPECT_TRUE( world.getBlock( 20, 7, 20 ) == rig._glass );
    SW_EXPECT_TRUE( world.getBlock( kVoxelChunkSize, 0, kVoxelChunkSize ) == rig._stone );
    (void)world.updateResidency( 3, 3, 0, 100 ); // 바뀌지 않은 채 다시 내려도 저장본은 하나 그대로다
    SW_EXPECT_EQUAL( uint32( 1 ), world.getStoredChunkCount() );

    // 세이브는 저장본 하나만 싣는다 — 같은 생성 훅의 새 월드가 읽으면 바뀐 블록과 생성 블록이 함께 나온다
    const vector<uint8> listSaved = VoxelFixture::save( world );
    VoxelWorld          restored;
    SW_REQUIRE_TRUE( restored.initialize( 4, 4, &rig._catalog ) );
    restored.setChunkGenerator( []( const VoxelWorld&, int32, int32, vector<VoxelBlockIndex>& ) {} );
    Archive reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( uint32( 1 ), restored.getStoredChunkCount() );
    SW_REQUIRE_TRUE( restored.loadChunk( 1, 1 ) );
    SW_EXPECT_TRUE( restored.getBlock( 20, 7, 20 ) == rig._glass );
    SW_EXPECT_TRUE( VoxelFixture::save( restored ) == listSaved );
}

// 판 2 세이브(모든 청크를 순서대로)는 판 올림 단계가 판 3 으로 옮긴다 — 옛 세이브의 블록이 그대로 읽힌다.
SW_TEST_CASE( VoxelWorldTest, Version2SaveIsUpgraded )
{
    VoxelFixture rig;
    Archive      writing;
    {
        StateIO               io( writing );
        int32                 chunkCountX = 2;
        int32                 chunkCountZ = 2;
        vector<hashed_string> listPaletteID{ hashed_string( "stone" ), hashed_string( "water" ), hashed_string( "glass" ) };
        io.header( VoxelWorld::kStateTag, 2 );
        io.value( chunkCountX );
        io.value( chunkCountZ );
        io.list( listPaletteID );
        for ( int32 chunkIndex = 0; chunkIndex < 4; ++chunkIndex )
        {
            uint16 paletteNumber = chunkIndex == 3 ? 1 : 0; // 마지막 청크 (1, 1) 만 돌로 찼다
            uint32 length        = static_cast<uint32>( kVoxelChunkVolume );
            uint16 closeNumber   = 0;
            uint32 closeLength   = 0;
            io.value( paletteNumber );
            io.value( length );
            io.value( closeNumber );
            io.value( closeLength );
        }
    }
    Archive reading( writing.getData(), writing.getSize() );
    SW_REQUIRE_TRUE( rig._world.readState( reading ) );
    SW_EXPECT_EQUAL( uint32( 4 ), rig._world.getStoredChunkCount() ); // 판 2 는 바뀐 청크를 가리지 않았다 — 모두 저장본이다
    SW_EXPECT_TRUE( rig._world.getBlock( 20, 9, 20 ) == rig._stone );
    SW_EXPECT_TRUE( rig._world.getBlock( 3, 9, 3 ) == kVoxelAirBlock );
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
        return reordered.addBlock( block ).value();
    };
    const VoxelBlockIndex sand  = addBlock( "sand" );
    const VoxelBlockIndex glass = addBlock( "glass" );
    const VoxelBlockIndex water = addBlock( "water" );
    const VoxelBlockIndex stone = addBlock( "stone" );
    SW_REQUIRE_TRUE( stone != rig._stone && glass != rig._glass );
    SW_REQUIRE_TRUE( world.initialize( 2, 2, &reordered ) );

    Archive reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( world.readState( reading ) );
    VoxelFixture::loadAll( world ); // 바닥을 깐 네 청크 모두 저장본에서 올라온다
    SW_EXPECT_TRUE( world.getBlock( 20, 9, 20 ) == glass );
    SW_EXPECT_TRUE( world.getBlock( 3, 4, 5 ) == water );
    SW_EXPECT_EQUAL( uint32( 32 * 32 ), world.countBlocks( stone ) );
    SW_EXPECT_EQUAL( uint32( 0 ), world.countBlocks( sand ) );
}

// 도는 월드의 카탈로그가 다시 읽히면 디렉터가 상태를 왕복시킨다 — 새 카탈로그에 묶고 읽으면 올라와 있던 청크의 블록 번호도 id 로 다시 짝지어진다.
SW_TEST_CASE( VoxelWorldTest, RebindingTheCatalogKeepsTheBlocksByID )
{
    VoxelFixture rig;
    rig.layFloor( 2 );
    SW_REQUIRE_TRUE( rig._world.setBlock( 20, 9, 20, rig._glass ) );
    const vector<uint8> listSaved = VoxelFixture::save( rig._world );

    VoxelBlockCatalog reordered;
    for ( const utf8* pID : { "sand", "glass", "water", "stone" } )
    {
        VoxelBlockDef block;
        block._id   = hashed_string( pID );
        block._name = pID;
        SW_REQUIRE_TRUE( reordered.addBlock( block ).has_value() );
    }
    rig._world.rebindCatalog( reordered );
    Archive reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( rig._world.readState( reading ) );
    SW_EXPECT_TRUE( rig._world.getBlock( 20, 9, 20 ) == reordered.findBlockIndex( hashed_string( "glass" ) ).value() );
    SW_EXPECT_EQUAL( uint32( 32 * 32 ), rig._world.countBlocks( reordered.findBlockIndex( hashed_string( "stone" ) ).value() ) );
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
    SW_REQUIRE_TRUE( onlyStone.addBlock( block ).has_value() );
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
        SW_REQUIRE_TRUE( reordered.addBlock( block ).has_value() );
    }
    const VoxelBlockIndex glass = reordered.findBlockIndex( hashed_string( "glass" ) ).value();
    SW_REQUIRE_TRUE( glass != rig._glass );
    VoxelHotbar restored{ &reordered };
    Archive     reading( listSaved.data(), listSaved.size() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 5, restored.countBlock( glass ) );

    VoxelBlockCatalog onlySand;
    VoxelBlockDef     sand;
    sand._id   = hashed_string( "sand" );
    sand._name = "sand";
    SW_REQUIRE_TRUE( onlySand.addBlock( sand ).has_value() );
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
    settings._treeChance   = 0.0f;
    const auto loadTerrain = [&rig, &settings]( VoxelWorld& world )
    {
        SW_REQUIRE_TRUE( world.initialize( 2, 2, &rig._catalog ) );
        world.setChunkGenerator( VoxelTerrainSettings::makeChunkGenerator( settings ) );
        SW_REQUIRE_EQUAL( uint32( 4 ), world.updateResidency( 0, 0, 1, 4 ) );
    };
    VoxelWorld world;
    loadTerrain( world );
    vector<VoxelBlockIndex>  listBlock;
    const VoxelTerrainReport report = VoxelTerrainSettings::fillChunk( world, settings, 0, 0, listBlock );
    SW_EXPECT_TRUE( report._minHeight <= report._maxHeight );
    SW_EXPECT_EQUAL( uint32( 0 ), report._treeCount );
    SW_EXPECT_TRUE( listBlock == world.findChunk( 0, 0 )->_listBlock ); // 생성 훅이 낸 청크와 같다
    SW_EXPECT_TRUE( world.getBlock( 3, 0, 3 ) == bedrock );
    SW_EXPECT_TRUE( world.countBlocks( grass ) > 0 );
    SW_EXPECT_TRUE( world.countBlocks( dirt ) > 0 );
    SW_EXPECT_EQUAL( uint32( 0 ), world.getStoredChunkCount() ); // 생성한 청크는 바뀐 청크가 아니다

    const int32 surface = VoxelTerrainSettings::computeSurfaceHeight( 3, 3, settings );
    SW_EXPECT_TRUE( report._minHeight <= surface && surface <= report._maxHeight );
    SW_EXPECT_TRUE( world.findTopSolidY( 3, 3 ) == surface ); // 나무가 없으면 맨 위 단단한 블록이 지면이다

    VoxelWorld again;
    loadTerrain( again );
    for ( int32 chunkZ = 0; chunkZ < 2; ++chunkZ )
    {
        for ( int32 chunkX = 0; chunkX < 2; ++chunkX )
        {
            SW_EXPECT_TRUE( again.findChunk( chunkX, chunkZ )->_listBlock == world.findChunk( chunkX, chunkZ )->_listBlock );
        }
    }
}

// 청크마다 따로 생성해도 청크 경계를 넘는 나무가 맞물린다 — 나무 자리는 이웃 청크를 읽지 않고 정해지고, 잎은 걸친 청크마다 같은 자리에 온다.
SW_TEST_CASE( VoxelTerrainTest, TreesMatchAcrossChunksGeneratedApart )
{
    VoxelFixture          rig;
    const VoxelBlockIndex log    = rig.addBlock( "log", true, true );
    const VoxelBlockIndex leaves = rig.addBlock( "leaves", true, false );
    (void)rig.addBlock( "grass", true, true );
    (void)rig.addBlock( "dirt", true, true );
    VoxelTerrainSettings settings;
    settings._treeChance      = 0.3f;
    settings._waterLevel      = 0; // 물 · 모래가 없어 모든 기둥이 풀이다
    settings._heightAmplitude = 0;
    VoxelWorld world;
    SW_REQUIRE_TRUE( world.initialize( 2, 1, &rig._catalog ) );
    // 경계(x = 15 | 16)에 맞닿은 두 기둥: 한쪽에 줄기가 있으면 맞은편 같은 높이는 잎이다(잎 덩어리 반경 안). 경계에 나무가 서는 씨앗이 나올 때까지 씨앗을 바꿔 본다.
    uint32 matchedCount = 0;
    for ( uint32 seed = 1; seed <= 64 && matchedCount == 0; ++seed )
    {
        settings._seed = seed;
        vector<VoxelBlockIndex> listLeft;
        vector<VoxelBlockIndex> listRight;
        (void)VoxelTerrainSettings::fillChunk( world, settings, 0, 0, listLeft );
        (void)VoxelTerrainSettings::fillChunk( world, settings, 1, 0, listRight );
        for ( int32 z = 0; z < kVoxelChunkSize; ++z )
        {
            for ( int32 y = settings._baseHeight + 4; y < kVoxelChunkHeight; ++y ) // 줄기 맨 위 아래 두 칸까지가 잎 높이다
            {
                const VoxelBlockIndex leftEdge  = listLeft[static_cast<size_t>( ( y * kVoxelChunkSize + z ) * kVoxelChunkSize + kVoxelChunkSize - 1 )];
                const VoxelBlockIndex rightEdge = listRight[static_cast<size_t>( ( y * kVoxelChunkSize + z ) * kVoxelChunkSize )];
                if ( leftEdge == log )
                {
                    SW_EXPECT_TRUE( rightEdge == leaves );
                    ++matchedCount;
                }
                if ( rightEdge == log )
                {
                    SW_EXPECT_TRUE( leftEdge == leaves );
                    ++matchedCount;
                }
            }
        }
    }
    SW_EXPECT_TRUE( matchedCount > 0 );
}

/** @brief [VoxelTerrainTest] 광석 자리는 지형 씨앗을 따른다: 같은 씨앗이면 같은 자리, 다른 씨앗이면 다른 자리다(감사 K-8 — 씨앗 없는 해시로 꾸미면 씨앗을 바꿔도 그대로다) */
SW_TEST_CASE( VoxelTerrainTest, DecorationsFollowSeed )
{
    VoxelFixture          fixture;
    const VoxelBlockIndex coal             = fixture.addBlock( "coal", true, true );
    const auto            computeOreDigest = [&]( uint32 seed )
    {
        VoxelWorld world;
        SW_EXPECT_TRUE( world.initialize( 2, 2, &fixture._catalog ) );
        VoxelTerrainSettings settings;
        settings._seed            = seed;
        settings._treeChance      = 0.0f;
        settings._heightAmplitude = 0; // 평지 — 지면은 씨앗과 무관해 광석 자리만 씨앗을 따르는지 본다
        settings._listOreLayer    = {
            VoxelOreLayer{ hashed_string( "coal" ), 0.05f, kVoxelChunkHeight }
        };
        world.setChunkGenerator( VoxelTerrainSettings::makeChunkGenerator( settings ) );
        (void)world.updateResidency( 0, 0, 1, 4 );
        uint64 digest = 14695981039346656037ull;
        int32  count  = 0;
        for ( int32 z = 0; z < world.getSizeZ(); ++z )
        {
            for ( int32 x = 0; x < world.getSizeX(); ++x )
            {
                for ( int32 y = 0; y < kVoxelChunkHeight; ++y )
                {
                    if ( world.getBlock( x, y, z ) != coal )
                        continue;
                    ++count;
                    digest = ( digest ^ static_cast<uint64>( ( x * kVoxelChunkHeight + y ) * 4096 + z ) ) * 1099511628211ull;
                }
            }
        }
        SW_EXPECT_TRUE( count > 0 );
        return digest;
    };
    const uint64 first = computeOreDigest( 7u );
    SW_EXPECT_EQUAL( first, computeOreDigest( 7u ) );
    SW_EXPECT_TRUE( first != computeOreDigest( 8u ) );
}
