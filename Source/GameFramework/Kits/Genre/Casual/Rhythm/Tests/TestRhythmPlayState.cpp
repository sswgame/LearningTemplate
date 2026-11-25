#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmChart.h"
#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmPlaySession.h"
#include "GameFramework/Runtime/Character/Input/TimingJudge.h"

// RhythmPlayStateTest — 리듬 한 판의 상태 바이트 신뢰 경계: 레인 대기열 · 판정 수 · 입력 기록 · 라이프 · 상태가 서로와 채보에 맞지 않으면 거절하고 판을 둔다.
// 꾸민 바이트는 `writeState` 의 형식을 그대로 손으로 적는다. 키트 GF_Rhythm(nogpu).

namespace
{
    // 레인 둘. 0 번 노트는 레인 0 의 롱노트(박 4 ~ 8, 2 ~ 4 초), 1 번 노트는 레인 1 의 보통 노트(박 6, 3 초). 판정은 셋이다.
    constexpr const utf8* kChartText = R"(<Chart title="Song" artist="Band" level="1" lanes="2">
        <Bpm beat="0" bpm="120"/>
        <Note lane="0" beat="4" endBeat="8"/>
        <Note lane="1" beat="6"/>
    </Chart>)";

    /** @brief 머리를 맞혀 롱노트를 누르고 있는 판을 꾸밉니다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨립니다. */
    struct RhythmFixture
    {
        float64 _accuracyWeightSum{ 1.0 };
        int64   _score{ 100 };
        float32 _life{ 100.0f };
        int32   _noteCount{ 2 };
        int32   _holdingNote0{ 0 };
        int32   _holdingNote1{ -1 };
        int32   _recordLane{ 0 };
        int32   _gradeCount{ 1 };
        int32   _combo{ 1 };
        int32   _maxCombo{ 1 };
        int32   _missCount{ 0 };
        int32   _judgedCount{ 1 };
        uint32  _cursor0{ 1 };
        uint32  _cursor1{ 0 };
        uint8   _state{ static_cast<uint8>( sw::RhythmPlayState::Playing ) };

        bool readInto( sw::RhythmPlaySession& inoutSession ) const
        {
            sw::Archive written;
            // 리플렉션 머리는 정상 `writeState` 바이트에서 가져온다 — 손으로 짜면 머리가 달라 다른 이유로 거절되어 거짓 통과한다.
            sw::Archive sessionBytes;
            inoutSession.writeState( sessionBytes );
            written.writeBytes( sessionBytes.getData(), sw::StateIO::kReflectedHeaderBytes );
            written << static_cast<uint32>( 1 ); // 등급 하나
            written << _gradeCount;
            written << static_cast<uint32>( 1 ); // 입력 기록 하나
            written << 2.0f;
            written << _recordLane;
            written << static_cast<uint8>( SW_TRUE );
            written << _score;
            written << _accuracyWeightSum;
            written << _life;
            written << 2.0f;
            written << _combo;
            written << _maxCombo;
            written << _missCount;
            written << _judgedCount;
            written << static_cast<int64>( _state ); // 리플렉션 열거는 int64 로 놓인다
            written << static_cast<uint32>( _noteCount );
            written << static_cast<uint32>( 2 );
            written << _cursor0;
            written << _holdingNote0;
            written << _cursor1;
            written << _holdingNote1;
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutSession.readState( reading );
        }
    };

    /** @brief 채보, 판정기, 시작한 판입니다. */
    /** @brief 씬 데이터(`stage.scene.xml` 의 `_listRank`)와 같은 등급 표를 든 설정입니다 — 등급 표가 없으면 판이 열리지 않는다. */
    sw::RhythmPlaySettings makeSettings()
    {
        sw::RhythmPlaySettings settings;
        const struct
        {
            const utf8* _pRank;
            float32     _minAccuracy;
        } arrRank[] = {
            {"S", 95.0f},
            {"A", 90.0f},
            {"B", 80.0f},
            {"C", 70.0f},
            {"D",  0.0f}
        };
        for ( const auto& source : arrRank )
        {
            sw::RhythmRankThreshold threshold;
            threshold._rank        = sw::hashed_string( source._pRank );
            threshold._minAccuracy = source._minAccuracy;
            settings._listRankThreshold.push_back( threshold );
        }
        return settings;
    }

    struct RhythmPlayStateTest
    {
        sw::RhythmChart       _chart{};
        sw::TimingJudge       _judge{};
        sw::RhythmPlaySession _session{};

        RhythmPlayStateTest()
        {
            SW_REQUIRE_TRUE( _chart.loadFromXMLText( kChartText, "RhythmPlayStateTest" ) );
            sw::TimingWindow window;
            window._grade      = "Cool";
            window._earlyWidth = 0.1f;
            window._lateWidth  = 0.1f;
            window._score      = 100;
            _judge.setWindows( { window } );
            SW_REQUIRE_TRUE( _session.initialize( _chart, _judge, makeSettings() ) );
        }
    };
} // namespace

