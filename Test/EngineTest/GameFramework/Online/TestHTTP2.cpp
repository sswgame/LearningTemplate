// HTTP/2 프레임과 HPACK — RFC 7541 부록 C 의 허프만 없는 예와 허프만 예(C.4.1, C.6.1), 부호기 왕복, 프레임 머리 · SETTINGS · WINDOW_UPDATE 의 바이트와 범위 거절.
#include "pch.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/HTTP/Hpack.h"
#include "GameFramework/Base/Online/HTTP/HpackStaticTable.h"

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
 * @brief [HTTP2Test] 틀린 허프만 문자열 · 표 밖 색인 · 잘린 블록 · 색인 0 · 알린 상한보다 큰 표 크기 갱신은 읽은 척하지 않고 거절한다
 */
SW_TEST_CASE( HTTP2Test, HpackRefusesWhatItCannotReadInsteadOfGuessing )
{
    HpackDecoder        decoder;
    vector<HpackHeader> listHeader;
    string              error;

    // 기호 '0' 은 5 비트 00000 이다. 패딩이 000 이면(1 이어야 한다) 거절하고, 패딩이 8 비트 넘게 길어도 거절한다.
    const uint8 arrBadPadding[] = { 0x40, 0x81, 0x00 };
    SW_EXPECT_FALSE( decoder.decode( arrBadPadding, sizeof( arrBadPadding ), listHeader, error ) );
    SW_EXPECT_TRUE( listHeader.empty() );
    SW_EXPECT_FALSE( error.empty() );
    const uint8 arrLongPadding[] = { 0x40, 0x82, 0x07, 0xff };
    SW_EXPECT_FALSE( decoder.decode( arrLongPadding, sizeof( arrLongPadding ), listHeader, error ) );
    // 길이 5 에 EOS(30 비트 모두 1)를 품은 문자열
    const uint8 arrEndOfString[] = { 0x40, 0x85, 0xff, 0xff, 0xff, 0xff, 0xfc };
    SW_EXPECT_FALSE( decoder.decode( arrEndOfString, sizeof( arrEndOfString ), listHeader, error ) );

    const uint8 arrFarIndex[] = { 0xff, 0x40 }; // 색인 127 + 64 = 191 — 동적 표가 비어 있다
    SW_EXPECT_FALSE( decoder.decode( arrFarIndex, sizeof( arrFarIndex ), listHeader, error ) );
    const uint8 arrZero[] = { 0x80 };
    SW_EXPECT_FALSE( decoder.decode( arrZero, sizeof( arrZero ), listHeader, error ) );
    const uint8 arrCut[] = { 0x40, 0x0a, 0x63, 0x75 }; // 이름이 길이보다 짧다
    SW_EXPECT_FALSE( decoder.decode( arrCut, sizeof( arrCut ), listHeader, error ) );
    const uint8 arrBigUpdate[] = { 0x3f, 0xe2, 0x1f }; // 표 크기 4097 = 31 + 98 + 31 * 128 (상한 4096 을 하나 넘는다)
    SW_EXPECT_FALSE( decoder.decode( arrBigUpdate, sizeof( arrBigUpdate ), listHeader, error ) );
}

/**
 * @brief [HTTP2Test] RFC 7541 C.4.1 의 첫 요청과 C.6.1 의 첫 응답 — 허프만으로 쓴 값을 풀어 헤더 목록과 동적 표 크기가 RFC 와 같다
 */
