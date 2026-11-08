#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Feature/World/Housing/Catalog/HousingPart.h"
#include "GameFramework/Kits/Feature/World/Housing/Rule/HousingLayout.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/World/Land/LandRegistry.h"

using namespace sw;

namespace
{
    /** @brief 테스트 카탈로그 글입니다. 바닥 · 벽(끝 소켓) · 문 · 탁자(2x1, 자유 배치) · 침대(2x2) 와, 옛 카탈로그면 침대 대신 간이침대(cot)입니다. */
    struct HousingCatalogText
    {
        static string make( bool bWithTable, int32 bedVersion, int32 bedMinLoadVersion, bool bOldCot )
        {
            string text = R"(<HousingPartCatalog currency="Gold" cellSize="1" levelHeight="3" socketSnapRadius="0.6">
                <Part id="floor" name="Floor" prefab="game/empty/prefabs/housing/floor.prefab.xml" layer="Floor" height="0.1" cost="5" refundRatio="0.5"/>
                <Part id="wall" name="Wall" prefab="game/empty/prefabs/housing/wall.prefab.xml" layer="Structure" height="2.8" cost="10" snapTo="wallEnd">
                    <Socket kind="wallEnd" position="-0.5,0,0"/><Socket kind="wallEnd" position="0.5,0,0"/>
                </Part>
                <Part id="door" name="Door" prefab="game/empty/prefabs/housing/door.prefab.xml" layer="Structure" height="2.2" cost="20" snapTo="wallEnd">
                    <Socket kind="wallEnd" position="-0.5,0,0"/><Socket kind="wallEnd" position="0.5,0,0"/>
                </Part>)";
            if ( bWithTable )
                text += R"(<Part id="table" name="Table" prefab="game/empty/prefabs/housing/table.prefab.xml" sizeX="2" sizeZ="1" height="0.8" cost="30" freePlacement="true"/>)";
            if ( bOldCot )
                text += R"(<Part id="cot" name="Cot" prefab="game/empty/prefabs/housing/bed.prefab.xml" sizeX="2" sizeZ="2" height="0.6" cost="40"/>)";
            else
            {
                StringBuilder<constant::buffer::kMax256> textPart;
                textPart.appendFormat( R"(<Part id="bed" name="Bed" prefab="game/empty/prefabs/housing/bed.prefab.xml" sizeX="2" sizeZ="2" height="0.6" cost="60" version="%#" minLoadVersion="%#" replaces="cot"/>)",
                                       bedVersion, bedMinLoadVersion );
                text += textPart.c_str();
            }
            text += "</HousingPartCatalog>";
            return text;
        }
    };

    /** @brief 6 x 6 칸 · 2 층 부지 "home" 과 1000 골드 지갑입니다. */
    struct HousingFixture
    {
        HousingPartCatalog  _catalog;
        Wallet              _wallet;
        HousingLayout       _layout;
        HousingPlotSettings _settings;

        explicit HousingFixture( bool bFreePlacement = false, const string& catalogText = HousingCatalogText::make( true, 2, 1, false ) )
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( catalogText, "test" ) );
            _settings._plotID         = hashed_string( "home" );
            _settings._sizeX          = 6;
            _settings._sizeZ          = 6;
            _settings._levelCount     = 2;
            _settings._bFreePlacement = bFreePlacement;
            _layout.initialize( _settings, _catalog );
            _layout.bindWallet( &_wallet );
            _wallet.setBalance( hashed_string( "Gold" ), 1000 );
        }

        static HousingCandidate makeCandidate( const utf8* pPartID, float32 x, float32 z, int32 level, float32 yawDegrees, HousingPlacementMode mode = HousingPlacementMode::Grid )
        {
            HousingCandidate candidate;
            candidate._partID              = hashed_string( pPartID );
            candidate._transform._position = float2{ x, z };
            candidate._transform._level    = level;
            candidate._transform._yaw      = yawDegrees * MathUtil::kDegreeToRadian;
            candidate._mode                = mode;
            return candidate;
        }

        HousingPlaceResult place( const utf8* pPartID, float32 x, float32 z, int32 level = 0, float32 yawDegrees = 0.0f, HousingPlacementMode mode = HousingPlacementMode::Grid )
        {
            uint32 placementID = 0;
            return _layout.place( makeCandidate( pPartID, x, z, level, yawDegrees, mode ), placementID );
        }

        /** @brief 칸 [0, @p count) x [0, @p count) 에 0 층 바닥을 깝니다. */
        void layFloors( int32 count )
        {
            for ( int32 z = 0; z < count; ++z )
            {
                for ( int32 x = 0; x < count; ++x )
                {
                    SW_REQUIRE_TRUE( place( "floor", static_cast<float32>( x ) + 0.5f, static_cast<float32>( z ) + 0.5f ) == HousingPlaceResult::Ok );
                }
            }
        }

        int64 getFunds() const { return _wallet.getBalance( hashed_string( "Gold" ) ); }

        static vector<uint8> save( const HousingLayout& layout )
        {
            Archive archive;
            layout.writeState( archive );
            return vector<uint8>( archive.getData(), archive.getData() + archive.getSize() );
        }

        static bool load( HousingLayout& layout, const vector<uint8>& bytes )
        {
            Archive archive( bytes.data(), bytes.size() );
            return layout.readState( archive );
        }
    };
} // namespace

