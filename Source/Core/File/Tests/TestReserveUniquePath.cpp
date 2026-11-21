#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

SW_TEST_CASE( ReserveUniquePathTest, FilesTakeTheNextFreeSuffix )
{
    const AbsolutePath base = test::makeScratchPath( "shot" );
    for ( const utf8* pSuffix : { "", "-2", "-3" } )
    {
        StringBuilder<constant::buffer::kMax1024> expectedPath;
        expectedPath.appendFormat( "%#%#.png", base.str(), pSuffix );
        SW_EXPECT_EQUAL( string( expectedPath.view() ), FileUtil::reserveUniquePath( base.str(), ".png", false ) );
    }
    StringBuilder<constant::buffer::kMax1024> secondPath;
    secondPath.appendFormat( "%#-2.png", base.str() );
    SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::toDiskPath( secondPath.view() ) ) );
}

SW_TEST_CASE( ReserveUniquePathTest, DirectoriesTakeTheNextFreeSuffix )
{
    const AbsolutePath base = test::makeScratchPath( "report" );
    SW_EXPECT_EQUAL( base.str(), FileUtil::reserveUniquePath( base.str(), {}, true ) );
    StringBuilder<constant::buffer::kMax1024> secondPath;
    secondPath.appendFormat( "%#-2", base.str() );
    SW_EXPECT_EQUAL( string( secondPath.view() ), FileUtil::reserveUniquePath( base.str(), {}, true ) );
    SW_EXPECT_TRUE( FileUtil::isDirectory( FileUtil::toDiskPath( secondPath.view() ) ) );
}

SW_TEST_CASE( ReserveUniquePathTest, GivesUpAfterTheLastAttempt )
{
    const AbsolutePath base = test::makeScratchPath( "full" );
    SW_REQUIRE_EQUAL( base.str(), FileUtil::reserveUniquePath( base.str(), {}, true, 1 ) );
    SW_EXPECT_LOG( "File.CreateFailed" );
    SW_EXPECT_TRUE( FileUtil::reserveUniquePath( base.str(), {}, true, 1 ).empty() );
}
