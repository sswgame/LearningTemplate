// 건반 리듬 키트(오투잼 · 비트매니아) — 변속 · 정지의 박 ↔ 초 왕복과 변박 마디선, 판정 창 · 너무 이른 누름 무시 · 놓침, 롱노트 끝까지 누름 · 일찍 떼기,
// 콤보 끊김 · 최대 콤보 · 콤보 보너스 점수 · 정확도 · 등급 · 리플레이, 라이프 0 실패, 오토플레이, 시간 기반 · BPM 따라 스크롤.
#include "pch.h"

#include "GameFramework/Base/TimingJudge.h"
#include "GameFramework/Kits/Rhythm/RhythmChart.h"
#include "GameFramework/Kits/Rhythm/RhythmPlaySession.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kRhythmJudgeXml = R"(<TimingWindows>
        <Window grade="Cool" width="0.03" score="300"/>
        <Window grade="Good" width="0.07" score="200"/>
        <Window grade="Bad" width="0.12" score="50" breaksCombo="true"/>
      </TimingWindows>)";

    /** @brief 120 BPM · 오프셋 0 · 레인 4 의 채보 글입니다. */
    string makeSimpleChartXml( const utf8* pNoteXml )
    {
        string text = R"(<Chart title="Test" artist="Tester" level="3" lanes="4" offset="0"><Bpm beat="0" bpm="120"/>)";
        text += pNoteXml;
        text += "</Chart>";
        return text;
    }

    int32 countEvents( const vector<RhythmEvent>& listEvent, RhythmEvent::Kind kind )
    {
        int32 count = 0;
        for ( const RhythmEvent& event : listEvent )
        {
            if ( event._kind == kind )
                ++count;
        }
        return count;
    }
} // namespace

