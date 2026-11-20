/**
 * @file TestMemoryViewModel.cpp
 * @brief Memory 창 ViewModel 테스트 — 시트 표의 정렬(숫자 칸은 값, 글 칸은 글) · 이름 거르기 · 형식 거르기, 비교 표의 칸과 늘어난 것 순, 스냅숏 고르기 · 비교.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Memory/Model/MemoryViewModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct MemoryViewModelTestUtil
    {
        /** @brief 에셋 시트에 줄 하나를 더합니다(Kind · Path · Total · CPU · GPU · References). */
        static void addAsset( MemorySnapshot& snapshot, string_view kind, string_view path, uint64 cpuBytes, uint64 gpuBytes )
        {
            snapshot.getSheet( MemorySheetKind::Assets )
                ._listRow.push_back( MemoryRow{
                    { MemoryCell{ string{ kind }, 0 }, MemoryCell{ string{ path }, 0 }, MemoryCell{ string{}, cpuBytes + gpuBytes },
                     MemoryCell{ string{}, cpuBytes }, MemoryCell{ string{}, gpuBytes }, MemoryCell{ string{}, 1 } }
            } );
        }

        static MemorySnapshot makeSnapshot( string_view label )
        {
            MemorySnapshot snapshot;
            snapshot._label  = string{ label };
            snapshot._source = "local";
            addAsset( snapshot, "Texture", "game/rock.dds", 100, 9000 );
            addAsset( snapshot, "Mesh", "game/rock.mesh", 500, 2000 );
            addAsset( snapshot, "Texture", "game/sky.dds", 100, 300 );
            return snapshot;
        }
    };
} // namespace

/**
 * @brief [MemoryViewModelTest] 시트 표는 숫자 칸을 값으로 정렬하고(글 순이면 "9 KB" 가 "10 KB" 앞에 온다), 이름 · 형식 거르기를 함께 걸며, 에셋 줄은 경로를 들고 있다
 */
SW_TEST_CASE( MemoryViewModelTest, SheetTableSortsByValueAndFilters )
{
    const MemorySnapshot      snapshot = MemoryViewModelTestUtil::makeSnapshot( "a" );
    vector<MemoryTableColumn> listColumn;
    vector<MemoryTableRow>    listRow;

    MemoryViewModel::buildSheetTable( snapshot, MemorySheetKind::Assets, 2, true, "", "", listColumn, listRow );
    SW_REQUIRE_EQUAL( size_t{ 6 }, listColumn.size() );
    SW_EXPECT_TRUE( listColumn[2]._bNumeric != SW_FALSE );
    SW_EXPECT_TRUE( listColumn[1]._bNumeric == SW_FALSE );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listRow.size() );
    SW_EXPECT_EQUAL( string( "game/rock.dds" ), listRow[0]._assetPath );
    SW_EXPECT_EQUAL( string( "game/rock.mesh" ), listRow[1]._assetPath );
    SW_EXPECT_EQUAL( string( "game/sky.dds" ), listRow[2]._assetPath );

    MemoryViewModel::buildSheetTable( snapshot, MemorySheetKind::Assets, 1, false, "", "", listColumn, listRow ); // 경로 글 오름차순
    SW_EXPECT_EQUAL( string( "game/rock.dds" ), listRow[0]._listCell[1] );
    SW_EXPECT_EQUAL( string( "game/sky.dds" ), listRow[2]._listCell[1] );

    MemoryViewModel::buildSheetTable( snapshot, MemorySheetKind::Assets, 2, true, "ROCK", "", listColumn, listRow ); // 이름 거르기는 대소문자 무시
    SW_EXPECT_EQUAL( size_t{ 2 }, listRow.size() );
    MemoryViewModel::buildSheetTable( snapshot, MemorySheetKind::Assets, 2, true, "rock", "Texture", listColumn, listRow ); // 형식 거르기와 함께
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRow.size() );
    SW_EXPECT_EQUAL( string( "game/rock.dds" ), listRow[0]._assetPath );
    SW_EXPECT_EQUAL( string( "8.89 KB" ), listRow[0]._listCell[2] ); // 9100 바이트
}

