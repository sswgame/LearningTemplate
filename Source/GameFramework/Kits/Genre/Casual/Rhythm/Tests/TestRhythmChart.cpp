#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmChart.h"

// RhythmChartTest — 리듬 채보 읽기: 박과 초의 변환(BPM 변화 · 정지 · 오프셋), 변화가 놓인 박의 BPM, 노트 정렬 · 시각 · 판정 수,
// 스크롤 위치(시간 기반 · 박 기반), 변박 마디선, 겹치는 노트 · 틀린 노트 · 모르는 원소 · 빈 채보 거절. 키트 GF_Rhythm(nogpu).

namespace
{
    // 박 0..4 는 120 BPM(박당 0.5 초), 박 4 에서 1 초 정지, 박 8 부터 240 BPM(박당 0.25 초), 오프셋 0.5 초.
    // 시각 점: 박 0 = 0.5 초, 박 4 = 2.5 초(정지 끝 3.5 초), 박 8 = 5.5 초. 값이 모두 2 의 거듭제곱 분수라 float 로도 정확하다.
    constexpr const utf8* kChartText = R"(<Chart title="Song" artist="Band" level="3" lanes="4" offset="0.5">
        <Bpm beat="0" bpm="120"/>
        <Bpm beat="8" bpm="240"/>
        <Stop beat="4" seconds="1"/>
        <Measure beat="8" beatsPerMeasure="3"/>
        <Note lane="1" beat="4"/>
        <Note lane="0" beat="4"/>
        <Note lane="2" beat="10" endBeat="12"/>
        <Note lane="3" beat="2"/>
    </Chart>)";

    constexpr float32 kTolerance = 1.0e-5f;
} // namespace

/**
 * @brief [RhythmChartTest] 노트는 박 · 레인 순이고, 정지 박 위 노트는 정지가 시작될 때 치며, 롱노트는 판정 둘이다
 */
SW_TEST_CASE( RhythmChartTest, NotesAreSortedTimedAndCounted )
{
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( kChartText, "test" ) );
    SW_EXPECT_EQUAL( sw::string( "Song" ), chart.getTitle() );
    SW_EXPECT_EQUAL( sw::string( "Band" ), chart.getArtist() );
    SW_EXPECT_EQUAL( 3, chart.getLevel() );
    SW_EXPECT_EQUAL( 4, chart.getLaneCount() );

    const sw::vector<sw::RhythmNote>& listNote = chart.getNotes();
    SW_REQUIRE_EQUAL( size_t( 4 ), listNote.size() );
    SW_EXPECT_EQUAL( 3, listNote[0]._lane );
    SW_EXPECT_NEAR( 1.5f, listNote[0]._time, kTolerance );
    SW_EXPECT_EQUAL( 0, listNote[1]._lane );
    SW_EXPECT_NEAR( 2.5f, listNote[1]._time, kTolerance ); // 정지(2.5 ~ 3.5 초)가 시작될 때
    SW_EXPECT_EQUAL( 1, listNote[2]._lane );
    SW_EXPECT_NEAR( 2.5f, listNote[2]._time, kTolerance );
    SW_EXPECT_EQUAL( 2, listNote[3]._lane );
    SW_EXPECT_TRUE( listNote[3]._bLong == SW_TRUE );
    SW_EXPECT_NEAR( 6.0f, listNote[3]._time, kTolerance );
    SW_EXPECT_NEAR( 6.5f, listNote[3]._endTime, kTolerance );
    SW_EXPECT_TRUE( listNote[0]._bLong == SW_FALSE );
    SW_EXPECT_EQUAL( 5, chart.getJudgmentCount() );
    SW_EXPECT_NEAR( 6.5f, chart.getEndTime(), kTolerance );
}

/**
 * @brief [RhythmChartTest] 초 → 박은 정지 동안 그 박에 머물고 박 0 앞은 첫 BPM 으로 늘린다. 변화가 놓인 박의 BPM 은 새 값이다
 */
SW_TEST_CASE( RhythmChartTest, ConvertsAcrossStopsAndBpmChanges )
{
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( kChartText, "test" ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), chart.getTimingPoints().size() );

    SW_EXPECT_NEAR( -1.0f, chart.convertSecondsToBeat( 0.0f ), kTolerance );
    SW_EXPECT_NEAR( 2.0f, chart.convertSecondsToBeat( 1.5f ), kTolerance );
    SW_EXPECT_NEAR( 4.0f, chart.convertSecondsToBeat( 3.0f ), kTolerance ); // 정지 중
    SW_EXPECT_NEAR( 5.0f, chart.convertSecondsToBeat( 4.0f ), kTolerance );
    SW_EXPECT_NEAR( 10.0f, chart.convertSecondsToBeat( 6.0f ), kTolerance );

    SW_EXPECT_NEAR( 0.0f, chart.convertBeatToSeconds( -1.0f ), kTolerance );
    SW_EXPECT_NEAR( 4.5f, chart.convertBeatToSeconds( 6.0f ), kTolerance );
    SW_EXPECT_NEAR( 5.5f, chart.convertBeatToSeconds( 8.0f ), kTolerance );

    SW_EXPECT_NEAR( 120.0f, chart.findBpmAt( 0.0f ), kTolerance );
    SW_EXPECT_NEAR( 120.0f, chart.findBpmAt( 7.5f ), kTolerance );
    SW_EXPECT_NEAR( 240.0f, chart.findBpmAt( 8.0f ), kTolerance );
    SW_EXPECT_NEAR( 240.0f, chart.findBpmAt( 9.0f ), kTolerance );
    SW_EXPECT_NEAR( 120.0f, chart.getBaseBpm(), kTolerance );
}