SW_TEST_CASE( RhythmTest, BeatSecondsRoundTripAcrossBpmChangesStopsAndMeasures )
{
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText( R"(<Chart title="Shift" lanes="7" offset="0.05">
        <Bpm beat="0" bpm="120"/><Bpm beat="8" bpm="240"/><Stop beat="12" seconds="0.5"/><Bpm beat="16" bpm="60"/>
        <Measure beat="4" beatsPerMeasure="3"/>
        <Note lane="0" beat="12"/><Note lane="1" beat="13"/><Note lane="9" beat="1"/>
      </Chart>)",
                                           "RhythmTest" ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( chart.getNotes().size() ) ); // 레인 9 는 버린다

    SW_EXPECT_NEAR_EQUAL( 0.05f, chart.convertBeatToSeconds( 0.0f ), 1.0e-5f ); // 오프셋 = 박 0 의 시각
    SW_EXPECT_NEAR_EQUAL( 2.05f, chart.convertBeatToSeconds( 4.0f ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 4.05f, chart.convertBeatToSeconds( 8.0f ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 5.05f, chart.convertBeatToSeconds( 12.0f ), 1.0e-5f ); // 정지 박의 노트는 정지가 시작될 때
    SW_EXPECT_NEAR_EQUAL( 5.80f, chart.convertBeatToSeconds( 13.0f ), 1.0e-5f ); // 정지 0.5 초 뒤 240 BPM
    SW_EXPECT_NEAR_EQUAL( 6.55f, chart.convertBeatToSeconds( 16.0f ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 8.55f, chart.convertBeatToSeconds( 18.0f ), 1.0e-5f );  // 60 BPM
    SW_EXPECT_NEAR_EQUAL( -0.95f, chart.convertBeatToSeconds( -2.0f ), 1.0e-5f ); // 앞은 첫 BPM 으로 늘인다
    SW_EXPECT_NEAR_EQUAL( 5.05f, chart.getNotes()[0]._time, 1.0e-5f );

    // 정지 동안 박은 멈춘다.
    SW_EXPECT_NEAR_EQUAL( 12.0f, chart.convertSecondsToBeat( 5.3f ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 12.0f, chart.convertSecondsToBeat( 5.55f ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 13.0f, chart.convertSecondsToBeat( 5.80f ), 1.0e-4f );

    const float32 arrBeat[] = { -1.0f, 0.0f, 1.5f, 7.75f, 8.0f, 9.25f, 11.0f, 13.5f, 16.0f, 17.25f, 30.0f };
    for ( const float32 beat : arrBeat )
        SW_EXPECT_NEAR_EQUAL( beat, chart.convertSecondsToBeat( chart.convertBeatToSeconds( beat ) ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 240.0f, chart.findBpmAt( 9.0f ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, chart.findBpmAt( 20.0f ), 1.0e-3f );

    // 변박 — 박 4 부터 3 박 마디.
    vector<float32> listLineBeat;
    chart.fillMeasureLineBeats( 13.0f, listLineBeat );
    SW_ASSERT_TRUE( listLineBeat.size() == 5 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listLineBeat[0], 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, listLineBeat[1], 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, listLineBeat[2], 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 13.0f, listLineBeat[4], 1.0e-5f );
}

SW_TEST_CASE( RhythmTest, JudgesByWindowIgnoresTooEarlyPressAndMissesPassedNotes )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kRhythmJudgeXml, "RhythmTest" ) );
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText( makeSimpleChartXml( R"(<Note lane="0" beat="2"/><Note lane="0" beat="4"/><Note lane="1" beat="4"/><Note lane="2" beat="6"/>)" ),
                                           "RhythmTest" ) );
    RhythmPlaySession session;
    session.initialize( &chart, &judge, RhythmPlaySettings{} );

    session.press( 0, 0.5f ); // 1.0 초 노트보다 0.5 초 이르다 — 가장 넓은 이른 폭(0.12) 밖이라 무시
    session.press( 3, 0.6f ); // 노트 없는 레인
    vector<RhythmEvent> listEvent;
    session.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, countEvents( listEvent, RhythmEvent::Kind::EmptyPress ) );
    SW_EXPECT_EQUAL( 0, session.getJudgedCount() );

    session.press( 0, 0.98f ); // 이 누름이 앞의 이른 누름 때문에 둘째 노트로 밀리지 않는다
    session.press( 0, 1.94f ); // −0.06 → Good
    session.press( 1, 2.10f ); // +0.10 → Bad, 콤보 끊김
    SW_EXPECT_EQUAL( 0, session.getCombo() );
    session.update( 3.10f ); // 3.0 초 노트는 아직 Bad 창 안
    SW_EXPECT_EQUAL( 0, session.getMissCount() );
    session.update( 3.20f );
    SW_EXPECT_EQUAL( 1, session.getMissCount() );

    SW_EXPECT_EQUAL( 1, session.findGradeCount( hashed_string( "Cool" ) ) );
    SW_EXPECT_EQUAL( 1, session.findGradeCount( hashed_string( "Good" ) ) );
    SW_EXPECT_EQUAL( 1, session.findGradeCount( hashed_string( "Bad" ) ) );
    SW_EXPECT_EQUAL( 2, session.getMaxCombo() );
    SW_EXPECT_EQUAL( 550, static_cast<int32>( session.getScore() ) );
    SW_EXPECT_TRUE( session.getState() == RhythmPlayState::Cleared );

    listEvent.clear();
    session.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.size() >= 2 );
    SW_EXPECT_TRUE( listEvent[0]._grade == hashed_string( "Cool" ) );
    SW_EXPECT_NEAR_EQUAL( -0.02f, listEvent[0]._offset, 1.0e-4f );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, RhythmEvent::Kind::Cleared ) );
}

