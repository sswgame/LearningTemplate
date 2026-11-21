/**
 * @file TestMemorySnapshot.cpp
 * @brief 메모리 스냅숏 테스트 — 수집(가짜 캐시 · 추적기에 올린 가짜 자원), 비교(늘어남 · 새로 생김 · 사라짐), CSV(BOM · 이스케이프 · 한글 경로 · 숫자 칸), 전송 글 왕복, 정책 읽기.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Profiling/Memory/MemoryDiffStatus.h"
#include "DevTools/Profiling/Memory/MemorySnapshot.h"
#include "DevTools/Profiling/Memory/MemorySnapshotExport.h"
#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

using namespace sw;

namespace
{
    /** @brief 정해 둔 줄을 내는 캐시입니다(수집이 캐시 질의만 보는지 본다). */
    class FakeMemoryAssetCache final : public AssetCacheBase
    {
    public:
        FakeMemoryAssetCache()
            : AssetCacheBase{ CacheTracking::uncounted( "test cache without lookups" ) }
        {
        }

        const utf8* getAssetKindName() const override { return "FakeKind"; }
        bool        isCached( string_view /*relativePath*/ ) const override { return false; }
        size_t      getCachedCount() const override { return 2; }
        void        clear() override {}
        void        collectMemoryEntries( vector<AssetMemoryEntry>& outListEntry ) const override
        {
            outListEntry.push_back( AssetMemoryEntry{ "game/a.dds", 100, 4000, 3 } );
            outListEntry.push_back( AssetMemoryEntry{ "game/b, \"quoted\".mesh", 50, 0, 1 } );
        }
    };

    /** @brief 시트 @p kind 에 키 하나 · 크기 칸 하나만 채운 줄을 더합니다(비교 테스트). */
    void addTagRow( MemorySnapshot& snapshot, string_view tag, uint64 liveBytes )
    {
        snapshot.getSheet( MemorySheetKind::Tags )._listRow.push_back( MemoryRow{
            { MemoryCell{ string{ tag }, 0 }, MemoryCell{ string{}, liveBytes }, MemoryCell{}, MemoryCell{}, MemoryCell{}, MemoryCell{} }
        } );
    }

    /** @brief CSV 글을 줄(CRLF)로 나눕니다(BOM 은 뗀다). */
    vector<string> splitCSVLines( string_view text )
    {
        if ( text.substr( 0, CSVTable::kByteOrderMark.size() ) == CSVTable::kByteOrderMark )
            text = text.substr( CSVTable::kByteOrderMark.size() );
        vector<string> listLine;
        size_t         start = 0;
        while ( start < text.size() )
        {
            const size_t end = text.find( "\r\n", start );
            if ( end == string_view::npos )
                break;
            listLine.push_back( string{ text.substr( start, end - start ) } );
            start = end + 2;
        }
        return listLine;
    }
} // namespace

/**
 * @brief [MemorySnapshotTest] 수집은 캐시 질의 줄을 에셋 시트로, 추적기 자원을 정체 순 GPU 시트로 옮기고, 항목 수를 알리지 않은 캐시 계수는 같은 이름의 캐시에게 묻는다
 * @details 원천은 값 묶음이라 엔진 서비스 없이 가짜 캐시 · 추적기만으로 돈다. 없는 원천(프로파일러 · 씬)의 시트는 빈다.
 */
