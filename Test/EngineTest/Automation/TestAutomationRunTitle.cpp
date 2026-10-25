#include "pch.h"

#include "Engine/Automation/AutomationRunTitle.h"

#include "TestFramework/TestFramework.h"

// 시나리오 · 벤치 실행 정보 글 — 창 제목과 단계 로그가 같은 함수로 만든 글을 쓴다(창이나 로그 없이 글자만 본다).

/**
 * @brief [AutomationRunTitleTest] 시나리오 요약은 이름 · 백엔드 · 단계 · 프레임을 순서대로 적는다
 */
SW_TEST_CASE( AutomationRunTitleTest, ScenarioSummaryHasNameBackendStepAndFrame )
{
    sw::AutomationRunTitleInput input;
    input._scenarioName = "shooter3d/weapons";
    input._backendName  = "DirectX12";
    input._stepKind     = "Expect";
    input._stepNumber   = 7;
    input._frame        = 120;
    SW_EXPECT_STREQ( "shooter3d/weapons \xC2\xB7 DirectX12 \xC2\xB7 step 7 Expect \xC2\xB7 f120", sw::AutomationRunTitleUtil::makeSummary( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 백엔드나 단계가 없으면 그 칸을 적지 않는다(헤드리스 · 첫 단계 전)
 */
SW_TEST_CASE( AutomationRunTitleTest, MissingBackendAndStepAreOmitted )
{
    sw::AutomationRunTitleInput input;
    input._scenarioName = "boot";
    input._frame        = 3;
    SW_EXPECT_STREQ( "boot \xC2\xB7 f3", sw::AutomationRunTitleUtil::makeSummary( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 시나리오가 없으면 벤치 인자 요약을 적는다
 */
SW_TEST_CASE( AutomationRunTitleTest, BenchSummaryReplacesScenarioName )
{
    sw::AutomationRunTitleInput input;
    input._benchSummary = "gv_benchMeshes=1000";
    input._backendName  = "Vulkan";
    input._frame        = 60;
    SW_EXPECT_STREQ( "bench gv_benchMeshes=1000 \xC2\xB7 Vulkan \xC2\xB7 f60", sw::AutomationRunTitleUtil::makeSummary( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 창 제목은 기본 제목 뒤에 요약을 붙인다
 */
SW_TEST_CASE( AutomationRunTitleTest, WindowTitlePrefixesBaseTitle )
{
    sw::AutomationRunTitleInput input;
    input._baseTitle    = "SWEngine";
    input._scenarioName = "boot";
    input._frame        = 1;
    SW_EXPECT_STREQ( "SWEngine \xE2\x80\x94 boot \xC2\xB7 f1", sw::AutomationRunTitleUtil::makeWindowTitle( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 긴 제목은 한계 안으로 줄이고 UTF-8 글자 중간을 자르지 않는다
 */
SW_TEST_CASE( AutomationRunTitleTest, LongTitleIsTruncatedOnCharacterBoundary )
{
    sw::string longName;
    for ( int32 index = 0; index < 100; ++index )
    {
        longName += "\xEA\xB0\x80"; // 가 (3 바이트) x 100 = 300 바이트
    }
    sw::AutomationRunTitleInput input;
    input._scenarioName    = longName;
    const sw::string title = sw::AutomationRunTitleUtil::makeWindowTitle( input );
    SW_EXPECT_TRUE( title.size() <= sw::AutomationRunTitleUtil::kMaxTitleBytes );
    SW_ASSERT_TRUE( title.size() >= 3 );
    SW_EXPECT_STREQ( "\xE2\x80\xA6", title.c_str() + title.size() - 3 );
    // 줄인 글은 전부 3 바이트 글자(앞 바이트 0xEA)와 끝의 말줄임표뿐이라 길이가 3 의 배수다.
    SW_EXPECT_EQUAL( size_t{ 0 }, title.size() % 3 );
}

/**
 * @brief [AutomationRunTitleTest] 단계 전환 로그는 이름 · 단계 번호 · 단계 이름 · 프레임을 적는다
 */
SW_TEST_CASE( AutomationRunTitleTest, StepLogLineMatchesAgreedFormat )
{
    SW_EXPECT_STREQ( "[Scenario] boot step 4 Screenshot frame 90", sw::AutomationRunTitleUtil::makeStepLogLine( "boot", 4, "Screenshot", 90 ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 벤치 요약은 gv_bench 인자만 이름순으로 잇는다
 */
SW_TEST_CASE( AutomationRunTitleTest, BenchSummaryKeepsOnlyBenchArgumentsSorted )
{
    sw::vector<sw::string> listPair;
    listPair.push_back( "gv_benchMeshes=1000" );
    listPair.push_back( "gv_fixedFrameDelta=0.016" );
    listPair.push_back( "gv_benchCharacters=50" );
    SW_EXPECT_STREQ( "gv_benchCharacters=50 gv_benchMeshes=1000", sw::AutomationRunTitleUtil::makeBenchSummary( listPair ).c_str() );
    SW_EXPECT_TRUE( sw::AutomationRunTitleUtil::makeBenchSummary( {} ).empty() );
}
