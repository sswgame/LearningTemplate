#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/CookJob.h"

// CookStoreTest — 쿠킹 출력 하나를 기록(.cookrec)과 DDC 로 만드는 저장소: 입력이 그대로면 굽지 않고 다시 쓰고, 입력 · 쿠커 버전이 바뀌면 다시 굽고,
// 입력을 기록하지 않은 작업은 실패, 요청에서 빠진 옛 출력만 지운다. 쿠커는 가짜다 — 입력 파일을 디스크 입구(`AssetIO::readBinaryFile`)로 읽어 접두어를 붙여 쓴다(nogpu).

using namespace sw;

namespace
{
    struct CookFixture
    {
        string    _root{ test::makeScratchPath( "Cook" ) }; ///< 작업 폴더 기준 상대 경로 — 디스크 읽기는 이것도 절대 경로로 기록해야 한다
        string    _inputPath{ FileUtil::joinPath( _root, "Source/a.txt" ) };
        string    _cookedDir{ FileUtil::joinPath( _root, "Cooked" ) };
        CookStore _store{ FileUtil::joinPath( _root, "Record" ), FileUtil::joinPath( _root, "DDC" ) };
        int32     _bakeCount{ 0 };

        /** @brief 같은 기록 · DDC 폴더의 새 저장소입니다(다음 쿠킹 실행). */
        CookStore makeNextRun() const { return CookStore{ FileUtil::joinPath( _root, "Record" ), FileUtil::joinPath( _root, "DDC" ) }; }

        string makeOutputPath( string_view outputID ) const { return FileUtil::joinPath( _cookedDir, outputID ); }

        /** @brief 입력 파일을 읽어 `baked:` 를 붙여 씁니다. */
        CookOutcome cook( const CookStore& store, const CookJobDesc& desc, string_view outputID )
        {
            const string outputPath = makeOutputPath( outputID );
            return store.cookFile( desc, outputID, outputPath, [&]
            {
                ++_bakeCount;
                vector<uint8> bytes;
                if ( AssetIO::readBinaryFile( _inputPath, bytes ) == false )
                    return CookStep::Failed;
                const string text = "baked:" + string( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
                return FileUtil::ensureParentDirectoryExists( outputPath ) && FileUtil::writeTextFile( outputPath, text ) ? CookStep::Written : CookStep::Failed;
            } );
        }

        string readOutput( string_view outputID ) const
        {
            string text;
            (void)FileUtil::readTextFile( makeOutputPath( outputID ), text );
            return text;
        }
    };

    const CookJobDesc kDesc{ ._cookerName = "Fake", ._cookerVersion = 1, ._target = "Test", ._settingsHash = 0 };
} // namespace

/**
 * @brief [CookStoreTest] 입력이 그대로면 두 번째 쿠킹은 굽지 않고, 지워진 출력은 DDC 에서 같은 바이트로 되살린다
 */
SW_TEST_CASE( CookStoreTest, ReusesWhileInputsAreUnchanged )
{
    CookFixture fixture;
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( fixture._inputPath ) && FileUtil::writeTextFile( fixture._inputPath, "one" ) );

    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, kDesc, "x.bin" ) );
    SW_EXPECT_EQUAL( string( "baked:one" ), fixture.readOutput( "x.bin" ) );
    SW_EXPECT_TRUE( FileUtil::exists( fixture._store.makeRecordPath( "x.bin" ) ) );

    const CookStore nextRun = fixture.makeNextRun();
    SW_EXPECT_EQUAL( CookOutcome::Reused, fixture.cook( nextRun, kDesc, "x.bin" ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( fixture.makeOutputPath( "x.bin" ) ) );
    SW_EXPECT_EQUAL( CookOutcome::Reused, fixture.cook( nextRun, kDesc, "x.bin" ) );
    SW_EXPECT_EQUAL( string( "baked:one" ), fixture.readOutput( "x.bin" ) );
    SW_EXPECT_EQUAL( 1, fixture._bakeCount );
    SW_EXPECT_EQUAL( 2u, nextRun.getTally()._reusedCount );
}

/**
 * @brief [CookStoreTest] 입력 내용이나 쿠커 버전이 바뀌면 다시 굽는다
 */
SW_TEST_CASE( CookStoreTest, CooksAgainWhenInputOrVersionChanges )
{
    CookFixture fixture;
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( fixture._inputPath ) && FileUtil::writeTextFile( fixture._inputPath, "one" ) );
    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, kDesc, "x.bin" ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( fixture._inputPath, "two" ) );
    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, kDesc, "x.bin" ) );
    SW_EXPECT_EQUAL( string( "baked:two" ), fixture.readOutput( "x.bin" ) );

    CookJobDesc nextVersion    = kDesc;
    nextVersion._cookerVersion = 2;
    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, nextVersion, "x.bin" ) );
    SW_EXPECT_EQUAL( CookOutcome::Reused, fixture.cook( fixture._store, nextVersion, "x.bin" ) );
    SW_EXPECT_EQUAL( 3, fixture._bakeCount );
}

/**
 * @brief [CookStoreTest] 입력을 기록하지 않은 작업은 실패이고 기록을 남기지 않는다. 건너뛴 출력도 기록이 없다
 */
