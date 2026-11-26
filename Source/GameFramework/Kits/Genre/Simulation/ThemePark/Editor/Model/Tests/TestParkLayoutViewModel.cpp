#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/Model/ParkLayoutViewModel.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/Tests/ThemeParkTestRules.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ParkLayoutViewModelTestUtil
    {
        /** @brief 실제 데이터(`coasters.xml` 의 family)와 같은 회로입니다 — 끝이 스테이션 뒤에 와서 닫는 곡선이 접히지 않는다(접히면 빌더가 경고한다). */
        static constexpr const utf8* kCoasters = R"(<CoasterLayoutCatalog>
  <Physics gravity="9.81" rollingResistance="0.012" dragCoefficient="0.0004" liftSpeed="4" stationSpeed="3" boosterSpeed="22" boosterAcceleration="9" brakeSpeed="5" brakeDeceleration="10" fixedStep="0.00416667" stallSpeed="0.3" stallSeconds="3" gForceWarmupSeconds="0.5"/>
  <Rating maxRating="10" intensityPerPositiveG="0.9" intensityPerNegativeG="1.5" intensityPerLateralG="1.2" intensitySpeedScale="15" nauseaPerLateralG="1.5" nauseaPerInversion="1" nauseaPerAirtime="0.3" nauseaPerIntensity="0.25" excitementSpeedScale="10" excitementPerAirtime="0.6" excitementPerInversion="0.8" excitementPerDrop="0.3" excitementDropHeightScale="15" excitementLengthScale="400" excitementIntensityLimit="8"/>
  <Layout id="family" name="Little Dipper" startHeight="1">
    <Piece type="Station" length="10"/>
    <Piece type="LiftHill" length="16" height="8"/>
    <Piece type="Drop" length="16" height="8"/>
    <Piece type="TurnLeft" radius="10" angle="180" bank="20"/>
    <Piece type="Straight" length="54"/>
    <Piece type="TurnLeft" radius="10" angle="180" bank="15"/>
    <Piece type="Brakes" length="8"/>
  </Layout>
</CoasterLayoutCatalog>)";

        static constexpr const utf8* kCoaster = R"(<Coaster layout="family" x="90" z="0" heading="0" color="0.3,0.75,0.4,1" capacity="16" loadTime="15" cost="4000"/>)";

        static string makeFlatRide( const utf8* pID, const utf8* pName, const utf8* pX, const utf8* pEntranceZ, const utf8* pCost )
        {
            StringBuilder<constant::buffer::kMax512> text;
            text.appendFormat( "<FlatRide id=\"%#\" name=\"%#\" excitement=\"2\" intensity=\"1\" nausea=\"1\" cycleTime=\"40\" capacity=\"16\" price=\"3\" runningCost=\"3\" cost=\"%#\" "
                               "x=\"%#\" z=\"0\" entranceX=\"%#\" entranceZ=\"%#\" shape=\"Cylinder\" sizeX=\"6\" sizeY=\"2\" sizeZ=\"6\" color=\"1,1,1,1\"/>",
                               pID, pName, pCost, pX, pX, pEntranceZ );
            return string( text.view() );
        }

        static string makeLayout( std::initializer_list<string_view> listBody )
        {
            StringBuilder<constant::buffer::kMax8192> text;
            text.appendFormat( "<ParkLayout gateX=\"20\" gateZ=\"-40\" startingCash=\"14000\" entryFee=\"5\">%#", kParkRules );
            for ( const string_view body : listBody )
            {
                text.append( body );
            }
            text.append( "</ParkLayout>" );
            return string( text.view() );
        }

        /** @brief 평지 둘(서로 먼 자리, 입구는 영역 밖)과 코스터 하나입니다. */
        static string makeCleanLayout()
        {
            return makeLayout( { makeFlatRide( "cups", "Spinning Cups", "45", "-10", "2000" ), makeFlatRide( "carousel", "Merry-Go-Round", "-10", "-10", "1500" ), kCoaster } );
        }

        [[nodiscard]] static bool apply( ParkLayoutViewModel& model, const string& layoutText, const utf8* pCoasters = kCoasters, bool bForce = false )
        {
            return model.applyTexts( layoutText, pCoasters, "test/rides.xml", "test/coasters.xml", bForce );
        }
    };
    using Util = ParkLayoutViewModelTestUtil;
} // namespace