/**
 * @brief [RhythmChartTest] 시간 기반 스크롤은 남은 초 x hiSpeed, 박 기반은 남은 박 x 기준 BPM 박 길이 x hiSpeed(정지 중이면 정지 박에서 잰다)
 */
SW_TEST_CASE( RhythmChartTest, NoteYFollowsScrollMode )
{
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( kChartText, "test" ) );
    SW_EXPECT_NEAR( 1.0f, chart.computeNoteY( 10.0f, 5.5f, 2.0f, sw::RhythmScrollMode::ConstantSpeed ), kTolerance );
    SW_EXPECT_NEAR( 2.0f, chart.computeNoteY( 10.0f, 5.5f, 2.0f, sw::RhythmScrollMode::FollowBpm ), kTolerance );
    // 정지 중(3 초)에는 박 기반 위치가 박 4 에서 멈춘다: (6 - 4) x 0.5 x 1
    SW_EXPECT_NEAR( 1.0f, chart.computeNoteY( 6.0f, 3.0f, 1.0f, sw::RhythmScrollMode::FollowBpm ), kTolerance );
}

/**
 * @brief [RhythmChartTest] 마디선은 4 박마다이고 변박 박에서 새 마디가 시작된다
 */
SW_TEST_CASE( RhythmChartTest, MeasureLinesFollowMeasureChanges )
{
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( kChartText, "test" ) );
    sw::vector<float32> listBeat;
    chart.fillMeasureLineBeats( 14.0f, listBeat );
    const float32 arrExpected[] = { 0.0f, 4.0f, 8.0f, 11.0f, 14.0f };
    SW_REQUIRE_EQUAL( sizeof( arrExpected ) / sizeof( arrExpected[0] ), listBeat.size() );
    for ( size_t index = 0; index < listBeat.size(); ++index )
    {
        SW_EXPECT_NEAR( arrExpected[index], listBeat[index], kTolerance );
    }
}

/**
 * @brief [RhythmChartTest] `<Bpm>` 이 없으면 루트 bpm(기본 120)이 끝까지의 빠르기다
 */
SW_TEST_CASE( RhythmChartTest, RootBpmIsUsedWithoutBpmElements )
{
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( R"(<Chart lanes="1" bpm="60"><Note lane="0" beat="3"/></Chart>)", "test" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), chart.getNotes().size() );
    SW_EXPECT_NEAR( 3.0f, chart.getNotes()[0]._time, kTolerance );
    SW_EXPECT_NEAR( 60.0f, chart.getBaseBpm(), kTolerance );
}

/**
 * @brief [RhythmChartTest] 같은 레인에서 앞 롱노트가 끝나기 전에 시작하는 노트는 버리고 채보를 읽지 못한 것으로 본다
 */
SW_TEST_CASE( RhythmChartTest, OverlappingNoteFailsTheChart )
{
    sw::RhythmChart chart;
    SW_EXPECT_FALSE( chart.loadFromXMLText(
        R"(<Chart lanes="2"><Note lane="0" beat="1" endBeat="3"/><Note lane="0" beat="2"/><Note lane="1" beat="2"/></Chart>)", "test" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), chart.getNotes().size() );
    SW_EXPECT_LOG( "RhythmChart.NoteOverlap" );
}

/**
 * @brief [RhythmChartTest] 레인 밖 노트 · 끝이 앞선 롱노트 · 모르는 원소는 채보를 실패로 만들고, 노트가 하나도 없으면 실패다
 */
SW_TEST_CASE( RhythmChartTest, InvalidElementsAndEmptyChartFail )
{
    sw::RhythmChart chart;
    SW_EXPECT_FALSE( chart.loadFromXMLText(
        R"(<Chart lanes="2"><Note lane="5" beat="1"/><Note lane="0" beat="2" endBeat="1"/><Foo/><Note lane="1" beat="3"/></Chart>)", "test" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), chart.getNotes().size() );
    SW_EXPECT_LOG( "RhythmChart.InvalidNote" );
    SW_EXPECT_LOG( "RhythmChart.UnknownElement" );

    sw::RhythmChart emptyChart;
    SW_EXPECT_FALSE( emptyChart.loadFromXMLText( "<Chart/>", "test" ) );
    SW_EXPECT_LOG( "RhythmChart.NoNotes" );
}
