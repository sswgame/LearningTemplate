#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"

// HTTP2FrameTest — HTTP/2 프레임 머리 · SETTINGS · WINDOW_UPDATE · GOAWAY 의 쓰기와 읽기, 범위 밖 값 거절(RFC 9113 4.1 · 6)(nogpu).

/**
 * @brief [HTTP2FrameTest] 머리는 9 바이트로 왕복하고, 읽을 때 스트림 id 의 예약 비트를 버리며, 24 · 31 비트를 넘으면 쓰지 않는다
 */
SW_TEST_CASE( HTTP2FrameTest, HeaderRoundTripsAndKeepsBitLimits )
{
    const sw::HTTP2FrameHeader header{ 0x123456u, 0x7FFFFFFFu, 0x1, sw::HTTP2FrameUtil::kFlagEndStream };
    sw::vector<uint8>          bytes;
    SW_REQUIRE_TRUE( sw::HTTP2FrameUtil::writeHeader( header, bytes ) );
    SW_REQUIRE_EQUAL( sw::HTTP2FrameUtil::kHeaderSize, bytes.size() );

    bytes[5] |= 0x80; // 예약 비트를 켠다 — 받는 쪽은 버린다
    sw::HTTP2FrameHeader read;
    SW_REQUIRE_TRUE( sw::HTTP2FrameUtil::readHeader( bytes.data(), bytes.size(), read ) );
    SW_EXPECT_EQUAL( header._length, read._length );
    SW_EXPECT_EQUAL( header._streamID, read._streamID );
    SW_EXPECT_EQUAL( header._type, read._type );
    SW_EXPECT_EQUAL( header._flags, read._flags );
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::readHeader( bytes.data(), 8, read ) ); // 아직 다 오지 않았다

    sw::vector<uint8> refused;
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::writeHeader( sw::HTTP2FrameHeader{ 0x1000000u, 1, 0, 0 }, refused ) );
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::writeHeader( sw::HTTP2FrameHeader{ 1, 0x80000000u, 0, 0 }, refused ) );
    SW_EXPECT_TRUE( refused.empty() );
}

/**
 * @brief [HTTP2FrameTest] SETTINGS 는 6 바이트 항목으로 왕복하고, 6 의 배수가 아닌 본문은 거절한다. WINDOW_UPDATE 0 · GOAWAY 짧은 본문 · 상한 넘는 DATA 도 거절한다
 */
SW_TEST_CASE( HTTP2FrameTest, BodiesRoundTripAndRefuseBadSizes )
{
    const sw::vector<sw::HTTP2Setting> listSetting{
        {sw::HTTP2FrameUtil::kSettingsMaxFrameSize, 32768u},
        {  sw::HTTP2FrameUtil::kSettingsEnablePush,     0u}
    };
    sw::vector<uint8> bytes;
    SW_REQUIRE_TRUE( sw::HTTP2FrameUtil::writeSettings( listSetting, false, bytes ) );
    sw::vector<sw::HTTP2Setting> read;
    SW_REQUIRE_TRUE( sw::HTTP2FrameUtil::readSettings( bytes.data() + sw::HTTP2FrameUtil::kHeaderSize, bytes.size() - sw::HTTP2FrameUtil::kHeaderSize, read ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), read.size() );
    SW_EXPECT_EQUAL( uint32( 32768 ), read[0]._value );
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::readSettings( bytes.data() + sw::HTTP2FrameUtil::kHeaderSize, 7, read ) );

    sw::vector<uint8> window;
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::writeWindowUpdate( 1, 0, window ) );
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::writeWindowUpdate( 1, 0x80000000u, window ) );
    SW_EXPECT_TRUE( sw::HTTP2FrameUtil::writeWindowUpdate( 0, 65535, window ) );

    const uint8     arrGoAway[8] = { 0, 0, 0, 7, 0, 0, 0, 2 };
    sw::HTTP2GoAway goAway;
    SW_REQUIRE_TRUE( sw::HTTP2FrameUtil::readGoAway( arrGoAway, 8, goAway ) );
    SW_EXPECT_EQUAL( uint32( 7 ), goAway._lastStreamID );
    SW_EXPECT_EQUAL( uint32( 2 ), goAway._errorCode );
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::readGoAway( arrGoAway, 7, goAway ) );

    const uint8       arrData[4] = { 1, 2, 3, 4 };
    sw::vector<uint8> data;
    SW_EXPECT_FALSE( sw::HTTP2FrameUtil::writeData( 1, arrData, 4, true, 3, data ) );
    SW_EXPECT_TRUE( sw::HTTP2FrameUtil::writeData( 1, arrData, 4, true, 4, data ) );
}
