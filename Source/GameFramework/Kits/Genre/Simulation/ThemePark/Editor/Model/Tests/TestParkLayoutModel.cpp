#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/Model/ParkLayoutModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ParkLayoutModelTestUtil
    {
        /** @brief 실제 데이터(`coasters.xml` 의 family)와 같은 회로입니다 — 끝이 스테이션 뒤에 와서 닫는 곡선이 접히지 않는다(접히면 빌더가 경고한다). */
        static constexpr const utf8* kCoasters = R"(<CoasterCatalog>
  <Layout id="family" name="Little Dipper" startHeight="1">
    <Piece type="Station" length="10"/>
    <Piece type="LiftHill" length="16" height="8"/>
    <Piece type="Drop" length="16" height="8"/>
    <Piece type="TurnLeft" radius="10" angle="3.14159" bank="20"/>
    <Piece type="Straight" length="54"/>
    <Piece type="TurnLeft" radius="10" angle="3.14159" bank="15"/>
    <Piece type="Brakes" length="8"/>
  </Layout>
</CoasterCatalog>)";

        static constexpr const utf8* kCoaster = R"(<Coaster layout="family" x="90" z="0" heading="0" color="0.3,0.75,0.4,1" capacity="16" loadTime="15" cost="4000"/>)";

        static string makeFlatRide( const utf8* pID, const utf8* pName, const utf8* pX, const utf8* pEntranceZ, const utf8* pCost )
        {
            return string( "<FlatRide id=\"" ) + pID + "\" name=\"" + pName + "\" excitement=\"2\" intensity=\"1\" nausea=\"1\" cycleTime=\"40\" capacity=\"16\" price=\"3\" runningCost=\"3\" cost=\"" + pCost +
                   "\" x=\"" + pX + "\" z=\"0\" entranceX=\"" + pX + "\" entranceZ=\"" + pEntranceZ + "\" shape=\"Cylinder\" sizeX=\"6\" sizeY=\"2\" sizeZ=\"6\" color=\"1,1,1,1\"/>";
        }

        static string makeLayout( const string& body )
        {
            return string( "<ParkLayout gateX=\"20\" gateZ=\"-40\" startingCash=\"14000\" entryFee=\"5\">" ) + body + "</ParkLayout>";
        }

        /** @brief 평지 둘(서로 먼 자리, 입구는 영역 밖)과 코스터 하나입니다. */
        static string makeCleanLayout()
        {
            return makeLayout( makeFlatRide( "cups", "Spinning Cups", "45", "-10", "2000" ) + makeFlatRide( "carousel", "Merry-Go-Round", "-10", "-10", "1500" ) + kCoaster );
        }

        [[nodiscard]] static bool apply( ParkLayoutModel& model, const string& layoutText, const utf8* pCoasters = kCoasters, bool bForce = false )
        {
            return model.applyTexts( layoutText, pCoasters, "test/rides.xml", "test/coasters.xml", bForce );
        }
    };
    using Util = ParkLayoutModelTestUtil;
} // namespace

SW_TEST_CASE( ParkLayoutModelTest, ReadsRidesGateAndTotals )
{
    ParkLayoutModel model;
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Unread );
    SW_EXPECT_TRUE( Util::apply( model, Util::makeCleanLayout() ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_EXPECT_EQUAL( 20.0f, model.getGatePosition()._x );
    SW_EXPECT_EQUAL( -40.0f, model.getGatePosition()._z );
    SW_EXPECT_EQUAL( 14000, model.getStartingCash() );
    SW_EXPECT_EQUAL( 5, model.getEntryFee() );
    SW_EXPECT_EQUAL( int64{ 7500 }, model.computeTotalBuildCost() );
    SW_EXPECT_EQUAL( 0u, model.countIssues() );
}

// 평지 놀이기구에는 태우는 시간이 없다 — 표에 키트 기본값(15)이 아니라 도는 시간이 나와야 한다. 코스터는 태우는 시간이다.
SW_TEST_CASE( ParkLayoutModelTest, TimeIsTheCycleForFlatRidesAndTheLoadForCoasters )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_EXPECT_EQUAL( 40.0f, model.getRides()[0]._timeSeconds );
    SW_EXPECT_EQUAL( 3, model.getRides()[0]._price );
    SW_EXPECT_FALSE( model.getRides()[0]._bCoaster );
    SW_EXPECT_EQUAL( 15.0f, model.getRides()[2]._timeSeconds );
    SW_EXPECT_TRUE( model.getRides()[2]._bCoaster );
}