SW_TEST_CASE( MemorySnapshotTest, CollectReadsCacheEntriesAndTrackerResources )
{
    FakeMemoryAssetCache cache;
    RHIMemoryTracker     tracker;
    RHITextureDesc       desc{};
    desc._width     = 64;
    desc._height    = 32;
    desc._mipLevels = 1;
    desc._arraySize = 1;
    desc._format    = RHIFormat::R8G8B8A8_UNORM;
    tracker.recordTexture( RHITextureHandle{ 9 }, desc, 8192 );
    tracker.setLabel( RHIMemoryKey::makeTexture( RHITextureHandle{ 9 } ), "Albedo" );
    tracker.recordAllocation( RHIMemoryKey::makeBuffer( RHIBufferHandle{ 2 } ), RHIMemoryKind::Buffer, 1024 );

    MemorySnapshotSources sources{};
    sources._listAssetCache.push_back( &cache );
    sources._pGPUTracker    = &tracker;
    sources._gpuBackendName = "FakeRHI";
    CacheStatsSnapshot stats{};
    stats._name     = "FakeKind";
    stats._hitCount = 7;
    sources._listCacheStats.push_back( stats );

    const MemorySnapshot snapshot = sources.collect( "label", "local" );
    SW_EXPECT_TRUE( snapshot.getSheet( MemorySheetKind::Tags )._listRow.empty() );
    SW_EXPECT_TRUE( snapshot.getSheet( MemorySheetKind::Types )._listRow.empty() );

    const MemorySheet& assets = snapshot.getSheet( MemorySheetKind::Assets );
    SW_REQUIRE_EQUAL( size_t{ 2 }, assets._listRow.size() );
    SW_EXPECT_EQUAL( string( "FakeKind" ), assets._listRow[0]._listCell[0]._text );
    SW_EXPECT_EQUAL( string( "game/a.dds" ), assets._listRow[0]._listCell[1]._text );
    SW_EXPECT_EQUAL( 4100ull, assets._listRow[0]._listCell[2]._value ); // Total = CPU + GPU
    SW_EXPECT_EQUAL( 3ull, assets._listRow[0]._listCell[5]._value );

    const MemorySheet& gpu = snapshot.getSheet( MemorySheetKind::GPUResources );
    SW_REQUIRE_EQUAL( size_t{ 2 }, gpu._listRow.size() );
    SW_EXPECT_EQUAL( string( "Buffer 2" ), gpu._listRow[0]._listCell[0]._text ); // 키 공간 순(버퍼 먼저)
    SW_EXPECT_EQUAL( string( "Texture 9" ), gpu._listRow[1]._listCell[0]._text );
    SW_EXPECT_EQUAL( string( "Albedo" ), gpu._listRow[1]._listCell[1]._text );
    SW_EXPECT_EQUAL( 64ull, gpu._listRow[1]._listCell[4]._value );
    SW_EXPECT_EQUAL( 8192ull, gpu._listRow[1]._listCell[8]._value );
    SW_EXPECT_EQUAL( 9216ull, snapshot._gpuLiveBytes );
    SW_EXPECT_TRUE( StringUtil::startsWith( snapshot._gpuDescription, "FakeRHI" ) );

    const MemorySheet& caches = snapshot.getSheet( MemorySheetKind::Caches );
    SW_REQUIRE_EQUAL( size_t{ 1 }, caches._listRow.size() );
    SW_EXPECT_EQUAL( 2ull, caches._listRow[0]._listCell[1]._value ); // 항목 수를 모르는 칸 → 캐시의 getCachedCount
    SW_EXPECT_EQUAL( 7ull, caches._listRow[0]._listCell[2]._value );
}

/**
 * @brief [MemorySnapshotTest] 비교는 키로 줄을 맞춰 늘어난 것부터 내고 새로 생김 · 늘어남 · 줄어듦 · 사라짐 · 같음을 가르며, 같은 키의 줄은 더한다
 */