/**
 * @brief [RhythmPlayStateTest] 실제 판은 왕복한다(롱노트를 누른 채로 이어진다). 형식에 맞게 꾸민 판도 읽힌다
 */
SW_TEST_CASE_F( RhythmPlayStateTest, RealAndCraftedStatesRead )
{
    _session.press( 0, 2.0f );
    SW_REQUIRE_TRUE( _session.isHolding( 0 ) );
    sw::Archive written;
    _session.writeState( written );

    sw::RhythmPlaySession restored;
    SW_REQUIRE_TRUE( restored.initialize( _chart, _judge, makeSettings() ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.isHolding( 0 ) );
    SW_EXPECT_EQUAL( _session.getScore(), restored.getScore() );
    SW_EXPECT_EQUAL( _session.getJudgedCount(), restored.getJudgedCount() );

    SW_REQUIRE_TRUE( RhythmFixture{}.readInto( restored ) );
    SW_EXPECT_EQUAL( int64( 100 ), restored.getScore() );
}

/**
 * @brief [RhythmPlayStateTest] 결함: 보통 노트나 아직 머리를 판정하지 않은 노트를 누르고 있다고 적으면 끝 판정을 공짜로 받았다. -1 밖의 음수 번호도 받았다. 거절하고 판을 둔다
 */
SW_TEST_CASE_F( RhythmPlayStateTest, RefusesHoldingWhatWasNotPressed )
{
    SW_REQUIRE_TRUE( RhythmFixture{}.readInto( _session ) );

    RhythmFixture notYetPressed;
    notYetPressed._cursor0 = 0;
    SW_EXPECT_FALSE( notYetPressed.readInto( _session ) );

    RhythmFixture shortNote;
    shortNote._cursor0      = 0;
    shortNote._holdingNote0 = -1;
    shortNote._cursor1      = 1;
    shortNote._holdingNote1 = 1;
    SW_EXPECT_FALSE( shortNote.readInto( _session ) );

    RhythmFixture otherLane;
    otherLane._holdingNote0 = 1;
    SW_EXPECT_FALSE( otherLane.readInto( _session ) );

    RhythmFixture brokenIndex;
    brokenIndex._holdingNote0 = -2;
    SW_EXPECT_FALSE( brokenIndex.readInto( _session ) );

    RhythmFixture cursorOver;
    cursorOver._cursor0 = 2;
    SW_EXPECT_FALSE( cursorOver.readInto( _session ) );

    RhythmFixture otherChart;
    otherChart._noteCount = 3;
    SW_EXPECT_FALSE( otherChart.readInto( _session ) );

    SW_EXPECT_TRUE( _session.isHolding( 0 ) );
    SW_EXPECT_EQUAL( 1, _session.getJudgedCount() );
}

/**
 * @brief [RhythmPlayStateTest] 결함: 판정 수를 서로 맞춰 보지 않아 음수 등급 수, 판정 없이 지나간 노트, 채보보다 많은 판정, 판정 수를 넘는 콤보 · 정확도를 받았다. 거절하고 판을 둔다
 */
SW_TEST_CASE_F( RhythmPlayStateTest, RefusesCountsThatDoNotAddUp )
{
    SW_REQUIRE_TRUE( RhythmFixture{}.readInto( _session ) );

    RhythmFixture gradeNegative;
    gradeNegative._gradeCount = -1;
    SW_EXPECT_FALSE( gradeNegative.readInto( _session ) );

    RhythmFixture missNegative;
    missNegative._missCount = -1;
    SW_EXPECT_FALSE( missNegative.readInto( _session ) );

    RhythmFixture gradeMismatch;
    gradeMismatch._gradeCount = 2;
    SW_EXPECT_FALSE( gradeMismatch.readInto( _session ) );

    RhythmFixture skippedNotes;
    skippedNotes._cursor1 = 1;
    SW_EXPECT_FALSE( skippedNotes.readInto( _session ) );

    RhythmFixture overChart;
    overChart._gradeCount  = 4;
    overChart._judgedCount = 4;
    SW_EXPECT_FALSE( overChart.readInto( _session ) );

    RhythmFixture comboOver;
    comboOver._combo    = 2;
    comboOver._maxCombo = 2;
    SW_EXPECT_FALSE( comboOver.readInto( _session ) );

    RhythmFixture comboNegative;
    comboNegative._combo = -1;
    SW_EXPECT_FALSE( comboNegative.readInto( _session ) );

    RhythmFixture accuracyOver;
    accuracyOver._accuracyWeightSum = 1.5;
    SW_EXPECT_FALSE( accuracyOver.readInto( _session ) );

    RhythmFixture accuracyNegative;
    accuracyNegative._accuracyWeightSum = -0.5;
    SW_EXPECT_FALSE( accuracyNegative.readInto( _session ) );

    RhythmFixture scoreNegative;
    scoreNegative._score = -1;
    SW_EXPECT_FALSE( scoreNegative.readInto( _session ) );

    SW_EXPECT_EQUAL( 1, _session.getJudgedCount() );
    SW_EXPECT_EQUAL( int64( 100 ), _session.getScore() );
}

/**
 * @brief [RhythmPlayStateTest] 결함: 범위 밖 라이프, 판정이 남았는데 깬 판, 라이프가 남았는데 실패한 판, 레인 밖의 입력 기록(리플레이가 쓴다)을 받았다. 거절하고 판을 둔다
 */
SW_TEST_CASE_F( RhythmPlayStateTest, RefusesLifeStateAndRecordsOutOfRange )
{
    SW_REQUIRE_TRUE( RhythmFixture{}.readInto( _session ) );

    RhythmFixture lifeOver;
    lifeOver._life = 100.5f;
    SW_EXPECT_FALSE( lifeOver.readInto( _session ) );

    RhythmFixture lifeNegative;
    lifeNegative._life = -1.0f;
    SW_EXPECT_FALSE( lifeNegative.readInto( _session ) );

    RhythmFixture fakeClear;
    fakeClear._state = static_cast<uint8>( sw::RhythmPlayState::Cleared );
    SW_EXPECT_FALSE( fakeClear.readInto( _session ) );

    RhythmFixture fakeFail;
    fakeFail._state = static_cast<uint8>( sw::RhythmPlayState::Failed );
    SW_EXPECT_FALSE( fakeFail.readInto( _session ) );

    RhythmFixture brokenState;
    brokenState._state = 9;
    SW_EXPECT_FALSE( brokenState.readInto( _session ) );

    RhythmFixture recordOutside;
    recordOutside._recordLane = 2;
    SW_EXPECT_FALSE( recordOutside.readInto( _session ) );

    RhythmFixture recordNegative;
    recordNegative._recordLane = -1;
    SW_EXPECT_FALSE( recordNegative.readInto( _session ) );

    SW_EXPECT_EQUAL( sw::RhythmPlayState::Playing, _session.getState() );
    SW_EXPECT_NEAR( 100.0f, _session.getLife(), 1.0e-5f );
    SW_EXPECT_EQUAL( size_t( 1 ), _session.getInputRecords().size() );
}

/**
 * @brief [RhythmPlayStateTest] 결정성: 같은 누름 · 뗌 시각이면 판정 · 콤보 · 롱노트 · 입력 기록이 같은 바이트를 내고, 중간 저장에서 이어도 같다
 */
SW_TEST_CASE_F( RhythmPlayStateTest, PlayDeterminism )
{
    // 레인 둘에 탭 · 롱노트를 엇갈려 놓는다(120 BPM — 박당 0.5 초). 누름은 1 초마다 레인 0, 0.53 초 어긋나 레인 1 이다.
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( R"(<Chart title="Song" artist="Band" level="1" lanes="2">
        <Bpm beat="0" bpm="120"/>
        <Note lane="0" beat="2"/><Note lane="0" beat="4" endBeat="6"/><Note lane="0" beat="8"/><Note lane="0" beat="10"/>
        <Note lane="1" beat="3"/><Note lane="1" beat="5"/><Note lane="1" beat="7" endBeat="9"/><Note lane="1" beat="11"/>
    </Chart>)",
                                            "RhythmPlayStateTest" ) );
    const auto setup = [this, &chart]( sw::RhythmPlaySession& session ) -> bool
    { return session.initialize( chart, _judge, makeSettings() ); };
    const auto step = []( sw::RhythmPlaySession& session, int32 stepIndex )
    {
        const float32 now = static_cast<float32>( stepIndex ) / 30.0f;
        session.update( now );
        if ( stepIndex % 30 == 0 )
            session.press( 0, now );
        if ( stepIndex % 30 == 25 )
            session.release( 0, now );
        if ( stepIndex % 30 == 16 )
            session.press( 1, now );
        if ( stepIndex % 30 == 10 )
            session.release( 1, now );
    };
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::isDeterministic<sw::RhythmPlaySession>( setup, step, 200 ) );
}

