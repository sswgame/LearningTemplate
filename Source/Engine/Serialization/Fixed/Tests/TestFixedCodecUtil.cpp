// FixedCodecUtilTest — 고정 배치 코덱의 글자 필드: 상한 · 잘림 · 깨진 UTF-8 · NUL 을 읽기에서 막는다. 클라이언트 글자의 입구이다(nogpu).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

using namespace sw;

namespace
{
    /** @brief 글자를 쓰고 그대로 읽습니다. */
    struct FixedTextRoundTrip
    {
        static FixedReadResult run( string_view text, uint32 writeMax, uint32 readMax, string& outText )
        {
            BitWriter writer;
            SW_EXPECT_TRUE( FixedCodecUtil::writeString( writer, string( text ), writeMax ) );
            const vector<uint8> bytes = writer.getBytes();
            BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
            return FixedCodecUtil::readString( reader, readMax, outText );
        }
    };
} // namespace

/** @brief [FixedCodecUtilTest] 올바른 글은 그대로 읽는다 — 빈 글 · 한글 · 줄바꿈 */
SW_TEST_CASE( FixedCodecUtilTest, ValidTextRoundTrips )
{
    for ( const string_view text : { string_view( "" ), string_view( "plain" ), string_view( "한글 이름\n둘째 줄" ) } )
    {
        string read;
        SW_EXPECT_TRUE( FixedTextRoundTrip::run( text, 64, 64, read ) == FixedReadResult::Ok );
        SW_EXPECT_EQUAL( string( text ), read );
    }
}

/** @brief [FixedCodecUtilTest] 깨진 UTF-8 · 끊긴 글자 · 가운데 NUL 은 InvalidText 다 — 저장되어 다른 사람에게 나가기 전에 막는다 */
SW_TEST_CASE( FixedCodecUtilTest, BrokenTextIsRefused )
{
    string read;
    SW_EXPECT_TRUE( FixedTextRoundTrip::run( "bad\xFFtext", 64, 64, read ) == FixedReadResult::InvalidText );
    SW_EXPECT_TRUE( FixedTextRoundTrip::run( "cut\xED\x95", 64, 64, read ) == FixedReadResult::InvalidText );
    SW_EXPECT_TRUE( FixedTextRoundTrip::run( string_view( "nul\0byte", 8 ), 64, 64, read ) == FixedReadResult::InvalidText );
}

/** @brief [FixedCodecUtilTest] 상한을 넘으면 OutOfRange, 스트림이 모자라도 글자 검사가 먼저 오판하지 않는다 */
SW_TEST_CASE( FixedCodecUtilTest, BoundsComeBeforeText )
{
    string read;
    SW_EXPECT_TRUE( FixedTextRoundTrip::run( "toolong", 7, 5, read ) == FixedReadResult::OutOfRange );

    BitWriter writer;
    SW_REQUIRE_TRUE( FixedCodecUtil::writeString( writer, "abcdefgh", 64 ) );
    const vector<uint8> bytes = writer.getBytes();
    BitReader           reader( bytes.data(), 2 ); // 글자 중간에서 끊겼다
    SW_EXPECT_TRUE( FixedCodecUtil::readString( reader, 64, read ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( reader.hasOverflowed() ); // 끊김은 코덱 끝의 넘침 검사가 Truncated 로 돌려준다
}