SW_TEST_CASE( MemorySnapshotTest, CompareSortsGrownFirstAndMarksNewAndGone )
{
    MemorySnapshot before;
    addTagRow( before, "Mesh", 1000 );
    addTagRow( before, "Audio", 500 );
    addTagRow( before, "UI", 300 );
    addTagRow( before, "Gone", 200 );
    MemorySnapshot after;
    addTagRow( after, "Mesh", 1500 );
    addTagRow( after, "Audio", 400 );
    addTagRow( after, "UI", 300 );
    addTagRow( after, "New", 100 );
    addTagRow( after, "New", 50 ); // 같은 키는 더한다

    const MemorySheetDiff diff = MemorySheetDiff::compare( before, after, MemorySheetKind::Tags );
    SW_REQUIRE_EQUAL( size_t{ 5 }, diff._listRow.size() );
    SW_EXPECT_EQUAL( string( "Mesh" ), diff._listRow[0]._listKeyText[0] );
    SW_EXPECT_TRUE( diff._listRow[0]._status == MemoryDiffStatus::Grown );
    SW_EXPECT_EQUAL( int64{ 500 }, diff._listRow[0]._delta );
    SW_EXPECT_EQUAL( string( "New" ), diff._listRow[1]._listKeyText[0] );
    SW_EXPECT_TRUE( diff._listRow[1]._status == MemoryDiffStatus::New );
    SW_EXPECT_EQUAL( 150ull, diff._listRow[1]._after );
    SW_EXPECT_TRUE( diff._listRow[2]._status == MemoryDiffStatus::Same );
    SW_EXPECT_TRUE( diff._listRow[3]._status == MemoryDiffStatus::Shrunk );
    SW_EXPECT_EQUAL( string( "Gone" ), diff._listRow[4]._listKeyText[0] );
    SW_EXPECT_TRUE( diff._listRow[4]._status == MemoryDiffStatus::Gone );
    SW_EXPECT_EQUAL( int64{ -200 }, diff._listRow[4]._delta );
    SW_EXPECT_EQUAL( int64{ 500 + 150 - 100 - 200 }, diff._totalDelta );
}

/**
 * @brief [MemorySnapshotTest] CSV 는 BOM · 머리말 · CRLF 이고, 쉼표 · 따옴표 · 줄바꿈 칸은 따옴표로 감싸 안의 따옴표를 두 번 쓴다
 */
SW_TEST_CASE( MemorySnapshotTest, CSVEscapesCellsAndStartsWithBOM )
{
    CSVTable table{};
    table._listHeader = { "Name", "Note" };
    table._listRow.push_back( vector<string>{ "plain", "a,b" } );
    table._listRow.push_back( vector<string>{ "say \"hi\"", "line1\nline2" } );
    const string text = table.writeText();
    SW_EXPECT_TRUE( StringUtil::startsWith( text, CSVTable::kByteOrderMark ) );
    SW_EXPECT_EQUAL( string( "\xEF\xBB\xBFName,Note\r\nplain,\"a,b\"\r\n\"say \"\"hi\"\"\",\"line1\nline2\"\r\n" ), text );
}

/**
 * @brief [MemorySnapshotTest] 시트 표는 바이트 칸을 원값과 MB 두 칸으로 내고, 한글 폴더에 쓴 파일을 다시 읽으면 머리말 · 행 수가 같다
 */
SW_TEST_CASE( MemorySnapshotTest, SheetTableSplitsBytesAndWritesUnderKoreanFolder )
{
    FakeMemoryAssetCache  cache;
    MemorySnapshotSources sources{};
    sources._listAssetCache.push_back( &cache );
    const MemorySnapshot snapshot = sources.collect( "label", "local" );

    const CSVTable table = MemorySnapshotExport::makeSheetTable( snapshot, MemorySheetKind::Assets );
    // Kind · Path · Total(bytes · MB) · CPU(bytes · MB) · GPU(bytes · MB) · References
    SW_REQUIRE_EQUAL( size_t{ 9 }, table._listHeader.size() );
    SW_EXPECT_EQUAL( string( "Total (bytes)" ), table._listHeader[2] );
    SW_EXPECT_EQUAL( string( "Total (MB)" ), table._listHeader[3] );
    SW_REQUIRE_EQUAL( size_t{ 2 }, table._listRow.size() );
    SW_EXPECT_EQUAL( string( "4100" ), table._listRow[0][2] );
    SW_EXPECT_EQUAL( string( "0.004" ), table._listRow[0][3] );
    SW_EXPECT_EQUAL( string( "1.000" ), MemorySnapshotExport::formatMegabytes( 1048576 ) );

    const string folder = test::makeScratchPath( "메모리 내보내기" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    const expected<MemoryExportResult> result = MemorySnapshotExport::writeSnapshot( snapshot, folder );
    SW_REQUIRE_TRUE( result.has_value() );
    SW_EXPECT_EQUAL( size_t{ 1 + kMemorySheetCount }, result->_listFile.size() );

    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( folder, "Assets.csv" ), text ) );
    const vector<string> listLine = splitCSVLines( text );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listLine.size() );
    SW_EXPECT_EQUAL( string( "Kind,Path,Total (bytes),Total (MB),CPU (bytes),CPU (MB),GPU (bytes),GPU (MB),References" ), listLine[0] );
    SW_EXPECT_TRUE( StringUtil::contains( listLine[2], "\"game/b, \"\"quoted\"\".mesh\"" ) );

    vector<uint8> listByte;
    SW_REQUIRE_TRUE( FileUtil::readFile( FileUtil::joinPath( folder, "Tags.csv" ), listByte ) );
    SW_REQUIRE_TRUE( listByte.size() >= 3 );
    SW_EXPECT_TRUE( listByte[0] == 0xEF && listByte[1] == 0xBB && listByte[2] == 0xBF ); // readTextFile 은 BOM 을 떼므로 바이트로 본다
}

