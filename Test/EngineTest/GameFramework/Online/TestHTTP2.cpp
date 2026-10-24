// HTTP/2 프레임과 HPACK — RFC 7541 부록 C 의 허프만 없는 예, 부호기 왕복, 프레임 머리 · SETTINGS · WINDOW_UPDATE 의 바이트와 범위 거절.
#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"
#include "GameFramework/Base/Online/HTTP/Hpack.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [HTTP2Test] RFC 7541 C.2 의 예 — 색인에 더하는 글자 그대로는 동적 표에 들어가고(크기 55), 더하지 않는 것은 들어가지 않으며, 색인된 헤더는 정적 표에서 읽는다
 */
SW_TEST_CASE( HTTP2Test, HpackDecodesTheRfcLiteralExamples )
{
    HpackDecoder        decoder;
    vector<HpackHeader> listHeader;
    string              error;

    // C.2.1: custom-key: custom-header (색인에 더함)
    const uint8 arrIncremental[] = { 0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79, 0x0d, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72 };
    SW_ASSERT_TRUE( decoder.decode( arrIncremental, sizeof( arrIncremental ), listHeader, error ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listHeader.size() );
    SW_EXPECT_TRUE( listHeader[0]._name == "custom-key" && listHeader[0]._value == "custom-header" );
    SW_EXPECT_EQUAL( uint32( 55 ), decoder.getDynamicTableSize() );
    SW_EXPECT_EQUAL( size_t( 1 ), decoder.getDynamicTableCount() );

    // 방금 더한 항목은 색인 62
    const uint8 arrDynamicIndex[] = { 0xbe };
    SW_ASSERT_TRUE( decoder.decode( arrDynamicIndex, sizeof( arrDynamicIndex ), listHeader, error ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listHeader.size() );
    SW_EXPECT_TRUE( listHeader[0]._name == "custom-key" );

    // C.2.2: :path: /sample/path (색인에 더하지 않음 — 이름은 정적 색인 4)
    const uint8 arrWithoutIndexing[] = { 0x04, 0x0c, 0x2f, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2f, 0x70, 0x61, 0x74, 0x68 };
    SW_ASSERT_TRUE( decoder.decode( arrWithoutIndexing, sizeof( arrWithoutIndexing ), listHeader, error ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listHeader.size() );
    SW_EXPECT_TRUE( listHeader[0]._name == ":path" && listHeader[0]._value == "/sample/path" );
    SW_EXPECT_EQUAL( size_t( 1 ), decoder.getDynamicTableCount() ); // 늘지 않았다

    // C.2.4: 색인된 헤더 2 번(:method GET)
    const uint8 arrIndexed[] = { 0x82 };
    SW_ASSERT_TRUE( decoder.decode( arrIndexed, sizeof( arrIndexed ), listHeader, error ) );
    SW_EXPECT_TRUE( listHeader[0]._name == ":method" && listHeader[0]._value == "GET" );
}

/**
 * @brief [HTTP2Test] 허프만 문자열 · 표 밖 색인 · 잘린 블록 · 색인 0 · 알린 상한보다 큰 표 크기 갱신은 읽은 척하지 않고 거절한다
 */
SW_TEST_CASE( HTTP2Test, HpackRefusesWhatItCannotReadInsteadOfGuessing )
{
    HpackDecoder        decoder;
    vector<HpackHeader> listHeader;
    string              error;

    // C.4.1 의 첫 요청 — :authority 값이 허프만이다(아직 읽지 못한다).
    const uint8 arrHuffman[] = { 0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff };
    SW_EXPECT_FALSE( decoder.decode( arrHuffman, sizeof( arrHuffman ), listHeader, error ) );
    SW_EXPECT_TRUE( listHeader.empty() );
    SW_EXPECT_FALSE( error.empty() );

    const uint8 arrFarIndex[] = { 0xff, 0x40 }; // 색인 127 + 64 = 191 — 동적 표가 비어 있다
    SW_EXPECT_FALSE( decoder.decode( arrFarIndex, sizeof( arrFarIndex ), listHeader, error ) );
    const uint8 arrZero[] = { 0x80 };
    SW_EXPECT_FALSE( decoder.decode( arrZero, sizeof( arrZero ), listHeader, error ) );
    const uint8 arrCut[] = { 0x40, 0x0a, 0x63, 0x75 }; // 이름이 길이보다 짧다
    SW_EXPECT_FALSE( decoder.decode( arrCut, sizeof( arrCut ), listHeader, error ) );
    const uint8 arrBigUpdate[] = { 0x3f, 0xe1, 0x1f }; // 표 크기 4097
    SW_EXPECT_FALSE( decoder.decode( arrBigUpdate, sizeof( arrBigUpdate ), listHeader, error ) );
}

/**
 * @brief [HTTP2Test] 부호기가 쓴 블록은 복호기가 같은 헤더 목록으로 읽고, 정적 표 완전 일치는 한 바이트이며, 대문자 이름은 거절한다
 */
SW_TEST_CASE( HTTP2Test, HpackEncoderRoundTripsThroughTheDecoder )
{
    const vector<HpackHeader> listSource = {
        {       ":method",                       "POST"},
        {       ":scheme",                      "https"},
        {         ":path", "/3/device/abcdef0123456789"},
        { "authorization",         "bearer token-value"},
        {    "apns-topic",            "com.example.app"},
        {"content-length",                         "42"},
    };
    vector<uint8> block;
    SW_ASSERT_TRUE( HpackEncoder::encode( listSource, block ) );
    SW_EXPECT_EQUAL( uint8( 0x83 ), block[0] ); // :method POST = 정적 색인 3
    SW_EXPECT_EQUAL( uint8( 0x87 ), block[1] ); // :scheme https = 정적 색인 7

    HpackDecoder        decoder;
    vector<HpackHeader> listRead;
    string              error;
    SW_ASSERT_TRUE( decoder.decode( block.data(), block.size(), listRead, error ) );
    SW_ASSERT_EQUAL( listSource.size(), listRead.size() );
    for ( size_t index = 0; index < listSource.size(); ++index )
    {
        SW_EXPECT_TRUE( listRead[index]._name == listSource[index]._name && listRead[index]._value == listSource[index]._value );
    }
    SW_EXPECT_EQUAL( size_t( 0 ), decoder.getDynamicTableCount() ); // 부호기는 상대의 표를 쓰지 않는다

    vector<uint8> untouched;
    SW_EXPECT_FALSE( HpackEncoder::encode( {
                                               { "Authorization", "x" }
    },
                                           untouched ) );
    SW_EXPECT_TRUE( untouched.empty() );
}

/**
 * @brief [HTTP2Test] 프레임 머리는 24 비트 길이 · 종류 · 플래그 · 31 비트 스트림 id 로 왕복하고, 범위 밖 값은 쓰지 않으며, SETTINGS 와 WINDOW_UPDATE 의 바이트가 맞는다
 */
SW_TEST_CASE( HTTP2Test, FramesWriteAndReadTheWireBytes )
{
    vector<uint8>    bytes;
    HTTP2FrameHeader header;
    header._length   = 0x010203;
    header._type     = static_cast<uint8>( HTTP2FrameType::Headers );
    header._flags    = HTTP2FrameUtil::kFlagEndHeaders;
    header._streamID = 0x7FFFFFFF;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeHeader( header, bytes ) );
    SW_ASSERT_EQUAL( size_t( 9 ), bytes.size() );
    HTTP2FrameHeader readBack;
    SW_ASSERT_TRUE( HTTP2FrameUtil::readHeader( bytes.data(), bytes.size(), readBack ) );
    SW_EXPECT_EQUAL( header._length, readBack._length );
    SW_EXPECT_EQUAL( header._streamID, readBack._streamID );
    SW_EXPECT_EQUAL( header._type, readBack._type );
    SW_EXPECT_FALSE( HTTP2FrameUtil::readHeader( bytes.data(), 8, readBack ) ); // 아직 다 오지 않았다

    vector<uint8>    untouched;
    HTTP2FrameHeader tooLong;
    tooLong._length = 0x1000000;
    SW_EXPECT_FALSE( HTTP2FrameUtil::writeHeader( tooLong, untouched ) );
    HTTP2FrameHeader badStream;
    badStream._streamID = 0x80000000u;
    SW_EXPECT_FALSE( HTTP2FrameUtil::writeHeader( badStream, untouched ) );
    SW_EXPECT_TRUE( untouched.empty() );

    vector<uint8> settings;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeSettings( {
                                                       { HTTP2FrameUtil::kSettingsInitialWindowSize, 65535 }
    },
                                                   false, settings ) );
    const uint8 arrExpected[] = { 0, 0, 6, 4, 0, 0, 0, 0, 0, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xFF };
    SW_ASSERT_EQUAL( sizeof( arrExpected ), settings.size() );
    for ( size_t index = 0; index < settings.size(); ++index )
    {
        SW_EXPECT_EQUAL( arrExpected[index], settings[index] );
    }
    vector<HTTP2Setting> listSetting;
    SW_ASSERT_TRUE( HTTP2FrameUtil::readSettings( settings.data() + 9, 6, listSetting ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listSetting.size() );
    SW_EXPECT_EQUAL( uint32( 65535 ), listSetting[0]._value );
    SW_EXPECT_FALSE( HTTP2FrameUtil::readSettings( settings.data() + 9, 5, listSetting ) ); // 6 의 배수가 아니다
    SW_EXPECT_FALSE( HTTP2FrameUtil::writeSettings( {
                                                        { HTTP2FrameUtil::kSettingsEnablePush, 0 }
    },
                                                    true, untouched ) ); // 확인에는 본문이 없다

    vector<uint8> update;
    SW_EXPECT_FALSE( HTTP2FrameUtil::writeWindowUpdate( 0, 0, update ) ); // 증가량 0 은 프로토콜 오류다
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeWindowUpdate( 3, 1000, update ) );
    SW_ASSERT_EQUAL( size_t( 13 ), update.size() );
    SW_EXPECT_EQUAL( uint8( 8 ), update[3] );
    SW_EXPECT_EQUAL( uint8( 3 ), update[8] );

    vector<uint8> headers;
    SW_EXPECT_FALSE( HTTP2FrameUtil::writeHeaders( 0, { 0x82 }, true, HTTP2FrameUtil::kDefaultMaxFrameSize, headers ) ); // 스트림 0 에는 HEADERS 가 없다
    SW_EXPECT_FALSE( HTTP2FrameUtil::writeHeaders( 1, vector<uint8>( 20, 0x82 ), true, 16, headers ) );                  // 상한 초과 — CONTINUATION 은 아직 없다
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeHeaders( 1, { 0x82 }, true, HTTP2FrameUtil::kDefaultMaxFrameSize, headers ) );
    SW_EXPECT_EQUAL( uint8( 0x5 ), headers[4] ); // END_HEADERS | END_STREAM

    const uint8 arrGoAway[] = { 0, 0, 0, 7, 0, 0, 0, 2, 'b', 'y', 'e' };
    HTTP2GoAway goAway;
    SW_ASSERT_TRUE( HTTP2FrameUtil::readGoAway( arrGoAway, sizeof( arrGoAway ), goAway ) );
    SW_EXPECT_EQUAL( uint32( 7 ), goAway._lastStreamID );
    SW_EXPECT_EQUAL( uint32( 2 ), goAway._errorCode );
    SW_EXPECT_FALSE( HTTP2FrameUtil::readGoAway( arrGoAway, 7, goAway ) );
}