SW_TEST_CASE( RhythmTest, LongNoteNeedsHoldingToTheEndAndEarlyReleaseIsJudged )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kRhythmJudgeXml, "RhythmTest" ) );
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText(
        makeSimpleChartXml( R"(<Note lane="0" beat="2" endBeat="6"/><Note lane="1" beat="2" endBeat="6"/><Note lane="2" beat="2" endBeat="6"/><Note lane="3" beat="2" endBeat="6"/>)" ),
        "RhythmTest" ) );
    SW_EXPECT_EQUAL( 8, chart.getJudgmentCount() ); // 머리 + 끝
    RhythmPlaySession session;
    session.initialize( &chart, &judge, RhythmPlaySettings{} );

    session.press( 0, 1.0f );
    session.press( 1, 1.0f );
    session.press( 2, 1.0f );
    SW_EXPECT_TRUE( session.isHolding( 0 ) );
    session.release( 1, 2.0f ); // 끝(3.0)보다 1 초 이르다 — 끝 Miss. 그 전에 레인 3 은 머리를 놓쳐 머리 · 끝 모두 Miss
    SW_EXPECT_FALSE( session.isHolding( 1 ) );
    SW_EXPECT_EQUAL( 3, session.getMissCount() );
    session.release( 2, 2.95f ); // 끝보다 0.05 이르다 — 떼기 판정 Good
    SW_EXPECT_EQUAL( 1, session.findGradeCount( hashed_string( "Good" ) ) );
    SW_EXPECT_TRUE( session.isHolding( 0 ) );
    session.update( 3.5f ); // 레인 0 은 끝까지 눌렀다 — Cool
    SW_EXPECT_FALSE( session.isHolding( 0 ) );
    session.release( 0, 3.6f ); // 이미 끝난 롱노트 — 판정 없음

    SW_EXPECT_EQUAL( 4, session.findGradeCount( hashed_string( "Cool" ) ) ); // 머리 셋 + 레인 0 끝
    SW_EXPECT_EQUAL( 3, session.getMissCount() );
    SW_EXPECT_EQUAL( 8, session.getJudgedCount() );
    SW_EXPECT_EQUAL( 3, session.getMaxCombo() );
    SW_EXPECT_TRUE( session.getState() == RhythmPlayState::Cleared );
}

SW_TEST_CASE( RhythmTest, ComboBreaksOnBadAndScoreAccuracyRankAndReplayAreDeterministic )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kRhythmJudgeXml, "RhythmTest" ) );
    string noteXml;
    for ( int32 beat = 1; beat <= 10; ++beat )
        noteXml += "<Note lane=\"0\" beat=\"" + std::to_string( beat ) + "\"/>";
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText( makeSimpleChartXml( noteXml.c_str() ), "RhythmTest" ) );

    RhythmPlaySettings settings;
    settings._comboBonusPerCombo = 0.01f;
    RhythmPlaySession session;
    session.initialize( &chart, &judge, settings );
    for ( int32 beat = 1; beat <= 10; ++beat )
    {
        const float32 target = static_cast<float32>( beat ) * 0.5f;
        session.press( 0, beat == 5 ? target + 0.1f : target ); // 다섯째만 Bad
        session.release( 0, target + 0.15f );                   // 보통 노트의 떼기는 판정이 없다
    }
    SW_EXPECT_EQUAL( 9, session.findGradeCount( hashed_string( "Cool" ) ) );
    SW_EXPECT_EQUAL( 5, session.getMaxCombo() ); // Bad 가 콤보를 끊지 않으면 10
    SW_EXPECT_EQUAL( 5, session.getCombo() );
    // Cool 300 + 300 × 콤보 × 0.01: (303 + 306 + 309 + 312) + Bad 50 + (303 + 306 + 309 + 312 + 315)
    SW_EXPECT_EQUAL( 2825, static_cast<int32>( session.getScore() ) );
    SW_EXPECT_NEAR_EQUAL( ( 9.0f + 50.0f / 300.0f ) * 10.0f, session.computeAccuracy(), 1.0e-3f );
    SW_EXPECT_TRUE( session.computeRank() == hashed_string( "A" ) );

    // 등급 표를 바꾸면 같은 정확도가 다른 등급이다.
    RhythmPlaySettings strictSettings = settings;
    strictSettings._listRankThreshold = {
        {hashed_string( "AAA" ), 99.0f},
        { hashed_string( "AA" ), 92.0f},
        {  hashed_string( "B" ),  0.0f}
    };

    // 기록한 입력을 다시 넣으면 같은 판이다(리플레이).
    RhythmPlaySession replay;
    replay.initialize( &chart, &judge, strictSettings );
    for ( const RhythmInputRecord& record : session.getInputRecords() )
    {
        if ( record._bPress == SW_TRUE )
            replay.press( record._lane, record._time );
        else
            replay.release( record._lane, record._time );
    }
    SW_EXPECT_EQUAL( static_cast<int32>( session.getScore() ), static_cast<int32>( replay.getScore() ) );
    SW_EXPECT_EQUAL( session.getMaxCombo(), replay.getMaxCombo() );
    SW_EXPECT_TRUE( replay.computeRank() == hashed_string( "B" ) );
}