// ---- 카탈로그 ----

SW_TEST_CASE( HousingPartCatalogTest, LoadsPartsSocketsAndReplacement )
{
    HousingFixture rig;
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( rig._catalog.getParts().size() ) );
    const HousingPartDef* pWall = rig._catalog.findPart( hashed_string( "wall" ) );
    SW_REQUIRE_TRUE( pWall != nullptr );
    SW_EXPECT_TRUE( pWall->_layer == HousingPartLayer::Structure );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( pWall->_listSocket.size() ) );
    SW_EXPECT_TRUE( rig._catalog.findReplacement( hashed_string( "cot" ) ) == rig._catalog.findPart( hashed_string( "bed" ) ) );
    SW_EXPECT_TRUE( rig._catalog.findPart( hashed_string( "table" ) )->_bFreePlacement );
}

// 스냅 지점 검증(에디터 저작의 정본): 점유 상자 밖의 소켓, 아무 부품에도 없는 소켓 종류, 판보다 높은 최소 판은 파일 전체를 거절한다.
SW_TEST_CASE( HousingPartCatalogTest, RefusesBadSnapPointsAndVersions )
{
    HousingPartCatalog outsideSocket;
    SW_EXPECT_LOG( "HousingPartCatalog.PartInvalid" );
    SW_EXPECT_FALSE( outsideSocket.loadFromXMLText(
        R"(<HousingPartCatalog currency="Gold"><Part id="wall" name="Wall" prefab="w.prefab.xml" layer="Structure"><Socket kind="end" position="1.5,0,0"/></Part></HousingPartCatalog>)", "test" ) );

    HousingPartCatalog unknownKind;
    SW_EXPECT_LOG( "HousingPartCatalog.PartInvalid" );
    SW_EXPECT_FALSE( unknownKind.loadFromXMLText( R"(<HousingPartCatalog currency="Gold"><Part id="door" name="Door" prefab="d.prefab.xml" snapTo="nothing"/></HousingPartCatalog>)", "test" ) );

    HousingPartCatalog badVersion;
    SW_EXPECT_LOG( "HousingPartCatalog.PartInvalid" );
    SW_EXPECT_FALSE( badVersion.loadFromXMLText( R"(<HousingPartCatalog currency="Gold"><Part id="bed" name="Bed" prefab="b.prefab.xml" version="1" minLoadVersion="2"/></HousingPartCatalog>)", "test" ) );
}

// ---- 스냅 · 회전 ----

// 격자: 90 도로 맞추고 돌린 점유 칸의 왼쪽 아래 칸을 반올림한다. 2 x 1 탁자를 80 도로 들면 1 x 2 로 선다.
SW_TEST_CASE( HousingLayoutTest, GridSnapRoundsCellsAndQuarterTurns )
{
    HousingFixture          rig;
    const HousingPlaceCheck flat = rig._layout.evaluate( HousingFixture::makeCandidate( "table", 2.3f, 1.2f, 0, 10.0f ) );
    SW_EXPECT_NEAR( 2.0f, flat._transform._position._x, 1.0e-5f );
    SW_EXPECT_NEAR( 1.5f, flat._transform._position._y, 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, flat._transform._yaw, 1.0e-5f );
    SW_EXPECT_TRUE( flat._result == HousingPlaceResult::NoSupport ); // 바닥이 없다

    const HousingPlaceCheck turned = rig._layout.evaluate( HousingFixture::makeCandidate( "table", 2.3f, 1.2f, 0, 80.0f ) );
    SW_EXPECT_NEAR( 2.5f, turned._transform._position._x, 1.0e-5f );
    SW_EXPECT_NEAR( 1.0f, turned._transform._position._y, 1.0e-5f );
    SW_EXPECT_NEAR( MathUtil::kHalfPi, turned._transform._yaw, 1.0e-5f );
    SW_EXPECT_EQUAL( 2, turned._cellMin._x );
    SW_EXPECT_EQUAL( 2, turned._cellMax._x );
    SW_EXPECT_EQUAL( 0, turned._cellMin._y );
    SW_EXPECT_EQUAL( 1, turned._cellMax._y );
}

