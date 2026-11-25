#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Panels/Console/Model/ConsoleLogRows.h"

using namespace sw;
using namespace sw::editor;

// 찍은 자리는 있을 때만 줄 끝에 붙는다.
SW_TEST_CASE( ConsoleLogLineTest, SourceIsAppendedOnlyWhenKnown )
{
    LogEntry entry;
    entry._timeStamp   = "t";
    entry._tag         = "Tag";
    entry._message     = "hello";
    const string plain = ConsoleLogRows::formatEntryLine( entry );
    SW_EXPECT_TRUE( plain.find( "hello" ) != string::npos );
    SW_EXPECT_TRUE( ConsoleLogRows::formatEntryLineWithSource( entry ) == plain );
    entry._file = "a.cpp";
    entry._line = 7;
    StringBuilder<constant::buffer::kMax256> expectedBuilder;
    expectedBuilder.appendFormat( "%# (a.cpp:7)", plain );
    const string expected( expectedBuilder.view() );
    SW_EXPECT_TRUE( ConsoleLogRows::formatEntryLineWithSource( entry ) == expected );
}
