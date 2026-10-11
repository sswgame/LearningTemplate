#include "pch.h"

#include "Engine/Automation/AutomationScenarioChain.h"

#include "TestFramework/TestFramework.h"

// 시나리오 연속 실행의 순서 — 목록 가르기 · 단계 보고 경로 · 통과해야 다음 단계 · 실패하면 나머지를 돌리지 않음(프로세스 없이 순서 규칙만 본다).

/**
 * @brief [AutomationScenarioChainTest] 목록은 쉼표로 가르고 앞뒤 공백과 빈 항목은 버린다
 */
SW_TEST_CASE( AutomationScenarioChainTest, SplitListTrimsAndDropsEmptyItems )
{
    const sw::vector<sw::string> listPath = sw::AutomationScenarioChain::splitList( " engine/a.scenario.xml ,b.scenario.xml,, 	c.scenario.xml," );
    SW_ASSERT_EQUAL( size_t{ 3 }, listPath.size() );
    SW_EXPECT_STREQ( "engine/a.scenario.xml", listPath[0].c_str() );
    SW_EXPECT_STREQ( "b.scenario.xml", listPath[1].c_str() );
    SW_EXPECT_STREQ( "c.scenario.xml", listPath[2].c_str() );
    SW_EXPECT_TRUE( sw::AutomationScenarioChain::splitList( " , ," ).empty() );
    SW_EXPECT_EQUAL( size_t{ 1 }, sw::AutomationScenarioChain::splitList( "only.scenario.xml" ).size() );
}

/**
 * @brief [AutomationScenarioChainTest] 보고 경로는 단계가 하나면 그대로이고, 여럿이면 확장자 앞에 1 부터 번호를 붙인다(폴더 이름의 점은 확장자가 아니다)
 */
SW_TEST_CASE( AutomationScenarioChainTest, StageReportPathsAreNumberedOnlyForChains )
{
    using Chain = sw::AutomationScenarioChain;
    SW_EXPECT_STREQ( "Saved/out.json", Chain::makeStageReportPath( "Saved/out.json", 0, 1 ).c_str() );
    SW_EXPECT_STREQ( "Saved/out.1.json", Chain::makeStageReportPath( "Saved/out.json", 0, 3 ).c_str() );
    SW_EXPECT_STREQ( "Saved/out.3.json", Chain::makeStageReportPath( "Saved/out.json", 2, 3 ).c_str() );
    SW_EXPECT_STREQ( "Saved.dir/out.2", Chain::makeStageReportPath( "Saved.dir/out", 1, 2 ).c_str() );
    SW_EXPECT_STREQ( "", Chain::makeStageReportPath( "", 0, 3 ).c_str() );
}

/**
 * @brief [AutomationScenarioChainTest] 단계가 모두 통과해야 체인이 통과한다 — 마지막 단계가 끝나면 더 이어 가지 않는다
 */
