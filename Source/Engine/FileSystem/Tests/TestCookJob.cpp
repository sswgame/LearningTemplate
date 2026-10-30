#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/CookJob.h"

using namespace sw;

/**
 * @brief [CookJobTest] 작업 폴더 기준 디스크 경로로 읽은 파일 · 목록은 절대 경로로 기록되어, 기록 뒤에 다시 풀어도 같은 파일 · 폴더의 해시가 나온다
 * @details 예전에는 상대 글이 그대로 리소스 id 가 되어 다시 풀 때 리소스 폴더에서 찾았다 — 기록이 늘 낡았다(없음) 또는 같은 이름의 다른 리소스와 비교됐다.
 */
SW_TEST_CASE( CookJobTest, RecordsRelativeDiskReadsAsAbsolutePaths )
{
    const string folder   = test::makeScratchPath( "Source" );
    const string filePath = FileUtil::joinPath( folder, "a.txt" );
    SW_REQUIRE_FALSE( FileUtil::isAbsolutePath( filePath ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) && FileUtil::writeTextFile( filePath, "one" ) );

    CookJob        job( CookJobDesc{ ._cookerName = "Fake", ._cookerVersion = 1, ._target = "Test", ._settingsHash = 0 } );
    vector<uint8>  bytes;
    vector<string> listFile;
    SW_REQUIRE_TRUE( AssetIO::readBinaryFile( filePath, bytes ) );
    SW_REQUIRE_TRUE( CookJob::collectFiles( folder, ".txt", listFile ) );

    SW_REQUIRE_EQUAL( size_t( 2 ), job.getInputs().size() );
    for ( const CookInput& input : job.getInputs() )
    {
        SW_EXPECT_TRUE( FileUtil::isAbsolutePath( input._assetID ) );
        SW_EXPECT_NOT_EQUAL( uint64( 0 ), input._contentHash );
        SW_EXPECT_EQUAL( input._contentHash, input.computeCurrentHash() );
    }
}
