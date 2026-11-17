/**
 * @file TestEditorAssetIndexCost.cpp
 * @brief 에디터 에셋 색인의 비용을 잽니다 — 합성 트리 크기별 전체 스캔과 파일 하나 바뀜(UI 스레드)의 ns. 에디터 에셋 색인 설계(DesignBacklog "에디터 에셋 색인")의 판정 자료입니다.
 *
 * 재는 법(통합 담당): `App --test --test-filter=EditorAssetIndexTest.ScanCostBySize` 를 Release 로 5 번 돌려 로그 `EditorAssetIndexTest.ScanCost` 의
 * 크기별 중앙값을 본다. 옛 참조 역색인 값은 R0 커밋(`EditorReferenceIndexTest.ScanCostBySize`)에서 잰다. 판정: 바뀜 1 ms 미만이 목표,
 * 10 만 트리 전체 스캔이 2 초를 넘으면 디스크 캐시(R3).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorAssetIndexCostTestInternal
    {
        /** @brief 폴더 하나에 담는 파일 수입니다(실제 콘텐츠 폴더와 비슷하게 넓게 나눈다). */
        static constexpr uint32 kFilePerFolder = 100;
        /** @brief 파일 하나가 적는 참조 수입니다(설계의 평균 4). */
        static constexpr uint32 kReferencePerFile = 4;

        static string makeAssetID( uint32 fileIndex )
        {
            StringBuilder<constant::buffer::kMax256> builder;
            builder.appendFormat( "game/bench/d%#/f%#.prefab.xml", fileIndex / kFilePerFolder, fileIndex );
            return string( builder.view() );
        }

        /** @brief 파일 @p fileIndex 의 글입니다 — 앞 파일 넷을 경로로 적는다(맨 앞 파일들은 뒤를 감아 적는다). */
        static string makeText( uint32 fileIndex, uint32 fileCount )
        {
            string text = "<Prefab formatVersion=\"0\">\n";
            for ( uint32 referenceIndex = 1; referenceIndex <= kReferencePerFile; ++referenceIndex )
            {
                const uint32 target = ( fileIndex + fileCount - referenceIndex ) % fileCount;
                text += "  <Child _prefab=\"" + makeAssetID( target ) + "\"/>\n";
            }
            return text + "</Prefab>\n";
        }

        /** @brief @p root 아래 @p fileCount 개의 합성 트리를 씁니다. */
        static bool writeTree( const string& root, uint32 fileCount )
        {
            for ( uint32 fileIndex = 0; fileIndex < fileCount; ++fileIndex )
            {
                const string absPath = FileUtil::joinPath( root, makeAssetID( fileIndex ) );
                if ( ( fileIndex % kFilePerFolder == 0 && FileUtil::ensureParentDirectoryExists( absPath ) == false ) ||
                     FileUtil::writeTextFile( absPath, makeText( fileIndex, fileCount ) ) == false )
                    return false;
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [EditorAssetIndexTest] 합성 트리(1 만 · 10 만 파일, 참조 평균 4)의 전체 스캔과 파일 하나 바뀜의 ns 를 로그로 남깁니다. 시간 문턱은 두지 않는다 — 판정은 통합 담당의 반복 측정이다.
 * @details 전체 스캔은 작업 관리자 없이(호출 스레드 하나) 잰다 — 에디터는 워커로 나누므로 기동 시간의 상한이다. 파일 하나 바뀜은 UI 스레드가 하는 일 그대로다.
 */
SW_TEST_CASE( EditorAssetIndexTest, ScanCostBySize, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다" )
{
    using Internal                              = EditorAssetIndexCostTestInternal;
    static constexpr uint32 kArrFileCount[]     = { 10000, 100000 };
    static constexpr uint32 kRefreshRepeatCount = 16;

    for ( const uint32 fileCount : kArrFileCount )
    {
        StringBuilder<constant::buffer::kMax64> folderName;
        folderName.appendFormat( "tree%#", fileCount );
        const string root = sw::test::makeScratchPath( folderName.view() );
        SW_REQUIRE_TRUE( Internal::writeTree( root, fileCount ) );

        EditorAssetIndexData data{};
        const int64          scanStart = MonotonicClock::nowNanoseconds();
        EditorAssetIndex::scan( root, data, nullptr );
        const int64 scanNanoseconds = MonotonicClock::nowNanoseconds() - scanStart;
        SW_EXPECT_EQUAL( fileCount, data._scannedFileCount );
        SW_EXPECT_EQUAL( static_cast<size_t>( fileCount ), data._mapReferrer.size() ); // 파일마다 넷이 가리킨다

        EditorAssetIndex index;
        index.assign( std::move( data ) );
        const string changedID = Internal::makeAssetID( fileCount / 2 );
        // 바뀐 파일을 실제로 다시 읽게 한다 — 크기 · 시각이 같으면 열지 않으므로 줄 끝 공백 수를 바꿔 크기를 바꾼다.
        int64 refreshNanoseconds = 0;
        for ( uint32 repeat = 0; repeat < kRefreshRepeatCount; ++repeat )
        {
            SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( root, changedID ), Internal::makeText( fileCount / 2, fileCount ) + string( repeat + 1, ' ' ) ) );
            const int64 refreshStart = MonotonicClock::nowNanoseconds();
            index.applyChanges( root, { changedID } );
            refreshNanoseconds += MonotonicClock::nowNanoseconds() - refreshStart;
        }
        refreshNanoseconds /= kRefreshRepeatCount;
        SW_EXPECT_EQUAL( Internal::kReferencePerFile, index.countReferrerFiles( Internal::makeAssetID( fileCount / 2 - 1 ) ) );

        SW_LOG_INFO_ID( "EditorAssetIndexTest.ScanCost", "Asset index: %# files -> full scan %# ns, one changed file %# ns (UI thread)", fileCount, scanNanoseconds, refreshNanoseconds );
    }
}