/**
 * @brief [RhythmPlayStateTest] 판정 창이 없는 판정기로는 판을 열지 않는다 — 열면 오토플레이의 `update` 가 끝나지 않는다
 */
SW_TEST_CASE( RhythmPlayStateTest, RefusesAJudgeWithoutWindows )
{
    sw::RhythmChart chart;
    SW_REQUIRE_TRUE( chart.loadFromXMLText( kChartText, "NoWindows" ) );
    const sw::TimingJudge  judge;
    sw::RhythmPlaySettings settings = makeSettings();
    settings._bAutoPlay             = SW_TRUE;
    sw::RhythmPlaySession session;
    SW_EXPECT_LOG( "RhythmPlaySession.NoTimingWindows" );
    SW_EXPECT_FALSE( session.initialize( chart, judge, settings ) );
    session.update( 100.0f ); // 열리지 않은 판이라 할 일이 없다
    SW_EXPECT_EQUAL( 0, session.getJudgedCount() );
}

/** @brief [RhythmPlayStateTest] 등급 표는 데이터다 — 없는 설정으로는 판이 열리지 않고, 표를 바꾸면 같은 정확도의 등급이 바뀐다 */
SW_TEST_CASE_F( RhythmPlayStateTest, RanksComeFromTheSettings )
{
    SW_EXPECT_LOG( "RhythmPlaySession.NoRankThresholds" );
    sw::RhythmPlaySession refused;
    SW_EXPECT_FALSE( refused.initialize( _chart, _judge, sw::RhythmPlaySettings{} ) );

    // 아무것도 판정하지 않은 판의 정확도는 100 % 다 — 기본 표에서는 S, 문턱 하나짜리 표에서는 그 이름이다.
    SW_EXPECT_TRUE( _session.computeRank() == sw::hashed_string( "S" ) );
    sw::RhythmPlaySettings  settings;
    sw::RhythmRankThreshold only;
    only._rank        = sw::hashed_string( "Gold" );
    only._minAccuracy = 0.0f;
    settings._listRankThreshold.push_back( only );
    sw::RhythmPlaySession custom;
    SW_REQUIRE_TRUE( custom.initialize( _chart, _judge, settings ) );
    SW_EXPECT_TRUE( custom.computeRank() == sw::hashed_string( "Gold" ) );
}
