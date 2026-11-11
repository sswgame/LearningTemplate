#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/TileSet/Model/TileSetEditModel.h"

#include "Engine/TileMap/TileSetAsset.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 4 × 4 아틀라스의 규칙 타일 하나(위가 비면 1, 기본 5)입니다. */
    constexpr const utf8* kTileSetXML = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"1\">"
                                        "  <RuleTile name=\"ground\" cell=\"5\" solid=\"true\" outside=\"same\">"
                                        "    <Rule pattern=\".x. ... ...\" cell=\"1\"/>"
                                        "  </RuleTile>"
                                        "</TileSet>";

    struct TileSetEditModelTestUtil
    {
        static void load( TileSetEditModel& model )
        {
            model.applyText( kTileSetXML );
            model.selectBrush( 0 );
        }

        /** @brief 모델의 글을 런타임 읽기로 다시 읽습니다. 규칙 위반이 있으면 false 입니다. */
        static bool canReread( const TileSetEditModel& model, TileSetAsset& outAsset )
        {
            return outAsset.loadFromXMLText( model.captureText(), "<model>" );
        }
    };
    using Util = TileSetEditModelTestUtil;
} // namespace

// 이웃 단추는 상관없음 → 같음 → 다름 → 상관없음으로 돌고, 런타임이 그 패턴대로 셀을 고른다.
SW_TEST_CASE( TileSetEditModelTest, NeighborCyclesAndTheRuntimeFollows )
{
    TileSetEditModel model;
    Util::load( model );
    SW_REQUIRE_TRUE( model.addRule() );
    SW_EXPECT_TRUE( model.getSelectedRule() == 1 );
    SW_REQUIRE_TRUE( model.pickCell( 9, false ) );
    // 아래(이웃 6)를 "다름" 으로 — 두 번 누른다.
    SW_REQUIRE_TRUE( model.cycleNeighbor( 6 ) );
    SW_REQUIRE_TRUE( model.cycleNeighbor( 6 ) );
    SW_EXPECT_TRUE( model.findSelectedBrush()->_listRule[1]._arrNeighbor[6] == TileNeighborRule::Other );

    TileSetAsset asset;
    SW_REQUIRE_TRUE( Util::canReread( model, asset ) );
    // 1 × 2 맵의 위 칸(땅): 위는 맵 밖(같음으로 본다)이라 첫 규칙(위가 다름)은 안 맞고, 아래가 빈 칸이라 둘째 규칙(아래가 다름)이 맞는다.
    const uint16         ground  = 1;
    const vector<uint16> cells   = { ground, 0 };
    const TileVisual*    pVisual = asset.resolveVisual( cells, 1, 2, 0, 0 );
    SW_REQUIRE_NOT_NULL( pVisual );
    SW_EXPECT_TRUE( pVisual->computeCellAt( 0.0f ) == 9 );
}

// 프레임을 덧붙이면 애니메이션이 되고 fps 가 없으면 기본값을 둔다(읽기는 fps 없는 애니메이션을 거절한다). 마지막 프레임은 빼지 않는다.
SW_TEST_CASE( TileSetEditModelTest, AppendedFramesStayLoadable )
{
    TileSetEditModel model;
    Util::load( model );
    SW_REQUIRE_TRUE( model.pickCell( 2, true ) );
    SW_REQUIRE_TRUE( model.pickCell( 3, true ) );
    const TileVisual* pVisual = model.findSelectedVisual();
    SW_REQUIRE_NOT_NULL( pVisual );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( pVisual->_listFrameCell.size() ) );
    SW_EXPECT_TRUE( pVisual->_framesPerSecond > 0.0f );
    TileSetAsset asset;
    SW_EXPECT_TRUE( Util::canReread( model, asset ) );
    SW_EXPECT_TRUE( model.removeLastFrame() );
    SW_EXPECT_TRUE( model.removeLastFrame() );
    SW_EXPECT_FALSE( model.removeLastFrame() );
    // 아틀라스 밖 셀은 고르지 않는다.
    SW_EXPECT_FALSE( model.pickCell( 16, false ) );
}

// 이름은 비거나 겹칠 수 없다(맵은 브러시를 이름으로 칠한다). 새 브러시는 겹치지 않는 이름이다.
SW_TEST_CASE( TileSetEditModelTest, BrushNamesStayUnique )
{
    TileSetEditModel model;
    Util::load( model );
    model.addBrush( false );
    model.addBrush( false );
    SW_EXPECT_TRUE( model.getContent()._listBrush[1]._name == hashed_string( "tile" ) );
    SW_EXPECT_TRUE( model.getContent()._listBrush[2]._name == hashed_string( "tile2" ) );
    SW_EXPECT_FALSE( model.renameSelectedBrush( "ground" ) );
    SW_EXPECT_FALSE( model.renameSelectedBrush( "" ) );
    SW_EXPECT_TRUE( model.renameSelectedBrush( "grass" ) );
    TileSetAsset asset;
    SW_EXPECT_TRUE( Util::canReread( model, asset ) );
    SW_EXPECT_TRUE( asset.findBrush( hashed_string( "grass" ) ) == 2u );
}

// 규칙 타일의 마지막 규칙은 지우지 않는다(규칙 없는 규칙 타일은 로드 오류). 격자를 줄여 셀이 밖으로 나가면 위반으로 보이고 저장하지 않는다.
SW_TEST_CASE( TileSetEditModelTest, ViolationsAreShownNotSaved )
{
    TileSetEditModel model;
    Util::load( model );
    model.selectRule( 0 );
    SW_EXPECT_FALSE( model.removeSelectedRule() );
    SW_REQUIRE_TRUE( model.setAtlasGrid( 2, 2 ) ); // 셀 5 가 밖이다
    vector<TileSetIssue> listIssue;
    model.collectIssues( listIssue );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listIssue.size() ) );
    SW_EXPECT_TRUE( listIssue[0]._brushIndex == 0 );
    SW_EXPECT_FALSE( listIssue[0]._ruleIndex.has_value() );
    // 되돌리기 글은 위반이 든 채로 돌아온다(편집 중의 상태).
    TileSetEditModel restored;
    restored.applyText( model.captureText() );
    SW_EXPECT_EQUAL( 2, restored.getContent()._columnCount );
    SW_EXPECT_FALSE( model.saveDocument( "never/written.tileset.xml" ) );
    SW_EXPECT_FALSE( model.getStatus().empty() );
}
