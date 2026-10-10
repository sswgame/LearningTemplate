#include "pch.h"

#include "Editor/Panels/PackagingProgressParser.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [PackagingProgressParserTest] 단계 줄은 진행을, 끝 줄은 폴더와 크기를 준다(폴더에 공백이 있어도 크기는 마지막 낱말)
 */
SW_TEST_CASE( PackagingProgressParserTest, StepAndDoneLines )
{
    PackagingProgress progress{};
    SW_EXPECT_FALSE( PackagingProgressParser::parseLine( "-- Configuring done", progress ) );
    SW_ASSERT_TRUE( PackagingProgressParser::parseLine( "[package] step 3/4 stage", progress ) );
    SW_EXPECT_EQUAL( 3u, progress._stepIndex );
    SW_EXPECT_EQUAL( 4u, progress._stepCount );
    SW_EXPECT_STREQ( "stage", progress._stepName.c_str() );
    SW_EXPECT_TRUE( progress._state == PackagingState::Running );
    SW_EXPECT_NEAR_EQUAL( 0.625f, progress.computeFraction(), 0.0001f );

    SW_ASSERT_TRUE( PackagingProgressParser::parseLine( "[package] done D:/My Packages/Empty-Client 1048576", progress ) );
    SW_EXPECT_TRUE( progress._state == PackagingState::Succeeded );
    SW_EXPECT_STREQ( "D:/My Packages/Empty-Client", progress._outputFolder.c_str() );
    SW_EXPECT_EQUAL( static_cast<uint64>( 1048576 ), progress._byteCount );
    SW_EXPECT_NEAR_EQUAL( 1.0f, progress.computeFraction(), 0.0001f );
}

/**
 * @brief [PackagingProgressParserTest] 실패 줄은 단계와 이유를 주고, 끝 줄 없이 끝난 실행은 실패다
 */
SW_TEST_CASE( PackagingProgressParserTest, FailedLineAndMissingVerdict )
{
    const vector<string>    listFailed = { "[package] step 1/4 build", "[package] FAILED build cmake --build --preset Ninja-Shipping failed" };
    const PackagingProgress failed     = PackagingProgressParser::parseOutput( listFailed, 1 );
    SW_EXPECT_TRUE( failed._state == PackagingState::Failed );
    SW_EXPECT_STREQ( "build", failed._stepName.c_str() );
    SW_EXPECT_STREQ( "cmake --build --preset Ninja-Shipping failed", failed._failure.c_str() );

    const vector<string>    listCut = { "[package] step 2/4 cook", "Traceback (most recent call last):" };
    const PackagingProgress cut     = PackagingProgressParser::parseOutput( listCut, 1 );
    SW_EXPECT_TRUE( cut._state == PackagingState::Failed );
    SW_EXPECT_STREQ( "Traceback (most recent call last):", cut._failure.c_str() );

    const PackagingProgress empty = PackagingProgressParser::parseOutput( {}, -1 );
    SW_EXPECT_TRUE( empty._state == PackagingState::Failed );
}
