#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

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
            written << static_cast<uint32>( _noteCount );
            written << static_cast<uint32>( 2 );
            written << _cursor0;
            written << _holdingNote0;
            written << _cursor1;
            written << _holdingNote1;
            written << static_cast<uint32>( 1 );
            written << _gradeCount;
            written << static_cast<uint32>( 1 );
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
            written << _state;
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutSession.readState( reading );
        }
    };

    /** @brief 채보, 판정기, 시작한 판입니다. */
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
            SW_REQUIRE_TRUE( _session.initialize( &_chart, &_judge, sw::RhythmPlaySettings{} ) );
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
    SW_REQUIRE_TRUE( restored.initialize( &_chart, &_judge, sw::RhythmPlaySettings{} ) );
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