SW_TEST_CASE( ParkLayoutViewModelTest, ReadsRidesGateAndTotals )
{
    ParkLayoutViewModel model;
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
SW_TEST_CASE( ParkLayoutViewModelTest, TimeIsTheCycleForFlatRidesAndTheLoadForCoasters )
{
    ParkLayoutViewModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_EXPECT_EQUAL( 40.0f, model.getRides()[0]._timeSeconds );
    SW_EXPECT_EQUAL( 3, model.getRides()[0]._price );
    SW_EXPECT_FALSE( model.getRides()[0]._bCoaster );
    SW_EXPECT_EQUAL( 15.0f, model.getRides()[2]._timeSeconds );
    SW_EXPECT_TRUE( model.getRides()[2]._bCoaster );
}

SW_TEST_CASE( ParkLayoutViewModelTest, SameTextIsNotReadAgainUnlessForced )
{
    ParkLayoutViewModel model;
    const string        layout = Util::makeCleanLayout();
    SW_EXPECT_TRUE( Util::apply( model, layout ) );
    SW_EXPECT_FALSE( Util::apply( model, layout ) );
    SW_EXPECT_TRUE( Util::apply( model, layout, Util::kCoasters, true ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
}

// 예전에는 배치 글만 비교해 코스터 파일을 고쳐도 트랙이 그대로였다.
SW_TEST_CASE( ParkLayoutViewModelTest, ChangingOnlyTheCoasterFileReloads )
{
    ParkLayoutViewModel model;
    const string        layout = Util::makeCleanLayout();
    SW_EXPECT_TRUE( Util::apply( model, layout ) );
    string renamed{ Util::kCoasters };
    renamed.replace( renamed.find( "Little Dipper" ), 13, "Big Dipper" );
    SW_EXPECT_TRUE( Util::apply( model, layout, renamed.c_str() ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_EXPECT_TRUE( model.getRides()[2]._name == "Big Dipper" );
}

// 예전에는 틀린 파일을 프레임마다 다시 읽고 경고했다 — 같은 글이면 다시 읽지 않고, 글이 고쳐지면 읽힌다.
SW_TEST_CASE( ParkLayoutViewModelTest, BrokenFileIsReadOnceAndRecoversWhenFixed )
{
    SW_EXPECT_LOG( "ParkLayout.CoasterLayoutNotCoaster" );
    SW_EXPECT_LOG( "ParkLayoutViewModel.DidNotLoadThrough" );
    ParkLayoutViewModel model;
    const string        broken = Util::makeLayout( { R"(<Coaster layout="nope" x="0" z="0" color="1,1,1,1" capacity="16" loadTime="15" cost="4000"/>)" } );
    SW_EXPECT_TRUE( Util::apply( model, broken ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Failed );
    SW_EXPECT_TRUE( model.getRides().empty() );
    SW_EXPECT_FALSE( Util::apply( model, broken ) );

    SW_EXPECT_TRUE( Util::apply( model, Util::makeCleanLayout() ) );
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
}

SW_TEST_CASE( ParkLayoutViewModelTest, MissingAndFailedClearThePreviewAndAllowARereadOfTheSameText )
{
    ParkLayoutViewModel model;
    const string        layout = Util::makeCleanLayout();
    (void)Util::apply( model, layout );
    model.selectRide( 1 );
    model.setMissing();
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Missing );
    SW_EXPECT_TRUE( model.getRides().empty() );
    SW_EXPECT_EQUAL( ParkLayoutViewModel::kNoRide, model.getSelectedRide() );
    SW_EXPECT_TRUE( Util::apply( model, layout ) ); // 파일이 돌아왔다
    model.setFailed();
    SW_EXPECT_TRUE( Util::apply( model, layout ) ); // 읽기 실패 뒤에 같은 글이 읽혔다
    SW_EXPECT_TRUE( model.getState() == ParkLayoutState::Loaded );
}

// 예전에는 8 점마다 하나만 남겨 회로 끝에 틈이 보였다.
SW_TEST_CASE( ParkLayoutViewModelTest, CoasterTrackIsDrawnAsAClosedCircuit )
{
    ParkLayoutViewModel model;
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
SW_TEST_CASE( ParkLayoutViewModelTest, FocusHoldsTheWholeRide )
{
    ParkLayoutViewModel model;
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

SW_TEST_CASE( ParkLayoutViewModelTest, CheckFindsOverlapsOnBothRides )
{
    ParkLayoutViewModel model;
    (void)Util::apply( model, Util::makeLayout( { Util::makeFlatRide( "a", "A", "0", "-10", "1" ), Util::makeFlatRide( "b", "B", "5", "-10", "1" ), Util::makeFlatRide( "c", "C", "50", "-10", "1" ) } ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[0]._listIssue.size() );
    SW_EXPECT_TRUE( model.getRides()[0]._listIssue[0]._kind == ParkRideIssueKind::Overlap );
    SW_EXPECT_EQUAL( 1u, model.getRides()[0]._listIssue[0]._otherRide );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[1]._listIssue.size() );
    SW_EXPECT_EQUAL( 0u, model.getRides()[1]._listIssue[0]._otherRide );
    SW_EXPECT_TRUE( model.getRides()[2]._listIssue.empty() );
    SW_EXPECT_EQUAL( 2u, model.countIssues() );
}

SW_TEST_CASE( ParkLayoutViewModelTest, CheckFindsEntranceInsideAndDuplicateID )
{
    ParkLayoutViewModel model;
    (void)Util::apply( model, Util::makeLayout( { Util::makeFlatRide( "a", "A", "0", "1", "1" ), Util::makeFlatRide( "b", "B", "50", "-10", "1" ), Util::makeFlatRide( "b", "B2", "100", "-10", "1" ) } ) );
    SW_REQUIRE_EQUAL( size_t{ 3 }, model.getRides().size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[0]._listIssue.size() );
    SW_EXPECT_TRUE( model.getRides()[0]._listIssue[0]._kind == ParkRideIssueKind::EntranceInsideFootprint );
    SW_REQUIRE_EQUAL( size_t{ 1 }, model.getRides()[1]._listIssue.size() );
    SW_EXPECT_TRUE( model.getRides()[1]._listIssue[0]._kind == ParkRideIssueKind::DuplicateID );
    SW_EXPECT_EQUAL( 2u, model.getRides()[1]._listIssue[0]._otherRide );
}

SW_TEST_CASE( ParkLayoutViewModelTest, RowsAreFilteredByNameOrIDAndSortedStably )
{
    ParkLayoutViewModel model;
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

SW_TEST_CASE( ParkLayoutViewModelTest, SelectionSurvivesAReloadAndFollowsTheRide )
{
    ParkLayoutViewModel model;
    (void)Util::apply( model, Util::makeCleanLayout() );
    model.selectRide( 99 );
    SW_EXPECT_EQUAL( ParkLayoutViewModel::kNoRide, model.getSelectedRide() );
    model.selectRide( 1 );
    SW_EXPECT_EQUAL( 1u, model.getSelectedRide() );

    // 앞 놀이기구가 사라져 번호가 당겨져도 같은 놀이기구를 가리킨다.
    SW_EXPECT_TRUE( Util::apply( model, Util::makeLayout( { Util::makeFlatRide( "carousel", "Merry-Go-Round", "-10", "-10", "1500" ) } ) ) );
    SW_EXPECT_EQUAL( 0u, model.getSelectedRide() );

    SW_EXPECT_TRUE( Util::apply( model, Util::makeLayout( { Util::makeFlatRide( "cups", "Spinning Cups", "45", "-10", "2000" ) } ) ) );
    SW_EXPECT_EQUAL( ParkLayoutViewModel::kNoRide, model.getSelectedRide() );
}

SW_TEST_CASE( ParkLayoutViewModelTest, NearestRideIgnoresHeight )
{
    ParkLayoutViewModel model;
    float32             distance{ -1.0f };
    SW_EXPECT_EQUAL( ParkLayoutViewModel::kNoRide, model.findNearestRide( float3{}, distance ) );
    (void)Util::apply( model, Util::makeCleanLayout() );
    SW_EXPECT_EQUAL( 1u, model.findNearestRide( float3{ -7.0f, 100.0f, 4.0f }, distance ) );
    SW_EXPECT_NEAR( 5.0f, distance, 0.001f );
}

SW_TEST_CASE( ParkLayoutViewModelTest, SegmentsUseTheSelectedColorAndMarkEntrancesAndGate )
{
    ParkLayoutViewModel model;
    (void)Util::apply( model, Util::makeLayout( { Util::makeFlatRide( "a", "A", "0", "-10", "1" ) } ) );
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

/** @brief [ParkLayoutViewModelTest] 바닥 영역 겹침은 게임의 땅 얻기와 같은 사각형이다 — 둥근 놀이기구 둘이 모서리만 닿아도(원으로는 떨어져 있어도) 겹친다 */
SW_TEST_CASE( ParkLayoutViewModelTest, FootprintOverlapMatchesTheLandClaimRectangle )
{
    ParkRidePreview roundRide;
    roundRide._position = float3{ 0.0f, 0.0f, 0.0f };
    roundRide._size     = float3{ 10.0f, 4.0f, 10.0f };
    ParkRidePreview otherRide;
    otherRide._position = float3{ 9.0f, 0.0f, 9.0f }; // 가운데 거리 12.7 > 반지름 합 10 — 원이면 떨어져 있다
    otherRide._size     = float3{ 10.0f, 4.0f, 10.0f };
    SW_EXPECT_TRUE( roundRide.overlapsFootprint( otherRide ) );
    otherRide._position = float3{ 10.5f, 0.0f, 0.0f };
    SW_EXPECT_FALSE( roundRide.overlapsFootprint( otherRide ) );
}