/**
 * @brief [MemorySnapshotTest] 전송 글(원격 `memory.snapshot` 의 답)은 탭 · 줄바꿈 · 역슬래시가 든 글과 숫자를 그대로 되돌리고, 칸 수가 틀린 글은 거절한다
 */
SW_TEST_CASE( MemorySnapshotTest, TextRoundTripsAndRejectsBadRows )
{
    FakeMemoryAssetCache  cache;
    MemorySnapshotSources sources{};
    sources._listAssetCache.push_back( &cache );
    MemorySnapshot snapshot = sources.collect( "tab\there", "remote\\host\nline" );
    addTagRow( snapshot, "Mesh", 123456789012ull );

    MemorySnapshot read;
    SW_REQUIRE_TRUE( MemorySnapshot::readText( snapshot.writeText(), read ) );
    SW_EXPECT_EQUAL( snapshot._label, read._label );
    SW_EXPECT_EQUAL( snapshot._source, read._source );
    SW_REQUIRE_EQUAL( size_t{ 2 }, read.getSheet( MemorySheetKind::Assets )._listRow.size() );
    SW_EXPECT_EQUAL( string( "game/b, \"quoted\".mesh" ), read.getSheet( MemorySheetKind::Assets )._listRow[1]._listCell[1]._text );
    SW_EXPECT_EQUAL( 123456789012ull, read.getSheet( MemorySheetKind::Tags )._listRow[0]._listCell[1]._value );

    string       broken = snapshot.writeText(); // Tags 시트 첫 줄 자리에 칸 수가 틀린 줄을 넣는다
    const size_t tagsAt = broken.find( "sheet\tTags\t1\n" );
    SW_REQUIRE_TRUE( tagsAt != string::npos );
    broken.insert( tagsAt + string_view( "sheet\tTags\t1\n" ).size(), "Mesh\t1\n" );
    SW_EXPECT_FALSE( MemorySnapshot::readText( broken, read ) );
    SW_EXPECT_FALSE( MemorySnapshot::readText( "not a snapshot", read ) );
}

/**
 * @brief [MemorySnapshotTest] 정책은 두 칸이 모두 있어야 하고, 내보내기 폴더는 폴더 이름 하나(구분자 없음), 사이트 상한은 1 이상이다
 */
SW_TEST_CASE( MemorySnapshotTest, PolicyNeedsBothKeysInRange )
{
    SW_EXPECT_TRUE( MemorySnapshotPolicy::parse( R"({ "_exportFolder": "MemorySnapshots", "_allocationSiteLimit": 8 })", "test" ).has_value() );
    SW_EXPECT_LOG( "JSON.MissingMember" ); // 필수 칸 없음 — JSON 읽기가 오류 로그를 남긴다
    SW_EXPECT_FALSE( MemorySnapshotPolicy::parse( R"({ "_exportFolder": "MemorySnapshots" })", "test" ).has_value() );
    SW_EXPECT_FALSE( MemorySnapshotPolicy::parse( R"({ "_exportFolder": "a/b", "_allocationSiteLimit": 8 })", "test" ).has_value() );
    SW_EXPECT_FALSE( MemorySnapshotPolicy::parse( R"({ "_exportFolder": "MemorySnapshots", "_allocationSiteLimit": 0 })", "test" ).has_value() );
}