// 소켓: 문이 벽 끝 소켓에 자기 소켓을 맞춰 붙는다(발하임식 점 대 점). 날 자리가 반지름 밖이면 격자로 간다.
SW_TEST_CASE( HousingLayoutTest, DoorSnapsToWallEndSocket )
{
    HousingFixture rig;
    rig.layFloors( 4 );
    uint32 wallID = 0;
    SW_REQUIRE_TRUE( rig._layout.place( HousingFixture::makeCandidate( "wall", 0.5f, 0.5f, 0, 0.0f ), wallID ) == HousingPlaceResult::Ok );

    const HousingPlaceCheck snapped = rig._layout.evaluate( HousingFixture::makeCandidate( "door", 1.3f, 0.6f, 0, 0.0f ) );
    SW_EXPECT_TRUE( snapped._result == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( wallID, snapped._socketPlacementID );
    SW_EXPECT_NEAR( 1.5f, snapped._transform._position._x, 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, snapped._transform._position._y, 1.0e-5f );

    const HousingPlaceCheck far = rig._layout.evaluate( HousingFixture::makeCandidate( "door", 3.4f, 2.6f, 0, 0.0f ) );
    SW_EXPECT_EQUAL( 0u, far._socketPlacementID );
    SW_EXPECT_NEAR( 3.5f, far._transform._position._x, 1.0e-5f );
}

// 자유 배치: 부지와 부품이 모두 허락해야 한다. 45 도 탁자는 돌린 상자를 덮는 칸을 모두 차지한다.
SW_TEST_CASE( HousingLayoutTest, FreePlacementNeedsPlotAndPart )
{
    HousingFixture locked;
    SW_EXPECT_TRUE( locked._layout.evaluate( HousingFixture::makeCandidate( "table", 2.0f, 2.0f, 0, 45.0f, HousingPlacementMode::Free ) )._result ==
                    HousingPlaceResult::FreePlacementNotAllowed );

    HousingFixture open( true );
    open.layFloors( 4 );
    SW_EXPECT_TRUE( open._layout.evaluate( HousingFixture::makeCandidate( "floor", 4.2f, 4.2f, 0, 0.0f, HousingPlacementMode::Free ) )._result ==
                    HousingPlaceResult::FreePlacementNotAllowed ); // 바닥은 자유 배치를 허락하지 않는다
    const HousingPlaceCheck free = open._layout.evaluate( HousingFixture::makeCandidate( "table", 2.0f, 2.0f, 0, 45.0f, HousingPlacementMode::Free ) );
    SW_EXPECT_TRUE( free._result == HousingPlaceResult::Ok );
    SW_EXPECT_NEAR( MathUtil::kPi * 0.25f, free._transform._yaw, 1.0e-5f );
    SW_EXPECT_EQUAL( 0, free._cellMin._x );
    SW_EXPECT_EQUAL( 3, free._cellMax._x );
}

// ---- 배치 불가 까닭 ----

SW_TEST_CASE( HousingLayoutTest, RefusalsNameTheReason )
{
    HousingFixture rig;
    SW_EXPECT_TRUE( rig.place( "sofa", 0.5f, 0.5f ) == HousingPlaceResult::UnknownPart );
    SW_EXPECT_TRUE( rig.place( "floor", 6.5f, 0.5f ) == HousingPlaceResult::OutOfPlot );
    SW_EXPECT_TRUE( rig.place( "floor", 0.5f, 0.5f, 2 ) == HousingPlaceResult::BadLevel );
    SW_EXPECT_TRUE( rig.place( "table", 1.0f, 0.5f ) == HousingPlaceResult::NoSupport );
    rig.layFloors( 3 );
    SW_EXPECT_TRUE( rig.place( "floor", 0.5f, 0.5f ) == HousingPlaceResult::Overlap );
    SW_EXPECT_TRUE( rig.place( "table", 1.0f, 0.5f ) == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( rig.place( "wall", 1.5f, 0.5f ) == HousingPlaceResult::Overlap );       // 구조물과 가구는 칸을 나누지 않는다
    SW_EXPECT_TRUE( rig.place( "floor", 2.5f, 2.5f, 1 ) == HousingPlaceResult::NoSupport ); // 받칠 벽이 없다
    SW_EXPECT_TRUE( rig.place( "wall", 2.5f, 2.5f ) == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( rig.place( "floor", 2.5f, 2.5f, 1 ) == HousingPlaceResult::Ok );
}

// ---- 비용 · 회수 · 받침 ----

SW_TEST_CASE( HousingLayoutTest, CostRefundAndSupport )
{
    HousingFixture rig;
    rig._wallet.setBalance( hashed_string( "Gold" ), 100 );
    uint32 floorID = 0;
    SW_REQUIRE_TRUE( rig._layout.place( HousingFixture::makeCandidate( "floor", 0.5f, 0.5f, 0, 0.0f ), floorID ) == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( 95, static_cast<int32>( rig.getFunds() ) );
    SW_EXPECT_TRUE( rig._layout.remove( floorID ) == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( 97, static_cast<int32>( rig.getFunds() ) ); // 5 의 절반 내림 = 2

    rig.layFloors( 2 );
    rig._wallet.setBalance( hashed_string( "Gold" ), 10 );
    SW_EXPECT_TRUE( rig._layout.evaluate( HousingFixture::makeCandidate( "bed", 1.0f, 1.0f, 0, 0.0f ) )._result == HousingPlaceResult::NotEnoughFunds );
    SW_EXPECT_TRUE( rig.place( "bed", 1.0f, 1.0f ) == HousingPlaceResult::NotEnoughFunds );
    SW_EXPECT_EQUAL( 10, static_cast<int32>( rig.getFunds() ) );

    rig._wallet.setBalance( hashed_string( "Gold" ), 1000 );
    SW_REQUIRE_TRUE( rig.place( "bed", 1.0f, 1.0f ) == HousingPlaceResult::Ok );
    const uint32 underBed = rig._layout.getPlacements().front()._placementID;
    SW_EXPECT_TRUE( rig._layout.remove( underBed ) == HousingPlaceResult::SupportsOthers );
    SW_EXPECT_TRUE( rig._layout.move( underBed, HousingTransform{
                                                    float2{ 4.5f, 4.5f },
                                                    0, 0.0f
    },
                                      HousingPlacementMode::Grid ) == HousingPlaceResult::SupportsOthers );

    HousingFixture noWallet;
    noWallet._layout.bindWallet( nullptr );
    SW_EXPECT_TRUE( noWallet.place( "floor", 0.5f, 0.5f ) == HousingPlaceResult::NoWallet );
}

// ---- 되돌리기 ----

// 이력은 편집의 역이고 값도 그대로 되돌린다. 되돌린 배치는 같은 안정 id 로 돌아온다. 새 편집은 다시 할 것을 버린다.
SW_TEST_CASE( HousingLayoutTest, UndoRedoRestoresPlacementsAndFunds )
{
    HousingFixture rig;
    SW_EXPECT_TRUE( rig._layout.undo() == HousingPlaceResult::NothingToUndo );
    uint32 floorID = 0;
    SW_REQUIRE_TRUE( rig._layout.place( HousingFixture::makeCandidate( "floor", 0.5f, 0.5f, 0, 0.0f ), floorID ) == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig._layout.move( floorID, HousingTransform{
                                                    float2{ 1.4f, 0.6f },
                                                    0, 0.0f
    },
                                       HousingPlacementMode::Grid ) == HousingPlaceResult::Ok );
    SW_EXPECT_NEAR( 1.5f, rig._layout.findPlacement( floorID )->_transform._position._x, 1.0e-5f );
    SW_EXPECT_EQUAL( 995, static_cast<int32>( rig.getFunds() ) );

    SW_EXPECT_TRUE( rig._layout.undo() == HousingPlaceResult::Ok ); // 옮기기
    SW_EXPECT_NEAR( 0.5f, rig._layout.findPlacement( floorID )->_transform._position._x, 1.0e-5f );
    SW_EXPECT_TRUE( rig._layout.undo() == HousingPlaceResult::Ok ); // 놓기 — 낸 값 전부
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( rig._layout.getPlacements().size() ) );
    SW_EXPECT_EQUAL( 1000, static_cast<int32>( rig.getFunds() ) );
    SW_EXPECT_TRUE( rig._layout.undo() == HousingPlaceResult::NothingToUndo );

    SW_EXPECT_TRUE( rig._layout.redo() == HousingPlaceResult::Ok );
    SW_EXPECT_TRUE( rig._layout.redo() == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig._layout.findPlacement( floorID ) != nullptr );
    SW_EXPECT_NEAR( 1.5f, rig._layout.findPlacement( floorID )->_transform._position._x, 1.0e-5f );
    SW_EXPECT_EQUAL( 995, static_cast<int32>( rig.getFunds() ) );
    SW_EXPECT_TRUE( rig._layout.redo() == HousingPlaceResult::NothingToRedo );

    SW_REQUIRE_TRUE( rig._layout.remove( floorID ) == HousingPlaceResult::Ok );
    SW_EXPECT_EQUAL( 997, static_cast<int32>( rig.getFunds() ) );
    SW_EXPECT_TRUE( rig._layout.undo() == HousingPlaceResult::Ok ); // 치우기 — 돌려받은 값을 다시 낸다
    SW_EXPECT_EQUAL( 995, static_cast<int32>( rig.getFunds() ) );
    SW_EXPECT_TRUE( rig._layout.findPlacement( floorID ) != nullptr );
    SW_EXPECT_TRUE( rig._layout.canRedo() );
    SW_EXPECT_TRUE( rig.place( "floor", 3.5f, 3.5f ) == HousingPlaceResult::Ok );
    SW_EXPECT_FALSE( rig._layout.canRedo() );
    SW_EXPECT_EQUAL( 2u, rig._layout.getPlacements().back()._placementID ); // id 는 다시 쓰지 않는다
}

// ---- 저장 · 불러오기 ----

SW_TEST_CASE( HousingLayoutTest, SaveLoadRoundTripsStableIDs )
{
    HousingFixture rig;
    rig.layFloors( 3 );
    SW_REQUIRE_TRUE( rig.place( "wall", 0.5f, 0.5f ) == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig.place( "floor", 0.5f, 0.5f, 1 ) == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig.place( "table", 2.3f, 1.2f, 0, 80.0f ) == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig._layout.remove( 5 ) == HousingPlaceResult::Ok ); // 가운데가 빈 id 가 남는다
    const vector<uint8> bytes = HousingFixture::save( rig._layout );

    HousingFixture loaded;
    SW_REQUIRE_TRUE( HousingFixture::load( loaded._layout, bytes ) );
    SW_EXPECT_EQUAL( rig._layout.getPlacements().size(), loaded._layout.getPlacements().size() );
    SW_EXPECT_EQUAL( rig._layout.getNextPlacementID(), loaded._layout.getNextPlacementID() );
    SW_EXPECT_TRUE( loaded._layout.findPlacement( 5 ) == nullptr );
    SW_EXPECT_FALSE( loaded._layout.canUndo() ); // 이력은 싣지 않는다
    SW_EXPECT_TRUE( HousingFixture::save( loaded._layout ) == bytes );
    SW_EXPECT_TRUE( loaded.place( "floor", 0.5f, 0.5f ) == HousingPlaceResult::Overlap ); // 다시 채운 칸 점유
}

// 다른 부지 · 없는 부품 · 읽지 못하는 판 · 지금 규칙을 어기는 배치는 거절하고 상태를 그대로 둔다. 지운 부품은 이어받은 부품으로 읽는다.
SW_TEST_CASE( HousingLayoutTest, LoadRefusesMismatchesAndFollowsReplacement )
{
    HousingFixture rig;
    rig.layFloors( 2 );
    SW_REQUIRE_TRUE( rig.place( "table", 1.0f, 0.5f ) == HousingPlaceResult::Ok );
    SW_REQUIRE_TRUE( rig.place( "bed", 4.0f, 4.0f ) == HousingPlaceResult::NoSupport );
    const vector<uint8> bytes = HousingFixture::save( rig._layout );

    HousingFixture otherPlot;
    otherPlot._settings._plotID = hashed_string( "barn" );
    otherPlot._layout.initialize( otherPlot._settings, otherPlot._catalog );
    SW_EXPECT_LOG( "HousingLayout.SavedPlotDiffers" );
    SW_EXPECT_FALSE( HousingFixture::load( otherPlot._layout, bytes ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( otherPlot._layout.getPlacements().size() ) );

    HousingFixture noTable( false, HousingCatalogText::make( false, 2, 1, false ) );
    SW_EXPECT_LOG( "HousingLayout.SavedPartUnknown" );
    SW_EXPECT_FALSE( HousingFixture::load( noTable._layout, bytes ) );

    HousingFixture small;
    small._settings._sizeX = 1;
    small._layout.initialize( small._settings, small._catalog );
    SW_EXPECT_LOG( "HousingLayout.SavedPlacementBreaksRules" );
    SW_EXPECT_FALSE( HousingFixture::load( small._layout, bytes ) );

    // 옛 카탈로그의 간이침대 → 새 카탈로그의 침대(replaces="cot"). 판이 최소 판 밖이면 거절한다.
    HousingFixture oldCatalog( false, HousingCatalogText::make( true, 2, 1, true ) );
    oldCatalog.layFloors( 2 );
    SW_REQUIRE_TRUE( oldCatalog.place( "cot", 1.0f, 1.0f ) == HousingPlaceResult::Ok );
    const vector<uint8> cotBytes = HousingFixture::save( oldCatalog._layout );
    HousingFixture      replaced;
    SW_REQUIRE_TRUE( HousingFixture::load( replaced._layout, cotBytes ) );
    SW_EXPECT_TRUE( replaced._layout.getPlacements().back()._partID == hashed_string( "bed" ) );
    SW_EXPECT_EQUAL( 2, replaced._layout.getPlacements().back()._partVersion );

    const vector<uint8> bedV2Bytes = HousingFixture::save( replaced._layout );
    HousingFixture      bedV3{ false, HousingCatalogText::make( true, 3, 3, false ) };
    SW_EXPECT_LOG( "HousingLayout.SavedPartVersionUnsupported" );
    SW_EXPECT_FALSE( HousingFixture::load( bedV3._layout, bedV2Bytes ) );
    HousingFixture bedV3Compatible{ false, HousingCatalogText::make( true, 3, 2, false ) };
    SW_EXPECT_TRUE( HousingFixture::load( bedV3Compatible._layout, bedV2Bytes ) );
}

/**
 * @brief [HousingLayoutTest] 부지는 공유 땅에서 자기 칸 전체를 차지한다 — 남의 칸(다른 부지 · 다른 키트)과 겹치면 하나도 차지하지 않고 거절하고, 닫으면 반납해 다른 부지가 쓴다
 */
SW_TEST_CASE( HousingLayoutTest, PlotClaimsItsCellsOnTheSharedLand )
{
    LandRegistry land;
    land.initialize( 16, 16, 1.0f, float3{ 0.0f, 0.0f, 0.0f } );
    HousingFixture home;
    SW_REQUIRE_TRUE( home._layout.bindLand( &land, int2{ 2, 2 } ) );
    SW_EXPECT_TRUE( home._layout.hasClaimedLand() );
    SW_EXPECT_TRUE( land.getOwnerName( 2, 2 ) == hashed_string( "Housing.home" ) );
    SW_EXPECT_TRUE( land.getOwnerName( 7, 7 ) == hashed_string( "Housing.home" ) ); // 6 x 6 칸의 끝
    SW_EXPECT_TRUE( land.getOwnerName( 8, 8 ).empty() );
    SW_EXPECT_FALSE( land.isBlockedFor( LandRegistry::kNoOwner, 3, 3 ) ); // 차지일 뿐 막힘이 아니다(길은 지난다)

    HousingFixture neighbour;
    neighbour._settings._plotID = hashed_string( "next" );
    neighbour._layout.initialize( neighbour._settings, neighbour._catalog );
    SW_EXPECT_LOG( "HousingLayout.LandTaken" );
    SW_EXPECT_FALSE( neighbour._layout.bindLand( &land, int2{ 5, 5 } ) ); // home 의 칸과 겹친다
    SW_EXPECT_FALSE( neighbour._layout.hasClaimedLand() );
    SW_EXPECT_TRUE( land.getOwnerName( 10, 10 ).empty() ); // 반쯤 차지하지 않는다

    home._layout.releaseLand();
    SW_EXPECT_TRUE( land.getOwnerName( 2, 2 ).empty() );
    SW_EXPECT_TRUE( neighbour._layout.bindLand( &land, int2{ 5, 5 } ) );
    SW_EXPECT_TRUE( neighbour._layout.bindLand( nullptr, int2{} ) ); // 땅 없는 부지는 단독이다 — 다시 묶으면 앞의 차지를 반납한다
    SW_EXPECT_FALSE( neighbour._layout.hasClaimedLand() );
    SW_EXPECT_TRUE( land.getOwnerName( 5, 5 ).empty() );
}