SW_TEST_CASE( CookStoreTest, RejectsJobsWithoutInputs )
{
    CookFixture  fixture;
    const string outputPath = fixture.makeOutputPath( "none.bin" );
    SW_EXPECT_LOG( "Cook.NoInputRecorded" );
    const CookOutcome outcome = fixture._store.cookFile( kDesc, "none.bin", outputPath, [&]
    { return FileUtil::ensureParentDirectoryExists( outputPath ) && FileUtil::writeTextFile( outputPath, "x" ) ? CookStep::Written : CookStep::Failed; } );
    SW_EXPECT_EQUAL( CookOutcome::Failed, outcome );
    SW_EXPECT_FALSE( FileUtil::exists( fixture._store.makeRecordPath( "none.bin" ) ) );

    SW_EXPECT_EQUAL( CookOutcome::Skipped, fixture._store.cookFile( kDesc, "skip.bin", fixture.makeOutputPath( "skip.bin" ), []
    { return CookStep::Skipped; } ) );
    SW_EXPECT_FALSE( FileUtil::exists( fixture._store.makeRecordPath( "skip.bin" ) ) );
    SW_EXPECT_EQUAL( 1u, fixture._store.getTally()._failedCount );
}

/**
 * @brief [CookStoreTest] 이번 실행이 요청하지 않은 옛 출력만 지운다 — 건너뛴 출력은 요청이 아니고, 기록 없는 파일은 건드리지 않는다
 */
SW_TEST_CASE( CookStoreTest, RemovesOnlyUnrequestedRecordedOutputs )
{
    CookFixture fixture;
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( fixture._inputPath ) && FileUtil::writeTextFile( fixture._inputPath, "one" ) );
    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, kDesc, "keep.bin" ) );
    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, kDesc, "sub/gone.bin" ) );
    SW_EXPECT_EQUAL( CookOutcome::Cooked, fixture.cook( fixture._store, kDesc, "skipped.bin" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( fixture.makeOutputPath( "authored.bin" ), "hand" ) );

    const CookStore nextRun = fixture.makeNextRun();
    SW_EXPECT_EQUAL( CookOutcome::Reused, fixture.cook( nextRun, kDesc, "keep.bin" ) );
    CookJobDesc noLongerCooked   = kDesc; // 설정이 바뀌어 기록이 낡아야 쿠커가 건너뛸 기회를 얻는다 — 입력 · 설정이 그대로면 기록이 이긴다
    noLongerCooked._settingsHash = 1;
    SW_EXPECT_EQUAL( CookOutcome::Skipped, nextRun.cookFile( noLongerCooked, "skipped.bin", fixture.makeOutputPath( "skipped.bin" ), []
    { return CookStep::Skipped; } ) );

    uint32 failedCount = 0;
    SW_EXPECT_EQUAL( 2u, nextRun.removeUnrequestedOutputs( fixture._cookedDir, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );
    SW_EXPECT_TRUE( FileUtil::exists( fixture.makeOutputPath( "keep.bin" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( fixture.makeOutputPath( "sub/gone.bin" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( fixture.makeOutputPath( "skipped.bin" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( fixture.makeOutputPath( "authored.bin" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( nextRun.makeRecordPath( "sub/gone.bin" ) ) ); // 기록은 남는다 — 소스가 돌아오면 DDC 로 다시 쓴다
}

/**
 * @brief [CookRecordTest] 쓴 기록을 읽으면 키 · 출력 해시 · 파일 입력 · 목록 입력이 같고, 머리 줄이 다르거나 줄이 깨진 기록은 거절한다
 */
SW_TEST_CASE( CookRecordTest, RoundTripsAndRejectsBrokenRecords )
{
    const string root = test::makeScratchPath( "Record" );
    CookRecord   record;
    record._key        = 0x0123456789abcdefull;
    record._outputHash = 0xfedcba9876543210ull;
    record._listInput.push_back( CookInput{ ._kind = CookInputKind::File, ._assetID = "game/a b/x.xml", ._extension = {}, ._contentHash = 7 } );
    record._listInput.push_back( CookInput{ ._kind = CookInputKind::Listing, ._assetID = "game/folder", ._extension = ".meta", ._contentHash = 9 } );
    const string path = FileUtil::joinPath( root, "x.cookrec" );
    SW_REQUIRE_TRUE( record.write( path ) );

    CookRecord readBack;
    SW_REQUIRE_TRUE( CookRecord::read( path, readBack ) );
    SW_EXPECT_EQUAL( record._key, readBack._key );
    SW_EXPECT_EQUAL( record._outputHash, readBack._outputHash );
    SW_REQUIRE_EQUAL( size_t( 2 ), readBack._listInput.size() );
    SW_EXPECT_TRUE( record._listInput[0] == readBack._listInput[0] );
    SW_EXPECT_TRUE( record._listInput[1] == readBack._listInput[1] );

    SW_EXPECT_FALSE( CookRecord::read( FileUtil::joinPath( root, "missing.cookrec" ), readBack ) ); // 처음 굽는 작업 — 조용히 false

    SW_EXPECT_LOG( "Cook.RecordInvalid" );
    const string oldFormat = FileUtil::joinPath( root, "old.cookrec" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( oldFormat, "SWCOOKREC 1\nkey 0123456789abcdef\noutput 0123456789abcdef\n" ) );
    SW_EXPECT_FALSE( CookRecord::read( oldFormat, readBack ) );
    const string shortHash = FileUtil::joinPath( root, "short.cookrec" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( shortHash, string( CookRecord::kHeader ) + "\nkey 0123\noutput 0123456789abcdef\n" ) );
    SW_EXPECT_FALSE( CookRecord::read( shortHash, readBack ) );
    SW_EXPECT_EQUAL( 0ull, readBack._key ); // 거절한 기록은 빈 값으로 돌려준다
}