SW_TEST_CASE( RhythmTest, LifeReachingZeroFailsAndStopsJudging )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kRhythmJudgeXml, "RhythmTest" ) );
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText(
        makeSimpleChartXml( R"(<Note lane="0" beat="2"/><Note lane="0" beat="4"/><Note lane="0" beat="6"/><Note lane="0" beat="8"/><Note lane="0" beat="10"/>)" ),
        "RhythmTest" ) );
    RhythmPlaySettings settings;
    settings._initialLife   = 50.0f;
    settings._missLifeDelta = -40.0f;
    settings._listGradeRule = {
        { hashed_string( "Cool" ), 10.0f, 1.0f }
    };
    RhythmPlaySession session;
    session.initialize( &chart, &judge, settings );

    session.press( 0, 1.0f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, session.getLife(), 1.0e-4f );
    session.update( 2.5f ); // 2.0 초 노트 Miss → 20
    SW_EXPECT_NEAR_EQUAL( 20.0f, session.getLife(), 1.0e-4f );
    SW_EXPECT_TRUE( session.getState() == RhythmPlayState::Playing );
    session.update( 10.0f ); // 3.0 초 노트 Miss → 0 — 실패, 뒤 노트는 판정하지 않는다
    SW_EXPECT_TRUE( session.getState() == RhythmPlayState::Failed );
    SW_EXPECT_NEAR_EQUAL( 0.0f, session.getLife(), 1.0e-4f );
    SW_EXPECT_EQUAL( 3, session.getJudgedCount() );
    session.press( 0, 4.0f );
    SW_EXPECT_EQUAL( 3, session.getJudgedCount() );
    SW_EXPECT_TRUE( session.computeRank() == hashed_string( "F" ) );

    vector<RhythmEvent> listEvent;
    session.drainEvents( listEvent );
    SW_ASSERT_TRUE( listEvent.empty() == false );
    SW_EXPECT_TRUE( listEvent.back()._kind == RhythmEvent::Kind::Failed );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, RhythmEvent::Kind::Cleared ) );
}

SW_TEST_CASE( RhythmTest, AutoPlayHitsEveryNoteWithTheBestGrade )
{
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kRhythmJudgeXml, "RhythmTest" ) );
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText( R"(<Chart lanes="7" offset="0.1">
        <Bpm beat="0" bpm="150"/><Bpm beat="8" bpm="300"/><Stop beat="10" seconds="0.4"/>
        <Note lane="0" beat="1"/><Note lane="1" beat="1"/><Note lane="2" beat="2" endBeat="5"/><Note lane="3" beat="3.5"/>
        <Note lane="2" beat="6"/><Note lane="4" beat="10"/><Note lane="5" beat="10.25"/><Note lane="6" beat="9" endBeat="12"/>
      </Chart>)",
                                           "RhythmTest" ) );
    RhythmPlaySettings settings;
    settings._bAutoPlay    = SW_TRUE;
    settings._globalOffset = 0.07f;
    RhythmPlaySession session;
    session.initialize( &chart, &judge, settings );
    session.press( 3, 0.2f ); // 오토플레이는 사람의 입력을 받지 않는다
    for ( int32 frame = 0; frame < 60 * 8; ++frame )
        session.update( static_cast<float32>( frame ) / 60.0f );

    SW_EXPECT_TRUE( session.getState() == RhythmPlayState::Cleared );
    SW_EXPECT_EQUAL( 10, chart.getJudgmentCount() );
    SW_EXPECT_EQUAL( 10, session.findGradeCount( hashed_string( "Cool" ) ) );
    SW_EXPECT_EQUAL( 0, session.getMissCount() );
    SW_EXPECT_EQUAL( 10, session.getMaxCombo() );
    SW_EXPECT_NEAR_EQUAL( 100.0f, session.computeAccuracy(), 1.0e-4f );
    SW_EXPECT_TRUE( session.computeRank() == hashed_string( "S" ) );

    vector<RhythmEvent> listEvent;
    session.drainEvents( listEvent );
    for ( const RhythmEvent& event : listEvent )
    {
        if ( event._kind == RhythmEvent::Kind::Judged )
            SW_EXPECT_NEAR_EQUAL( 0.0f, event._offset, 1.0e-6f ); // 판정 시각에 정확히
    }
}

