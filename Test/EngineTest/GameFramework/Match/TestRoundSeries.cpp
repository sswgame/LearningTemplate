// 라운드 묶음 — 선승(1 위 1 점) · 라운드 시간(걸음) · 라운드 사이 대기 · 무승부 · 서든 데스, 순위 점수(같은 점수 같은 순위 · 목록 밖 0 점), 상태 바이트 형식 · 되살리기 · 거절.
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Gameplay/Match/RoundSeries.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 대기가 끝나 다음 라운드가 열릴 때까지 걸음을 진행합니다. 100 걸음 안에 안 열리면 false 입니다. */
    bool advanceRoundSeriesToNextRound( RoundSeries& series )
    {
        for ( int32 tick = 0; tick < 100; ++tick )
        {
            if ( series.advanceTick() == RoundSeriesTick::RoundStarted )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [RoundSeriesTest] 2 선승 — 라운드 시간은 걸음으로 세고 결과를 넣을 때까지 다시 알린다, 대기 중의 결과는 받지 않는다,
 *        무승부 라운드는 모두 1 점, 함께 닿아 같으면 무승부 규칙은 끝 · 서든 데스 규칙은 다음 라운드
 */
SW_TEST_CASE( RoundSeriesTest, FirstToWinClockIntermissionAndTieRules )
{
    RoundSeriesSettings settings;
    settings._winScore          = 2;
    settings._roundTicks        = 3;
    settings._intermissionTicks = 2;
    settings._tieRule           = RoundSeriesTieRule::Draw;
    RoundSeries series;
    series.initialize( settings );
    SW_EXPECT_TRUE( series.getPhase() == RoundSeriesPhase::Waiting );
    SW_EXPECT_TRUE( series.reportRoundWinner( 0 ) == RoundSeriesOutcome::Rejected ); // 시작 전
    SW_EXPECT_FALSE( series.start( 0 ) );
    SW_ASSERT_TRUE( series.start( 2 ) );
    SW_EXPECT_EQUAL( 0, series.getRoundIndex() );
    SW_EXPECT_EQUAL( 3, series.getRoundTicksRemaining() );

    // 라운드 시간 3 걸음 — 셋째 걸음에 다 되고, 결과를 넣을 때까지 걸음마다 다시 알린다.
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::None );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::None );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::TimeUp );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::TimeUp );
    SW_EXPECT_EQUAL( 0, series.getRoundTicksRemaining() );

    // 0 승 → 대기 2 걸음. 없는 참가자 · 대기 중의 결과는 받지 않는다.
    SW_EXPECT_TRUE( series.reportRoundWinner( 5 ) == RoundSeriesOutcome::Rejected );
    SW_EXPECT_TRUE( series.reportRoundWinner( 0 ) == RoundSeriesOutcome::Intermission );
    SW_EXPECT_EQUAL( 1, series.getTotal( 0 ) );
    SW_EXPECT_EQUAL( 0, series.getTotal( 1 ) );
    SW_EXPECT_EQUAL( 2, series.getIntermissionTicksRemaining() );
    SW_EXPECT_TRUE( series.reportRoundWinner( 1 ) == RoundSeriesOutcome::Rejected );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::None );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::RoundStarted );
    SW_EXPECT_EQUAL( 1, series.getRoundIndex() );
    SW_EXPECT_EQUAL( 3, series.getRoundTicksRemaining() );

    // 1 승 → 1 : 1. 무승부 라운드는 모두 1 위 → 2 : 2 로 함께 닿아 무승부로 끝난다.
    SW_EXPECT_TRUE( series.reportRoundWinner( 1 ) == RoundSeriesOutcome::Intermission );
    SW_ASSERT_TRUE( advanceRoundSeriesToNextRound( series ) );
    SW_EXPECT_TRUE( series.reportRoundWinner( RoundSeries::kNoWinner ) == RoundSeriesOutcome::Finished );
    SW_EXPECT_TRUE( series.isFinished() );
    SW_EXPECT_EQUAL( RoundSeries::kNoWinner, series.getWinner() );
    SW_EXPECT_EQUAL( 2, series.getTotal( 0 ) );
    SW_EXPECT_EQUAL( 2, series.getTotal( 1 ) );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::None );
    SW_EXPECT_TRUE( series.reportRoundWinner( 0 ) == RoundSeriesOutcome::Rejected );

    // 같은 흐름을 서든 데스로 — 2 : 2 에서 끝나지 않고 다음 라운드를 이긴 1 이 우승. 라운드 시간 0 은 제한 없음.
    settings._tieRule    = RoundSeriesTieRule::SuddenDeath;
    settings._roundTicks = 0;
    series.initialize( settings );
    SW_ASSERT_TRUE( series.start( 2 ) );
    SW_EXPECT_TRUE( series.advanceTick() == RoundSeriesTick::None );
    SW_EXPECT_TRUE( series.reportRoundWinner( 0 ) == RoundSeriesOutcome::Intermission );
    SW_ASSERT_TRUE( advanceRoundSeriesToNextRound( series ) );
    SW_EXPECT_TRUE( series.reportRoundWinner( 1 ) == RoundSeriesOutcome::Intermission );
    SW_ASSERT_TRUE( advanceRoundSeriesToNextRound( series ) );
    SW_EXPECT_TRUE( series.reportRoundWinner( RoundSeries::kNoWinner ) == RoundSeriesOutcome::Intermission );
    SW_EXPECT_FALSE( series.isFinished() );
    SW_ASSERT_TRUE( advanceRoundSeriesToNextRound( series ) );
    SW_EXPECT_EQUAL( 3, series.getRoundIndex() );
    SW_EXPECT_TRUE( series.reportRoundWinner( 1 ) == RoundSeriesOutcome::Finished );
    SW_EXPECT_EQUAL( 1, series.getWinner() );
    SW_EXPECT_EQUAL( 3, series.getTotal( 1 ) );
}