SW_TEST_CASE( ParkLayoutModelTest, SameTextIsNotReadAgainUnlessForced )
{
    ParkLayoutModel model;
    const string    layout = Util::makeCleanLayout();
    SW_EXPECT_TRUE( Util::apply( model, layout ) );
    SW_EXPECT_FALSE( Util::apply( model, layout ) );
    SW_EXPECT_TRUE( Util::apply( model, layout, Util::kCoasters, true ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
}

// 예전에는 배치 글만 비교해 코스터 파일을 고쳐도 트랙이 그대로였다.
SW_TEST_CASE( ParkLayoutModelTest, ChangingOnlyTheCoasterFileReloads )
{
    ParkLayoutModel model;
    const string    layout = Util::makeCleanLayout();
    SW_EXPECT_TRUE( Util::apply( model, layout ) );
    string renamed{ Util::kCoasters };
    renamed.replace( renamed.find( "Little Dipper" ), 13, "Big Dipper" );
    SW_EXPECT_TRUE( Util::apply( model, layout, renamed.c_str() ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_EXPECT_TRUE( model.getRides()[2]._name == "Big Dipper" );
}

// 예전에는 틀린 파일을 프레임마다 다시 읽고 경고했다 — 같은 글이면 다시 읽지 않고, 글이 고쳐지면 읽힌다.
SW_TEST_CASE( ParkLayoutModelTest, BrokenFileIsReadOnceAndRecoversWhenFixed )
{
    SW_EXPECT_LOG( "ParkLayout.CoasterLayoutNotCoaster" );
    SW_EXPECT_LOG( "ParkLayoutModel.DidNotLoadThrough" );
    ParkLayoutModel model;
    const string    broken = Util::makeLayout( R"(<Coaster layout="nope" x="0" z="0" color="1,1,1,1" capacity="16" loadTime="15" cost="4000"/>)" );
    SW_EXPECT_TRUE( Util::apply( model, broken ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Failed );
    SW_EXPECT_TRUE( model.getRides().empty() );
    SW_EXPECT_FALSE( Util::apply( model, broken ) );

    SW_EXPECT_TRUE( Util::apply( model, Util::makeCleanLayout() ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
}

SW_TEST_CASE( ParkLayoutModelTest, MissingAndFailedClearThePreviewAndAllowARereadOfTheSameText )
{
    ParkLayoutModel model;
    const string    layout = Util::makeCleanLayout();
    (void)Util::apply( model, layout );
    model.selectRide( 1 );
    model.setMissing();
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Missing );
    SW_EXPECT_TRUE( model.getRides().empty() );
    SW_EXPECT_EQUAL( ParkLayoutModel::kNoRide, model.getSelectedRide() );
    SW_EXPECT_TRUE( Util::apply( model, layout ) ); // 파일이 돌아왔다
    model.setFailed();
    SW_EXPECT_TRUE( Util::apply( model, layout ) ); // 읽기 실패 뒤에 같은 글이 읽혔다
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
}

// 예전에는 8 점마다 하나만 남겨 회로 끝에 틈이 보였다.
SW_TEST_CASE( ParkLayoutModelTest, CoasterTrackIsDrawnAsAClosedCircuit )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    const vector<float3>& listPoint = model.getRides()[2]._listTrackPoint;
    SW_REQUIRE_TRUE( listPoint.size() > 2 );
    SW_EXPECT_EQUAL( listPoint.front()._x, listPoint.back()._x );
    SW_EXPECT_EQUAL( listPoint.front()._y, listPoint.back()._y );
    SW_EXPECT_EQUAL( listPoint.front()._z, listPoint.back()._z );
    SW_EXPECT_TRUE( model.getRides()[0]._listTrackPoint.empty() );
}

// 코스터를 더블클릭하면 스테이션(4 m)이 아니라 트랙 전체가 화면에 들어와야 한다(언리얼 · 유니티의 F 키가 범위를 담는 것과 같다).
SW_TEST_CASE( ParkLayoutModelTest, FocusHoldsTheWholeRide )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );

    const ParkRidePreview& flat = model.getRides()[0];
    SW_EXPECT_EQUAL( 45.0f, flat.computeFocusCenter()._x );
    SW_EXPECT_EQUAL( 1.0f, flat.computeFocusCenter()._y );     // 높이의 가운데
    SW_EXPECT_NEAR( 4.36f, flat.computeFocusRadius(), 0.01f ); // 6 x 2 x 6 상자의 대각선 절반

    const ParkRidePreview& coaster = model.getRides()[2];
    SW_EXPECT_TRUE( coaster.computeFocusRadius() > 20.0f );
    for ( const float3& point : coaster._listTrackPoint )
    {
        SW_EXPECT_TRUE( float3::getDistance( coaster.computeFocusCenter(), point ) <= coaster.computeFocusRadius() + 0.01f );
    }
}

SW_TEST_CASE( ParkLayoutModelTest, CheckFindsOverlapsOnBothRides )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeLayout( Util::makeFlatRide( "a", "A", "0", "-10", "1" ) + Util::makeFlatRide( "b", "B", "5", "-10", "1" ) + Util::makeFlatRide( "c", "C", "50", "-10", "1" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[0]._listIssue.size() );
    SW_EXPECT_TRUE( model.getRides()[0]._listIssue[0]._kind == ParkRideIssueKind::Overlap );
    SW_EXPECT_EQUAL( 1u, model.getRides()[0]._listIssue[0]._otherRide );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[1]._listIssue.size() );
    SW_EXPECT_EQUAL( 0u, model.getRides()[1]._listIssue[0]._otherRide );
    SW_EXPECT_TRUE( model.getRides()[2]._listIssue.empty() );
    SW_EXPECT_EQUAL( 2u, model.countIssues() );
}

SW_TEST_CASE( ParkLayoutModelTest, CheckFindsEntranceInsideAndDuplicateID )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeLayout( Util::makeFlatRide( "a", "A", "0", "1", "1" ) + Util::makeFlatRide( "b", "B", "50", "-10", "1" ) + Util::makeFlatRide( "b", "B2", "100", "-10", "1" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[0]._listIssue.size() );
    SW_EXPECT_TRUE( model.getRides()[0]._listIssue[0]._kind == ParkRideIssueKind::EntranceInsideFootprint );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[1]._listIssue.size() );
    SW_EXPECT_TRUE( model.getRides()[1]._listIssue[0]._kind == ParkRideIssueKind::DuplicateID );
    SW_EXPECT_EQUAL( 2u, model.getRides()[1]._listIssue[0]._otherRide );
}

SW_TEST_CASE( ParkLayoutModelTest, RowsAreFilteredByNameOrIDAndSortedStably )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    vector<uint32> listRow;

    model.collectRows( EditorListFilter{ "" }, ParkRideSortColumn::FileOrder, false, listRow );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_EQUAL( 0u, listRow[0] );
    SW_EXPECT_EQUAL( 2u, listRow[2] );

    model.collectRows( EditorListFilter{ "merry" }, ParkRideSortColumn::FileOrder, false, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( 1u, listRow[0] );
    model.collectRows( EditorListFilter{ "cups" }, ParkRideSortColumn::FileOrder, false, listRow );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( 0u, listRow[0] );

    model.collectRows( EditorListFilter{ "" }, ParkRideSortColumn::BuildCost, false, listRow );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_EQUAL( 1u, listRow[0] ); // 1500
    SW_EXPECT_EQUAL( 0u, listRow[1] ); // 2000
    SW_EXPECT_EQUAL( 2u, listRow[2] ); // 4000
    model.collectRows( EditorListFilter{ "" }, ParkRideSortColumn::BuildCost, true, listRow );
    SW_EXPECT_EQUAL( 2u, listRow[0] );

    // 값이 같으면 파일 순서다 — 평지 둘의 정원이 같다.
    model.collectRows( EditorListFilter{ "" }, ParkRideSortColumn::Capacity, false, listRow );
    SW_EXPECT_EQUAL( 0u, listRow[0] );
    SW_EXPECT_EQUAL( 1u, listRow[1] );

    model.collectRows( EditorListFilter{ "" }, ParkRideSortColumn::Name, false, listRow );
    SW_EXPECT_EQUAL( 2u, listRow[0] ); // Little Dipper
    SW_EXPECT_EQUAL( 1u, listRow[1] ); // Merry-Go-Round
}

SW_TEST_CASE( ParkLayoutModelTest, SelectionSurvivesAReloadAndFollowsTheRide )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    model.selectRide( 99 );
    SW_EXPECT_EQUAL( ParkLayoutModel::kNoRide, model.getSelectedRide() );
    model.selectRide( 1 );
    SW_EXPECT_EQUAL( 1u, model.getSelectedRide() );

    // 앞 놀이기구가 사라져 번호가 당겨져도 같은 놀이기구를 가리킨다.
    SW_EXPECT_TRUE( Util::apply( model, Util::makeLayout( Util::makeFlatRide( "carousel", "Merry-Go-Round", "-10", "-10", "1500" ) ) ) );
    SW_EXPECT_EQUAL( 0u, model.getSelectedRide() );

    SW_EXPECT_TRUE( Util::apply( model, Util::makeLayout( Util::makeFlatRide( "cups", "Spinning Cups", "45", "-10", "2000" ) ) ) );
    SW_EXPECT_EQUAL( ParkLayoutModel::kNoRide, model.getSelectedRide() );
}

