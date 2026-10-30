#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/CookJob.h"

using namespace sw;

/**
 * @brief [CookJobTest] 리소스 루트 밖 디스크 파일 · 목록은 절대 경로의 디스크 입력으로 기록되어, 기록 뒤에 다시 풀어도 같은 파일 · 폴더의 해시가 나온다
 * @details 예전에는 입력 한 칸에 리소스 id 와 디스크 경로를 함께 담고 다시 풀 때 모양으로 추측했다 — 종류를 기록에 남긴다(`CookInputKind`).
 */
SW_TEST_CASE( CookJobTest, RecordsDiskReadsAsAbsoluteDiskInputs )
{
    const string folder   = test::makeScratchPath( "Source" );
    const string filePath = FileUtil::joinPath( folder, "a.txt" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) && FileUtil::writeTextFile( filePath, "one" ) );

    CookJob        job( CookJobDesc{ ._cookerName = "Fake", ._cookerVersion = 1, ._target = "Test", ._settingsHash = 0 } );
    vector<uint8>  bytes;
    vector<string> listFile;
    SW_REQUIRE_TRUE( AssetIO::readBinaryFile( filePath, bytes ) );
    SW_REQUIRE_TRUE( CookJob::collectFiles( folder, ".txt", listFile ) );

    SW_REQUIRE_EQUAL( size_t( 2 ), job.getInputs().size() );
    for ( const CookInput& input : job.getInputs() )
    {
        SW_EXPECT_TRUE( FileUtil::isAbsolutePath( input._source ) );
        SW_EXPECT_TRUE( input._kind == CookInputKind::DiskFile || input._kind == CookInputKind::Listing ); // 리소스 루트 밖이라 리소스로 기록되지 않는다
        SW_EXPECT_NOT_EQUAL( uint64( 0 ), input._contentHash );
        SW_EXPECT_EQUAL( input._contentHash, input.computeCurrentHash() );
    }
}