SW_TEST_CASE( HTTP2Test, HpackDecodesTheRfcHuffmanExamples )
{
    HpackDecoder        requestDecoder;
    vector<HpackHeader> listHeader;
    string              error;

    const uint8 arrRequest[] = { 0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff };
    SW_ASSERT_TRUE( requestDecoder.decode( arrRequest, sizeof( arrRequest ), listHeader, error ) );
    SW_ASSERT_EQUAL( size_t( 4 ), listHeader.size() );
    SW_EXPECT_TRUE( listHeader[3]._name == ":authority" && listHeader[3]._value == "www.example.com" );
    SW_EXPECT_EQUAL( uint32( 57 ), requestDecoder.getDynamicTableSize() );

    HpackDecoder responseDecoder( 256 ); // C.6 은 표 크기 상한 256 으로 돈다
    const uint8  arrResponse[] = { 0x48, 0x82, 0x64, 0x02, 0x58, 0x85, 0xae, 0xc3, 0x77, 0x1a, 0x4b, 0x61, 0x96, 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b, 0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff, 0x6e, 0x91, 0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 0xe9, 0xae, 0x82, 0xae, 0x43, 0xd3 };
    SW_ASSERT_TRUE( responseDecoder.decode( arrResponse, sizeof( arrResponse ), listHeader, error ) );
    SW_ASSERT_EQUAL( size_t( 4 ), listHeader.size() );
    SW_EXPECT_TRUE( listHeader[0]._name == ":status" && listHeader[0]._value == "302" );
    SW_EXPECT_TRUE( listHeader[1]._name == "cache-control" && listHeader[1]._value == "private" );
    SW_EXPECT_TRUE( listHeader[2]._name == "date" && listHeader[2]._value == "Mon, 21 Oct 2013 20:13:21 GMT" );
    SW_EXPECT_TRUE( listHeader[3]._name == "location" && listHeader[3]._value == "https://www.example.com" );
    SW_EXPECT_EQUAL( uint32( 222 ), responseDecoder.getDynamicTableSize() );
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
 * @brief [HTTP2Test] 정적 표는 RFC 7541 부록 A 의 61 줄이다 — 생성 파일의 개수와 처음 · 끝 · 값이 있는 줄 · 이름만 있는 줄을 색인 표현으로 읽어 RFC 와 대조한다(전체 줄 대조는 생성기가 RFC 본문에서 한다)
 */
SW_TEST_CASE( HTTP2Test, HpackStaticTableMatchesTheRfcAppendixA )
{
    SW_EXPECT_EQUAL( uint32( 61 ), HpackStatic::kEntryCount );
    struct Expected
    {
        uint8       _index;
        const utf8* _pName;
        const utf8* _pValue;
    };
    const Expected arrExpected[] = {
        { 1,                  ":authority",              ""},
        { 7,                     ":scheme",         "https"},
        {14,                     ":status",           "500"},
        {16,             "accept-encoding", "gzip, deflate"},
        {20, "access-control-allow-origin",              ""},
        {40,           "if-modified-since",              ""},
        {56,   "strict-transport-security",              ""},
        {61,            "www-authenticate",              ""},
    };
    for ( const Expected& expected : arrExpected )
    {
        HpackDecoder        decoder;
        vector<HpackHeader> listHeader;
        string              error;
        const uint8         arrBlock[] = { static_cast<uint8>( 0x80 | expected._index ) };
        SW_ASSERT_TRUE( decoder.decode( arrBlock, sizeof( arrBlock ), listHeader, error ) );
        SW_ASSERT_EQUAL( size_t( 1 ), listHeader.size() );
        SW_EXPECT_TRUE( listHeader[0]._name == expected._pName && listHeader[0]._value == expected._pValue );
    }
}

/**
 * @brief [HTTP2Test] 응답 줄의 사유 문구는 RFC 9110 의 이름이고, 표에 없는 코드는 일반 문구로 쓴다
 */
SW_TEST_CASE( HTTP2Test, HTTPReasonPhrasesMatchTheRfc )
{
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 200 ) ) == "OK" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 302 ) ) == "Found" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 400 ) ) == "Bad Request" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 401 ) ) == "Unauthorized" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 403 ) ) == "Forbidden" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 404 ) ) == "Not Found" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 500 ) ) == "Internal Server Error" );
    SW_EXPECT_TRUE( string_view( HTTPWriteUtil::getReasonPhrase( 503 ) ) == "Service Unavailable" );
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