SW_TEST_CASE( AutomationScenarioChainTest, AllStagesMustPass )
{
    sw::AutomationScenarioChain chain;
    chain.begin( { "a.xml", "b.xml", "c.xml" } );
    SW_EXPECT_TRUE( chain.isActive() );
    SW_EXPECT_TRUE( chain.isChain() );
    SW_EXPECT_STREQ( "a.xml", chain.getCurrentPath().c_str() );
    SW_EXPECT_TRUE( chain.getFinalResult() == sw::AutomationResult::Running );

    SW_EXPECT_TRUE( chain.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, chain.getStageIndex() );
    SW_EXPECT_STREQ( "b.xml", chain.getCurrentPath().c_str() );
    SW_EXPECT_TRUE( chain.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_STREQ( "c.xml", chain.getCurrentPath().c_str() );
    SW_EXPECT_FALSE( chain.onStageEnded( sw::AutomationResult::Passed ) ); // 마지막 — 이어 갈 단계가 없다
    SW_EXPECT_FALSE( chain.isActive() );
    SW_EXPECT_TRUE( chain.getFinalResult() == sw::AutomationResult::Passed );
    SW_EXPECT_EQUAL( size_t{ 0 }, chain.getSkippedStageCount() );
    SW_EXPECT_TRUE( chain.getCurrentPath().empty() );
}

/**
 * @brief [AutomationScenarioChainTest] 통과하지 못한 단계에서 멈춘다 — 나머지는 돌리지 않고, 결과는 처음 통과하지 못한 단계의 것이며, 그 단계가 어디였는지 보고에 남는다
 */
SW_TEST_CASE( AutomationScenarioChainTest, FirstNonPassingStageStopsTheChain )
{
    sw::AutomationScenarioChain chain;
    chain.begin( { "a.xml", "b.xml", "c.xml", "d.xml" } );
    SW_EXPECT_TRUE( chain.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_FALSE( chain.onStageEnded( sw::AutomationResult::TimedOut ) ); // 2/4 에서 졌다
    SW_EXPECT_FALSE( chain.isActive() );
    SW_EXPECT_TRUE( chain.getFinalResult() == sw::AutomationResult::TimedOut );
    SW_EXPECT_EQUAL( size_t{ 2 }, chain.getSkippedStageCount() );
    SW_EXPECT_FALSE( chain.onStageEnded( sw::AutomationResult::Failed ) ); // 끝난 체인은 결과를 바꾸지 않는다
    SW_EXPECT_TRUE( chain.getFinalResult() == sw::AutomationResult::TimedOut );

    const sw::string line = chain.describeStageEnd( 1, sw::AutomationResult::TimedOut, "engine.reload" );
    SW_EXPECT_TRUE( line.find( "stage 2/4" ) != sw::string::npos );
    SW_EXPECT_TRUE( line.find( "TIMEOUT" ) != sw::string::npos );
    SW_EXPECT_TRUE( line.find( "engine.reload" ) != sw::string::npos );
    SW_EXPECT_TRUE( line.find( "2 later stage(s) skipped" ) != sw::string::npos );
}

/**
 * @brief [AutomationScenarioChainTest] 건너뜀(이 기계에서 못 돎)도 통과가 아니다 — 체인은 거기서 멈추고 건너뜀으로 끝난다. 빈 목록은 곧바로 끝난 통과다
 */
SW_TEST_CASE( AutomationScenarioChainTest, SkippedStopsTheChainAndAnEmptyListIsDone )
{
    sw::AutomationScenarioChain chain;
    chain.begin( { "a.xml", "b.xml" } );
    SW_EXPECT_FALSE( chain.onStageEnded( sw::AutomationResult::Skipped ) );
    SW_EXPECT_TRUE( chain.getFinalResult() == sw::AutomationResult::Skipped );

    sw::AutomationScenarioChain single;
    single.begin( { "only.xml" } );
    SW_EXPECT_FALSE( single.isChain() );
    SW_EXPECT_FALSE( single.onStageEnded( sw::AutomationResult::Failed ) );
    SW_EXPECT_TRUE( single.getFinalResult() == sw::AutomationResult::Failed );

    sw::AutomationScenarioChain empty;
    empty.begin( {} );
    SW_EXPECT_FALSE( empty.isActive() );
    SW_EXPECT_TRUE( empty.getFinalResult() == sw::AutomationResult::Passed );
}

/**
 * @brief [AutomationScenarioChainTest] keepGoing 이면 실패한 단계 뒤에도 다음 단계를 돌려 실패를 모은다 — 결과는 처음 통과하지 못한 단계의 것이고, 건너뜀은 실패로 세지 않고 이어 간다(전부 건너뛰면 건너뜀)
 */
SW_TEST_CASE( AutomationScenarioChainTest, KeepGoingCollectsEveryFailure )
{
    sw::AutomationScenarioChain chain;
    chain.begin( { "a.xml", "b.xml", "c.xml", "d.xml" }, true );
    SW_EXPECT_TRUE( chain.onStageEnded( sw::AutomationResult::Failed ) ); // 실패했어도 이어 간다
    SW_EXPECT_STREQ( "b.xml", chain.getCurrentPath().c_str() );
    SW_EXPECT_TRUE( chain.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_TRUE( chain.onStageEnded( sw::AutomationResult::TimedOut ) );
    SW_EXPECT_FALSE( chain.onStageEnded( sw::AutomationResult::Passed ) );    // 마지막
    SW_EXPECT_TRUE( chain.getFinalResult() == sw::AutomationResult::Failed ); // 처음 통과하지 못한 단계(a)의 결과
    SW_EXPECT_EQUAL( size_t{ 2 }, chain.getFailedStageCount() );
    SW_EXPECT_EQUAL( size_t{ 0 }, chain.getSkippedStageCount() );

    // 건너뜀은 그 단계만의 일이다 — 실패로 세지 않고 뒤 단계를 돌린다. 실패가 없으면 체인은 통과이고, 전부 건너뛰었을 때만 건너뜀이다.
    sw::AutomationScenarioChain skipped;
    skipped.begin( { "a.xml", "b.xml", "c.xml" }, true );
    SW_EXPECT_TRUE( skipped.onStageEnded( sw::AutomationResult::LoadError ) );
    SW_EXPECT_TRUE( skipped.onStageEnded( sw::AutomationResult::Skipped ) );
    SW_EXPECT_FALSE( skipped.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_TRUE( skipped.getFinalResult() == sw::AutomationResult::LoadError );
    SW_EXPECT_EQUAL( size_t{ 1 }, skipped.getFailedStageCount() );
    SW_EXPECT_EQUAL( size_t{ 1 }, skipped.getSkippedResultCount() );

    sw::AutomationScenarioChain someSkipped;
    someSkipped.begin( { "a.xml", "b.xml" }, true );
    SW_EXPECT_TRUE( someSkipped.onStageEnded( sw::AutomationResult::Skipped ) );
    SW_EXPECT_FALSE( someSkipped.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_TRUE( someSkipped.getFinalResult() == sw::AutomationResult::Passed );

    sw::AutomationScenarioChain allSkipped;
    allSkipped.begin( { "a.xml", "b.xml" }, true );
    SW_EXPECT_TRUE( allSkipped.onStageEnded( sw::AutomationResult::Skipped ) );
    SW_EXPECT_FALSE( allSkipped.onStageEnded( sw::AutomationResult::Skipped ) );
    SW_EXPECT_TRUE( allSkipped.getFinalResult() == sw::AutomationResult::Skipped );

    sw::AutomationScenarioChain lastFails;
    lastFails.begin( { "a.xml", "b.xml" }, true );
    SW_EXPECT_TRUE( lastFails.onStageEnded( sw::AutomationResult::Passed ) );
    SW_EXPECT_FALSE( lastFails.onStageEnded( sw::AutomationResult::Failed ) );
    SW_EXPECT_TRUE( lastFails.getFinalResult() == sw::AutomationResult::Failed );
}
