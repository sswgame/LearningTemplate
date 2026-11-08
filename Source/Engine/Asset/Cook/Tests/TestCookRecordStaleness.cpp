// CookRecordStalenessTest — 쿠킹 기록(층 2)이 입력 종류마다 새것 · 낡음을 맞게 가린다. 기록을 디스크에 썼다 읽어 다음 실행처럼 판정한다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;
using sw::test::ScopedScratchDomain;

namespace
{
    struct CookRecordStalenessTestInternal
    {
        static CookJobDesc makeDesc() { return CookJobDesc{ ._cookerName = "Fake", ._codeFingerprint = 1, ._target = "Test", ._settingsHash = 0 }; }

        /** @brief 끝난 작업 @p job 의 기록을 디스크에 썼다가 다시 읽습니다 — 판정은 쿠킹이 다음 실행에서 하듯 읽은 기록으로 한다. */
        static CookRecord roundTrip( const CookJob& job, const string& recordPath )
        {
            CookRecord record = CookRecord::make( job, uint64( 0x1234 ) );
            CookRecord loaded;
            if ( record.write( recordPath ) == false || CookRecord::read( recordPath, loaded ) == false )
                test::reportFailure( __FILE__, __LINE__, "could not write and read back the cook record " + recordPath );
            return loaded;
        }

        /** @brief 기록 @p record 가 새것이고 입력이 하나뿐인지 봅니다. */
        static void expectFreshSingleInput( const CookRecord& record, const CookInputKind kind )
        {
            SW_EXPECT_TRUE( record.isFresh( makeDesc() ) );
            SW_REQUIRE_EQUAL( size_t( 1 ), record._listInput.size() );
            SW_EXPECT_TRUE( record._listInput[0]._kind == kind );
        }

        /** @brief 원본을 바꾼 뒤 기록 @p record 가 낡았고, 바뀐 입력이 그 종류인지 봅니다. */
        static void expectStale( const CookRecord& record, const CookInputKind kind )
        {
            SW_EXPECT_FALSE( record.isFresh( makeDesc() ) );
            const CookInput* pChanged = record.findChangedInput();
            SW_REQUIRE_NOT_NULL( pChanged );
            SW_EXPECT_TRUE( pChanged->_kind == kind );
        }
    };
} // namespace

/**
 * @brief [CookRecordStalenessTest] 입력 종류마다 기록 → 원본 수정 → 낡음 판정이 맞다(리소스 · 디스크 파일 · 목록 · 없던 리소스)
 * @details 기록은 디스크에 썼다가 읽은 것으로 판정한다(다음 쿠킹과 같은 길). 종류 하나라도 다시 풀기가 어긋나면(예전 `findLooseFile` 추측) 낡음을 못 보거나 늘 낡는다.
 */
SW_TEST_CASE( CookRecordStalenessTest, EachInputKindGoesStaleWhenItsSourceChanges )
{
    using Internal = CookRecordStalenessTestInternal;
    const ScopedScratchDomain domain;
    const string              recordPath = test::makeScratchPath( "staleness.cookrec" );

    // 리소스: 리소스 id 로 읽은 파일의 내용이 바뀌면 낡는다.
    {
        const string diskPath = domain.makeDiskPath( "resource.txt" );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( diskPath, "one" ) );
        CookRecord record;
        {
            CookJob job( Internal::makeDesc() );
            string  text;
            SW_REQUIRE_TRUE( AssetIO::readTextAsset( ScopedScratchDomain::makeID( "resource.txt" ), text ) );
            record = Internal::roundTrip( job, recordPath );
        }
        Internal::expectFreshSingleInput( record, CookInputKind::Resource );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( diskPath, "two" ) );
        Internal::expectStale( record, CookInputKind::Resource );
    }

    // 디스크 파일: 리소스 루트 밖 파일의 내용이 바뀌면 낡는다.
    {
        const string folder   = test::makeScratchPath( "Disk" );
        const string diskPath = FileUtil::joinPath( folder, "disk.bin" );
        SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) && FileUtil::writeTextFile( diskPath, "one" ) );
        CookRecord record;
        {
            CookJob       job( Internal::makeDesc() );
            vector<uint8> bytes;
            SW_REQUIRE_TRUE( AssetIO::readBinaryFile( diskPath, bytes ) );
            record = Internal::roundTrip( job, recordPath );
        }
        Internal::expectFreshSingleInput( record, CookInputKind::DiskFile );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( diskPath, "two" ) );
        Internal::expectStale( record, CookInputKind::DiskFile );
    }

    // 목록: 읽은 파일은 그대로여도 같은 확장자의 파일이 하나 더 생기면 낡는다(폴더를 훑어 정한 출력).
    {
        const string folder = test::makeScratchPath( "Listing" );
        SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) && FileUtil::writeTextFile( FileUtil::joinPath( folder, "a.entry" ), "a" ) );
        CookRecord record;
        {
            CookJob        job( Internal::makeDesc() );
            vector<string> listFile;
            SW_REQUIRE_TRUE( CookJob::collectFiles( folder, ".entry", listFile ) );
            record = Internal::roundTrip( job, recordPath );
        }
        Internal::expectFreshSingleInput( record, CookInputKind::Listing );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "other.txt" ), "x" ) ); // 다른 확장자는 목록이 아니다
        SW_EXPECT_TRUE( record.isFresh( Internal::makeDesc() ) );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "b.entry" ), "b" ) );
        Internal::expectStale( record, CookInputKind::Listing );
    }

    // 없던 리소스: 찾지 못한 것도 해시 0 으로 기록되어, 그 파일이 생기면 낡는다.
    {
        CookRecord record;
        {
            CookJob job( Internal::makeDesc() );
            CookJob::allowMissingRead( ScopedScratchDomain::makeID( "late.txt" ) );
            record = Internal::roundTrip( job, recordPath );
        }
        Internal::expectFreshSingleInput( record, CookInputKind::Resource );
        SW_EXPECT_EQUAL( uint64( 0 ), record._listInput[0]._contentHash );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "late.txt" ), "late" ) );
        Internal::expectStale( record, CookInputKind::Resource );
    }
}