/**
 * @brief [RoundSeriesTest] 순위 점수 — 같은 점수 같은 순위 · 목록 밖 0 점 · 대기 0 은 바로 다음 라운드, 상태 바이트 형식(바이트 그대로) · 되살리기 · 참가자 수가 다르거나 모자란 바이트는 거절하고 바꾸지 않는다
 */
SW_TEST_CASE( RoundSeriesTest, PlacementPointsAndStateBytes )
{
    RoundSeriesSettings settings;
    settings._listPlacementPoint = { 3, 2, 1 };
    settings._winScore           = 5;
    RoundSeries series;
    series.initialize( settings );
    SW_ASSERT_TRUE( series.start( 4 ) );

    // 순위 = 1 + 나보다 점수가 높은 수. 2 · 5 · 2 · 0 → 2 · 1 · 2 · 4, 4 위는 목록 밖이라 0 점.
    const vector<int32> listScore{ 2, 5, 2, 0 };
    SW_EXPECT_EQUAL( 2, RoundSeries::computeRank( listScore, 0 ) );
    SW_EXPECT_EQUAL( 1, RoundSeries::computeRank( listScore, 1 ) );
    SW_EXPECT_EQUAL( 2, RoundSeries::computeRank( listScore, 2 ) );
    SW_EXPECT_EQUAL( 4, RoundSeries::computeRank( listScore, 3 ) );
    SW_EXPECT_EQUAL( 0, series.getPlacementPoint( 4 ) );
    SW_EXPECT_TRUE( series.reportRound( vector<int32>{ 1, 2 } ) == RoundSeriesOutcome::Rejected );
    SW_EXPECT_TRUE( series.reportRound( listScore ) == RoundSeriesOutcome::NextRound );
    SW_EXPECT_EQUAL( 1, series.getRoundIndex() );
    SW_EXPECT_EQUAL( 2, series.getTotal( 0 ) );
    SW_EXPECT_EQUAL( 3, series.getTotal( 1 ) );
    SW_EXPECT_EQUAL( 2, series.getTotal( 2 ) );
    SW_EXPECT_EQUAL( 0, series.getTotal( 3 ) );

    // 상태 바이트 — 단계 1 · 라운드 1(지그재그 02) · 남은 시간 0 · 남은 대기 0 · 우승자 −1(01) · 참가자 4 · 총점 2 3 2 0(지그재그 04 06 04 00).
    BitWriter writer;
    series.writeState( writer );
    const vector<uint8> bytes = writer.releaseBytes();
    const vector<uint8> expectedBytes{ 0x01, 0x02, 0x00, 0x00, 0x01, 0x04, 0x04, 0x06, 0x04, 0x00 };
    SW_EXPECT_TRUE( bytes == expectedBytes );

    // 같은 설정 · 참가자 수의 묶음에 되살린다.
    RoundSeries restored;
    restored.initialize( settings );
    SW_ASSERT_TRUE( restored.start( 4 ) );
    BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( 1, restored.getRoundIndex() );
    SW_EXPECT_EQUAL( 3, restored.getTotal( 1 ) );
    BitWriter rewriter;
    restored.writeState( rewriter );
    SW_EXPECT_TRUE( rewriter.releaseBytes() == bytes );

    // 참가자 수가 다르거나 바이트가 모자라면 거절하고 바꾸지 않는다.
    RoundSeries fresh;
    fresh.initialize( settings );
    SW_ASSERT_TRUE( fresh.start( 3 ) );
    BitReader otherReader( bytes.data(), static_cast<int32>( bytes.size() ) );
    SW_EXPECT_FALSE( fresh.readState( otherReader ) );
    SW_ASSERT_TRUE( fresh.start( 4 ) );
    BitReader shortReader( bytes.data(), static_cast<int32>( bytes.size() ) - 2 );
    SW_EXPECT_FALSE( fresh.readState( shortReader ) );
    SW_EXPECT_EQUAL( 0, fresh.getRoundIndex() );
    SW_EXPECT_EQUAL( 0, fresh.getTotal( 1 ) );

    // 2 라운드 0 · 0 · 9 · 9 → 2 · 3 번 공동 1 위(3 점씩), 총점 3 4 5 3 → 2 번 혼자 5 점으로 우승.
    SW_EXPECT_TRUE( series.reportRound( vector<int32>{ 0, 0, 9, 9 } ) == RoundSeriesOutcome::Finished );
    SW_EXPECT_EQUAL( 2, series.getWinner() );
}