/**
 * @brief [MemoryViewModelTest] 비교 표는 키 칸 · Before · After · Delta · Status 이고 늘어난 것이 먼저이며, 에셋 비교 줄도 경로를 들어 콘텐츠 브라우저로 갈 수 있다
 */
SW_TEST_CASE( MemoryViewModelTest, DiffTableListsGrownFirstWithStatus )
{
    MemorySnapshot before = MemoryViewModelTestUtil::makeSnapshot( "a" );
    MemorySnapshot after  = MemoryViewModelTestUtil::makeSnapshot( "b" );
    MemoryViewModelTestUtil::addAsset( after, "Mesh", "game/tree.mesh", 4000, 4000 );

    const MemorySheetDiff     diff = MemorySheetDiff::compare( before, after, MemorySheetKind::Assets );
    vector<MemoryTableColumn> listColumn;
    vector<MemoryTableRow>    listRow;
    MemoryViewModel::buildDiffTable( diff, 4, true, "", "", listColumn, listRow ); // Kind · Path · Before · After · Delta(4) · Status
    SW_REQUIRE_EQUAL( size_t{ 6 }, listColumn.size() );
    SW_EXPECT_EQUAL( string( "Delta" ), listColumn[4]._name );
    SW_REQUIRE_EQUAL( size_t{ 4 }, listRow.size() );
    SW_EXPECT_EQUAL( string( "game/tree.mesh" ), listRow[0]._assetPath );
    SW_EXPECT_TRUE( listRow[0]._status == MemoryDiffStatus::New );
    SW_EXPECT_EQUAL( string( "+7.81 KB" ), listRow[0]._listCell[4] );
    SW_EXPECT_EQUAL( string( "New" ), listRow[0]._listCell[5] );
    SW_EXPECT_TRUE( listRow[1]._status == MemoryDiffStatus::Same );

    MemoryViewModel::buildDiffTable( diff, 4, true, "", "Texture", listColumn, listRow ); // 거르기 칸(Kind)이 키라 비교에도 걸린다
    SW_EXPECT_EQUAL( size_t{ 2 }, listRow.size() );
}

/**
 * @brief [MemoryViewModelTest] 앞 스냅숏과 비교하면 표가 비교 표가 되고, 보는 스냅숏을 바꿔도 비교의 앞은 그대로이며, 상한을 넘으면 가장 오래된 스냅숏을 버린다
 */
SW_TEST_CASE( MemoryViewModelTest, CompareWithPreviousSwitchesToDiffTable )
{
    MemoryViewModel viewModel;
    MemoryViewState state;
    viewModel.addSnapshot( MemoryViewModelTestUtil::makeSnapshot( "a" ) );
    MemorySnapshot after = MemoryViewModelTestUtil::makeSnapshot( "b" );
    MemoryViewModelTestUtil::addAsset( after, "Mesh", "game/tree.mesh", 10, 10 );
    viewModel.addSnapshot( std::move( after ) );
    viewModel.selectSheet( MemorySheetKind::Assets );
    viewModel.gather( state );
    SW_EXPECT_EQUAL( 1, state._currentIndex );
    SW_EXPECT_TRUE( state._bCompareMode == SW_FALSE );
    SW_EXPECT_EQUAL( 4u, viewModel.getVisibleRowCount() );

    viewModel.compareWithPrevious();
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._bCompareMode != SW_FALSE );
    SW_EXPECT_EQUAL( 0, state._compareIndex );
    SW_REQUIRE_EQUAL( size_t{ 4 }, state._listRow.size() );
    SW_EXPECT_TRUE( state._listRow[0]._status == MemoryDiffStatus::New );
    SW_EXPECT_EQUAL( 4u, state._sortColumn ); // 키 둘(Kind · Path) + Before · After 다음이 Delta

    viewModel.selectSnapshot( 0 ); // 앞과 같아지면 비교를 끈다
    viewModel.gather( state );
    SW_EXPECT_TRUE( state._bCompareMode == SW_FALSE );

    for ( uint32 index = 0; index < MemoryViewModel::kMaxSnapshotCount; ++index )
    {
        viewModel.addSnapshot( MemoryViewModelTestUtil::makeSnapshot( "more" ) );
    }
    SW_EXPECT_EQUAL( MemoryViewModel::kMaxSnapshotCount, viewModel.getSnapshotCount() );
}
