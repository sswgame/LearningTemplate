#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/CookJob.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;

/**
 * @brief [CookJobTest] 리소스 루트 밖 디스크 파일 · 목록은 절대 경로의 디스크 입력으로 기록되어, 기록 뒤에 다시 풀어도 같은 파일 · 폴더의 해시가 나온다
 * @details 예전에는 입력 한 칸에 리소스 id 와 디스크 경로를 함께 담고 다시 풀 때 모양으로 추측했다 — 종류를 기록에 남긴다(`CookInputKind`).
 */
SW_TEST_CASE( CookJobTest, RecordsDiskReadsAsAbsoluteDiskInputs )
{
    const AbsolutePath folder   = test::makeScratchPath( "Source" );
    const AbsolutePath filePath = folder.join( "a.txt" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) && FileUtil::writeTextFile( filePath, "one" ) );

    CookJob              job( CookJobDesc{ ._cookerName = "Fake", ._codeFingerprint = 1, ._target = "Test", ._settingsHash = 0 } );
    vector<uint8>        bytes;
    vector<AbsolutePath> listFile;
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

/**
 * @brief [CookJobTest] 같은 입력인지는 종류 · 출처 · 확장자로만 가린다 — 내용 해시가 달라도 같은 입력이고(`==` 는 다르다), `makeSourceKey` 와 같은 동치다
 */
SW_TEST_CASE( CookJobTest, SameSourceIgnoresTheContentHash )
{
    const CookInput first{ CookInputKind::Listing, "a/folder", ".material", 1 };
    CookInput       changed = first;
    changed._contentHash    = 2;
    SW_EXPECT_TRUE( first.isSameSource( changed ) );
    SW_EXPECT_FALSE( first == changed );
    SW_EXPECT_EQUAL( first.makeSourceKey(), changed.makeSourceKey() );

    CookInput otherKind       = first;
    otherKind._kind           = CookInputKind::DiskFile;
    CookInput otherSource     = first;
    otherSource._source       = "b/folder";
    CookInput otherExtension  = first;
    otherExtension._extension = ".mesh";
    for ( const CookInput& other : { otherKind, otherSource, otherExtension } )
    {
        SW_EXPECT_FALSE( first.isSameSource( other ) );
        SW_EXPECT_TRUE( first.makeSourceKey() != other.makeSourceKey() );
    }
}

/** @brief [CookJobTest] 작업 안에서 "없음" 을 물은 것도 입력(해시 0)이다 — 있을 때만 읽는 리소스(층 파일)가 나중에 생기면 다시 굽는다. 있는 것은 뒤따르는 읽기가 기록한다 */
SW_TEST_CASE( CookJobTest, ExistenceQueriesRecordTheMissingResource )
{
    const test::ScopedScratchDomain domain;
    const AssetID                   missingID = test::ScopedScratchDomain::makeID( "layer.xml" );
    const AssetID                   presentID = test::ScopedScratchDomain::makeID( "base.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.xml" ), "<A/>" ) );

    const CookJob job{ CookJobDesc{} };
    SW_EXPECT_FALSE( AssetIO::hasAsset( missingID ) );
    SW_EXPECT_TRUE( AssetIO::hasAsset( presentID ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, job.getInputs().size() );
    const CookInput& input = job.getInputs().front();
    SW_EXPECT_TRUE( input._kind == CookInputKind::Resource );
    SW_EXPECT_EQUAL( uint64{ 0 }, input._contentHash );
    SW_EXPECT_EQUAL( uint64{ 0 }, input.computeCurrentHash() );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "layer.xml" ), "<A/>" ) );
    SW_EXPECT_TRUE( input.computeCurrentHash() != 0 ); // 생겼다 — 기록은 낡았다
}
