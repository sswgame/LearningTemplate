#include "pch.h"

#include "Core/Network/DebugReplyLine.h"
#include "Core/Network/Transport/NetEmulation.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 항목 `<번호>,` 를 @p maxBytes 를 넘지 않게 @p count 개까지 붙인다(줄일 수 있는 꼬리의 시험 대역). */
    void appendItems( string& inoutText, size_t maxBytes, int32 count )
    {
        for ( int32 index = 0; index < count; ++index )
        {
            const string item = to_string( static_cast<uint64>( index ) ) + ",";
            if ( inoutText.size() + item.size() > maxBytes )
                break;
            inoutText += item;
        }
    }
} // namespace

/**
 * @brief [DebugReplyLineTest] 상한 안의 줄은 그대로 나가고 잘림 표시가 붙지 않는다
 */
SW_TEST_CASE( DebugReplyLineTest, ALineWithinTheLimitIsKeptWhole )
{
    const string line = DebugReplyLine::makeCapped( "head", []( string& inoutText, size_t maxBytes )
    {
        appendItems( inoutText, maxBytes, 10 );
    } );
    SW_EXPECT_EQUAL( line, string( "head0,1,2,3,4,5,6,7,8,9," ) );
    SW_EXPECT_TRUE( line.find( DebugReplyLine::kTruncatedMarker ) == string::npos );
}

/**
 * @brief [DebugReplyLineTest] 상한을 넘는 꼬리는 일부를 빼고, 줄은 상한 안에서 끝나며, 끝에 전부 담았을 때의 길이가 표시된다
 */
SW_TEST_CASE( DebugReplyLineTest, AnOverlongTailIsCutAndMarkedWithItsFullLength )
{
    string fullLine = "head";
    appendItems( fullLine, std::numeric_limits<size_t>::max(), 2000 );
    SW_ASSERT_TRUE( fullLine.size() > DebugReplyLine::kMaxBytes );

    const string line = DebugReplyLine::makeCapped( "head", []( string& inoutText, size_t maxBytes )
    {
        appendItems( inoutText, maxBytes, 2000 );
    } );
    SW_EXPECT_TRUE( DebugReplyLine::fits( line.size() ) );
    const string marker = string( DebugReplyLine::kTruncatedMarker ) + to_string( static_cast<uint64>( fullLine.size() ) );
    SW_ASSERT_TRUE( line.size() > marker.size() );
    SW_EXPECT_EQUAL( line.substr( line.size() - marker.size() ), marker );
    SW_EXPECT_TRUE( line.compare( 0, 4, "head" ) == 0 );
    SW_EXPECT_TRUE( DebugReplyLine::kTruncatedMarkerBytes >= marker.size() );
}

/**
 * @brief [DebugReplyLineTest] 상한은 딱 `kMaxBytes` 까지 허용한다
 */
SW_TEST_CASE( DebugReplyLineTest, FitsIsInclusiveAtTheLimit )
{
    SW_EXPECT_TRUE( DebugReplyLine::fits( DebugReplyLine::kMaxBytes ) );
    SW_EXPECT_FALSE( DebugReplyLine::fits( DebugReplyLine::kMaxBytes + 1 ) );
    const string atLimit( DebugReplyLine::kMaxBytes, 'a' );
    SW_EXPECT_EQUAL( DebugReplyLine::makeFixed( atLimit ), atLimit );
}

/**
 * @brief [DebugReplyLineTest] 길이가 정해진 줄이 상한을 넘으면 말없이 나가지 않고 잘려 표시가 붙는다
 */
SW_TEST_CASE( DebugReplyLineTest, AnOverlongFixedLineIsCutAndMarked )
{
    const string tooLong( DebugReplyLine::kMaxBytes + 100, 'a' );
    const string line = DebugReplyLine::makeFixed( tooLong );
    SW_EXPECT_TRUE( DebugReplyLine::fits( line.size() ) );
    SW_EXPECT_TRUE( line.find( string( DebugReplyLine::kTruncatedMarker ) + to_string( static_cast<uint64>( tooLong.size() ) ) ) != string::npos );
}

/**
 * @brief [DebugReplyLineTest] 회선 흉내 보고 줄은 값이 커도 상한 안이고 잘림 표시 없이 되읽힌다
 */
SW_TEST_CASE( DebugReplyLineTest, TheEmulationReportLineStaysWithinTheLimit )
{
    NetEmulationReport report;
    report._sentPacketCount     = std::numeric_limits<uint64>::max();
    report._droppedPacketCount  = std::numeric_limits<uint64>::max();
    report._resentMessageCount  = std::numeric_limits<uint64>::max();
    report._measuredLossPercent = 100.0f;
    report._rttMilliseconds     = 1.0e9f;
    report._connectionCount     = 1000;
    const string line           = report.makeLine();
    SW_EXPECT_TRUE( DebugReplyLine::fits( line.size() ) );
    SW_EXPECT_TRUE( line.find( DebugReplyLine::kTruncatedMarker ) == string::npos );
    NetEmulationReport parsed;
    SW_EXPECT_TRUE( NetEmulationReport::tryParseLine( line, parsed ) );
}
