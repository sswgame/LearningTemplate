#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( ReserveUniquePathTest, FilesTakeTheNextFreeSuffix )
{
    const string base = test::makeScratchPath( "shot" );
    SW_EXPECT_EQUAL( base + ".png", FileUtil::reserveUniquePath( base, ".png", false ) );
    SW_EXPECT_EQUAL( base + "-2.png", FileUtil::reserveUniquePath( base, ".png", false ) );
    SW_EXPECT_EQUAL( base + "-3.png", FileUtil::reserveUniquePath( base, ".png", false ) );
    SW_EXPECT_TRUE( FileUtil::isRegularFile( base + "-2.png" ) );
}

SW_TEST_CASE( ReserveUniquePathTest, DirectoriesTakeTheNextFreeSuffix )
{
    const string base = test::makeScratchPath( "report" );
    SW_EXPECT_EQUAL( base, FileUtil::reserveUniquePath( base, {}, true ) );
    SW_EXPECT_EQUAL( base + "-2", FileUtil::reserveUniquePath( base, {}, true ) );
    SW_EXPECT_TRUE( FileUtil::isDirectory( base + "-2" ) );
}

SW_TEST_CASE( ReserveUniquePathTest, GivesUpAfterTheLastAttempt )
{
    const string base = test::makeScratchPath( "full" );
    SW_REQUIRE_EQUAL( base, FileUtil::reserveUniquePath( base, {}, true, 1 ) );
    SW_EXPECT_LOG( "File.CreateFailed" );
    SW_EXPECT_TRUE( FileUtil::reserveUniquePath( base, {}, true, 1 ).empty() );
}
