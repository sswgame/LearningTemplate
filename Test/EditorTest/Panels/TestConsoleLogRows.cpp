#include "pch.h"

#include "Editor/Panels/ConsoleLogRows.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    LogEntry makeEntry( const utf8* pMessage, LogLevel level, const utf8* pTimeStamp )
    {
        LogEntry entry{};
        entry._tag       = "Engine";
        entry._caller    = "Probe";
        entry._message   = pMessage;
        entry._timeStamp = pTimeStamp;
        entry._level     = level;
        return entry;
    }
} // namespace

/**
 * @brief [ConsoleLogRowsTest] 접기는 떨어져 있는 같은 줄도 처음 나온 자리에 모으고, 시각이 달라도 같은 줄로 본다
 */
SW_TEST_CASE( ConsoleLogRowsTest, CollapseMergesEqualLinesAtFirstPosition )
{
    const vector<LogEntry> listEntry = {
        makeEntry( "hello", LogLevel::Info, "00:01" ),
        makeEntry( "other", LogLevel::Info, "00:02" ),
        makeEntry( "hello", LogLevel::Info, "00:03" ),
        makeEntry( "hello", LogLevel::Warning, "00:04" ),
    };
    vector<const LogEntry*> listVisible;
    for ( const LogEntry& entry : listEntry )
    {
        listVisible.push_back( &entry );
    }

    vector<ConsoleLogRow> listRow;
    ConsoleLogRows::populate( listVisible, false, listRow );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listRow.size() );

    ConsoleLogRows::populate( listVisible, true, listRow );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listRow.size() );
    SW_EXPECT_EQUAL( &listEntry[0], listRow[0]._pEntry );
    SW_EXPECT_EQUAL( 2u, listRow[0]._repeatCount );
    SW_EXPECT_EQUAL( &listEntry[1], listRow[1]._pEntry );
    SW_EXPECT_EQUAL( 1u, listRow[1]._repeatCount );
    SW_EXPECT_EQUAL( &listEntry[3], listRow[2]._pEntry ); // 수준이 다르면 다른 줄이다
}

/**
 * @brief [ConsoleLogRowsTest] 맨 아래에 붙어 있을 때만 따라간다
 */
SW_TEST_CASE( ConsoleLogRowsTest, FollowsOnlyWhenAtBottom )
{
    SW_EXPECT_TRUE( ConsoleLogRows::isScrolledToBottom( 100.0f, 100.0f, 4.0f ) );
    SW_EXPECT_TRUE( ConsoleLogRows::isScrolledToBottom( 97.0f, 100.0f, 4.0f ) );
    SW_EXPECT_FALSE( ConsoleLogRows::isScrolledToBottom( 50.0f, 100.0f, 4.0f ) );
    SW_EXPECT_TRUE( ConsoleLogRows::isScrolledToBottom( 0.0f, 0.0f, 4.0f ) ); // 스크롤할 것이 없다
}

/**
 * @brief [ConsoleLogRowsTest] 누른 줄이 기준이고, 끌기 · Shift 는 끝만 옮기며, 줄이 줄면 범위를 자른다
 */
SW_TEST_CASE( ConsoleLogRowsTest, SelectionKeepsAnchorWhileDragging )
{
    ConsoleLogSelection selection;
    SW_EXPECT_FALSE( selection.hasSelection() );
    SW_EXPECT_EQUAL( 0u, selection.getCount() );

    selection.press( 5, false );
    selection.dragTo( 2 );
    SW_EXPECT_EQUAL( 2u, selection.getFirst() );
    SW_EXPECT_EQUAL( 5u, selection.getLast() );
    SW_EXPECT_EQUAL( 4u, selection.getCount() );
    SW_EXPECT_TRUE( selection.isSelected( 3 ) );
    SW_EXPECT_FALSE( selection.isSelected( 6 ) );

    selection.release();
    selection.dragTo( 9 ); // 떼면 끌기가 끝난다
    SW_EXPECT_EQUAL( 5u, selection.getLast() );

    selection.press( 8, true ); // Shift — 기준 5 는 그대로
    SW_EXPECT_EQUAL( 5u, selection.getFirst() );
    SW_EXPECT_EQUAL( 8u, selection.getLast() );

    selection.clampTo( 7 );
    SW_EXPECT_EQUAL( 6u, selection.getLast() );
    selection.clampTo( 0 );
    SW_EXPECT_FALSE( selection.hasSelection() );
}