SW_TEST_CASE( ParkLayoutModelTest, NearestRideIgnoresHeight )
{
    ParkLayoutModel model;
    float32         distance{ -1.0f };
    SW_EXPECT_EQUAL( ParkLayoutModel::kNoRide, model.findNearestRide( float3{}, distance ) );
    (void)Util::apply( model, Util::makeCleanLayout() );
    SW_EXPECT_EQUAL( 1u, model.findNearestRide( float3{ -7.0f, 100.0f, 4.0f }, distance ) );
    SW_EXPECT_NEAR( 5.0f, distance, 0.001f );
}

SW_TEST_CASE( ParkLayoutModelTest, SegmentsUseTheSelectedColorAndMarkEntrancesAndGate )
{
    ParkLayoutModel model;
    (void)Util::apply( model, Util::makeLayout( Util::makeFlatRide( "a", "A", "0", "-10", "1" ) ) );
    ParkLayoutColors colors{};
    colors._gate     = float4{ 0.0f, 1.0f, 0.0f, 1.0f };
    colors._selected = float4{ 1.0f, 0.0f, 0.0f, 1.0f };

    vector<EditorWorldSegment> listSegment;
    model.appendSegments( colors, listSegment );
    SW_REQUIRE_EQUAL( size_t{ 12 + 1 + 3 }, listSegment.size() ); // 상자 12, 줄 입구 1, 정문 3
    SW_EXPECT_EQUAL( 1.0f, listSegment[0]._color._y );            // 고르지 않은 놀이기구는 자기 색(흰색)이다
    SW_EXPECT_EQUAL( -10.0f, listSegment[12]._from._z );          // 줄 입구
    SW_EXPECT_EQUAL( 1.0f, listSegment.back()._color._y );

    model.selectRide( 0 );
    listSegment.clear();
    model.appendSegments( colors, listSegment );
    SW_EXPECT_EQUAL( 0.0f, listSegment[0]._color._y );
}