SW_TEST_CASE( RhythmTest, ScrollPositionIsTimeBasedOrFollowsBpmAndStops )
{
    RhythmChart chart;
    SW_ASSERT_TRUE( chart.loadFromXmlText( R"(<Chart lanes="4" offset="0">
        <Bpm beat="0" bpm="120"/><Bpm beat="8" bpm="240"/><Stop beat="12" seconds="1"/><Note lane="0" beat="14"/>
      </Chart>)",
                                           "RhythmTest" ) );
    SW_EXPECT_NEAR_EQUAL( 120.0f, chart.getBaseBpm(), 1.0e-4f );
    constexpr float32 kHiSpeed = 100.0f;

    // 기준 BPM 구간에서는 두 방식이 같다.
    SW_EXPECT_NEAR_EQUAL( 100.0f, chart.computeNoteY( 4.0f, 1.0f, kHiSpeed, RhythmScrollMode::ConstantSpeed ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, chart.computeNoteY( 4.0f, 1.0f, kHiSpeed, RhythmScrollMode::FollowBpm ), 1.0e-3f );
    // 두 배 빠른 구간: 시간 기반은 박 사이가 반으로, BPM 따라는 그대로.
    SW_EXPECT_NEAR_EQUAL( 50.0f, chart.computeNoteY( 10.0f, 4.0f, kHiSpeed, RhythmScrollMode::ConstantSpeed ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, chart.computeNoteY( 10.0f, 4.0f, kHiSpeed, RhythmScrollMode::FollowBpm ), 1.0e-3f );
    // 정지(5.0 ~ 6.0 초): 시간 기반은 계속 내려오고, BPM 따라는 멈춘다.
    SW_EXPECT_NEAR_EQUAL( 130.0f, chart.computeNoteY( 14.0f, 5.2f, kHiSpeed, RhythmScrollMode::ConstantSpeed ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 80.0f, chart.computeNoteY( 14.0f, 5.7f, kHiSpeed, RhythmScrollMode::ConstantSpeed ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, chart.computeNoteY( 14.0f, 5.2f, kHiSpeed, RhythmScrollMode::FollowBpm ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, chart.computeNoteY( 14.0f, 5.7f, kHiSpeed, RhythmScrollMode::FollowBpm ), 1.0e-3f );
    // 판정 시각에는 판정선 위(0).
    SW_EXPECT_NEAR_EQUAL( 0.0f, chart.computeNoteY( 14.0f, chart.convertBeatToSeconds( 14.0f ), kHiSpeed, RhythmScrollMode::ConstantSpeed ), 1.0e-3f );

    // 전역 오프셋은 노트를 그만큼 늦게 내린다.
    TimingJudge judge;
    SW_ASSERT_TRUE( judge.loadFromXmlText( kRhythmJudgeXml, "RhythmTest" ) );
    RhythmPlaySettings settings;
    settings._globalOffset = 0.2f;
    settings._scrollMode   = RhythmScrollMode::FollowBpm;
    RhythmPlaySession session;
    session.initialize( &chart, &judge, settings );
    SW_EXPECT_NEAR_EQUAL( 100.0f, session.computeNoteY( 10.0f, 4.2f, kHiSpeed ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, session.computeNoteY( 14.0f, chart.convertBeatToSeconds( 14.0f ) + 0.2f, kHiSpeed ), 1.0e-3f );
}
