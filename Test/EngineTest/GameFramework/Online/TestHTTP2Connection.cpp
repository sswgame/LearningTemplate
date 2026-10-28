// HTTP/2 연결 상태 기계 — 연결 서문 · SETTINGS · 요청 머리 바이트, 응답과 창 돌려주기, 보내기 창이 모자란 본문, 동시 스트림 상한의 줄, GOAWAY · RST_STREAM · 본문 상한,
// PING 답과 깨진 프레임(연결 오류). 상대는 손으로 만든 프레임이라 네트워크 없이 돈다(스위트는 HTTP2ConnectionTest).
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/HTTP/HTTP2Connection.h"
#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"
#include "GameFramework/Base/Online/HTTP/Hpack.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct ParsedFrame
    {
        HTTP2FrameHeader _header{};
        vector<uint8>    _body{};
    };

    /** @brief @p bytes 를 프레임으로 가릅니다(@p skip 바이트는 건너뜀 — 연결 서문). 끝이 잘리면 거기서 멈춘다. */
    vector<ParsedFrame> parseFrames( const vector<uint8>& bytes, size_t skip )
    {
        vector<ParsedFrame> listFrame;
        size_t              offset = skip;
        for ( ;; )
        {
            ParsedFrame frame;
            if ( HTTP2FrameUtil::readHeader( bytes.data() + offset, bytes.size() - offset, frame._header ) == false )
                break;
            if ( bytes.size() - offset < HTTP2FrameUtil::kHeaderSize + frame._header._length )
                break;
            const uint8* pBody = bytes.data() + offset + HTTP2FrameUtil::kHeaderSize;
            frame._body.assign( pBody, pBody + frame._header._length );
            listFrame.push_back( std::move( frame ) );
            offset += HTTP2FrameUtil::kHeaderSize + listFrame.back()._header._length;
        }
        return listFrame;
    }

    int32 countFrames( const vector<ParsedFrame>& listFrame, HTTP2FrameType type, uint32 streamID )
    {
        int32 count = 0;
        for ( const ParsedFrame& frame : listFrame )
        {
            if ( frame._header._type == static_cast<uint8>( type ) && frame._header._streamID == streamID )
                ++count;
        }
        return count;
    }

    uint64 sumDataBytes( const vector<ParsedFrame>& listFrame, uint32 streamID )
    {
        uint64 total = 0;
        for ( const ParsedFrame& frame : listFrame )
        {
            if ( frame._header._type == static_cast<uint8>( HTTP2FrameType::Data ) && frame._header._streamID == streamID )
                total += frame._header._length;
        }
        return total;
    }

    HTTPClientRequest makeRequest( HTTPMethod method, uint32 bodySize )
    {
        HTTPClientRequest request;
        request._method = method;
        request._url    = "https://push.example.test/3/device/abc";
        request._bodyBytes.assign( bodySize, 0x41 );
        request._listHeader.push_back( HTTPHeader{ "Authorization", "bearer token" } );
        request._listHeader.push_back( HTTPHeader{ "Connection", "keep-alive" } ); // 연결 단위 머리라 실리지 않는다
        return request;
    }

    HTTPAddress makeAddress()
    {
        HTTPAddress address;
        (void)HTTPAddress::parse( "https://push.example.test/3/device/abc", address ); // 상수 주소 — 틀리면 아래 단언이 머리 검사에서 드러난다
        return address;
    }

    void appendResponseHeaders( vector<uint8>& outBytes, uint32 streamID, int32 status, bool bEndStream )
    {
        vector<HpackHeader> listHeader;
        listHeader.push_back( HpackHeader{ ":status", to_string( status ) } );
        listHeader.push_back( HpackHeader{ "apns-id", "abc" } );
        vector<uint8> listBlock;
        (void)HpackEncoder::encode( listHeader, listBlock );
        (void)HTTP2FrameUtil::writeHeaders( streamID, listBlock, bEndStream, HTTP2FrameUtil::kDefaultMaxFrameSize, outBytes ); // 작은 블록이라 범위 안이다
    }

    void appendData( vector<uint8>& outBytes, uint32 streamID, uint32 size, bool bEndStream )
    {
        const vector<uint8> body( size, 0x42 );
        (void)HTTP2FrameUtil::writeData( streamID, body.data(), body.size(), bEndStream, HTTP2FrameUtil::kDefaultMaxFrameSize, outBytes ); // 작은 본문이라 범위 안이다
    }

    vector<HTTP2Setting> makeSettings( uint16 id, uint32 value )
    {
        vector<HTTP2Setting> listSetting;
        listSetting.push_back( HTTP2Setting{ id, value } );
        return listSetting;
    }

    /** @brief 연결을 시작하고 서문 · 첫 SETTINGS 를 비워 둔 채 서버 SETTINGS 를 먹여 둡니다. */
    void startAndGreet( HTTP2Connection& connection, vector<uint8>& outBytes )
    {
        connection.start( outBytes );
        vector<uint8> listServerSettings;
        (void)HTTP2FrameUtil::writeSettings( {}, false, listServerSettings ); // 본문이 없어 범위 안이다
        string error;
        (void)connection.feed( listServerSettings.data(), listServerSettings.size(), outBytes, error ); // 빈 SETTINGS — 확인이 돌아온다
        outBytes.clear();
    }

    bool feedBytes( HTTP2Connection& connection, const vector<uint8>& bytes, vector<uint8>& outBytes, string& outError )
    {
        return connection.feed( bytes.data(), bytes.size(), outBytes, outError );
    }
} // namespace

/**
 * @brief [HTTP2ConnectionTest] 시작은 연결 서문과 푸시를 끈 SETTINGS 를 쓰고, GET 은 의사 머리 넷과 소문자 머리를 HEADERS 하나(END_STREAM)로 싣는다 — 연결 단위 머리는 싣지 않는다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionStartAndRequestHeadersAreOnTheWire )
{
    HTTP2Connection connection{ HTTP2ConnectionConfig{} };
    vector<uint8>   out;
    connection.start( out );
    const string_view preface = HTTP2FrameUtil::kConnectionPreface;
    SW_ASSERT_TRUE( out.size() > preface.size() );
    SW_EXPECT_TRUE( string_view( reinterpret_cast<const utf8*>( out.data() ), preface.size() ) == preface );
    vector<ParsedFrame> listFrame = parseFrames( out, preface.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), listFrame.size() );
    SW_EXPECT_TRUE( listFrame[0]._header._type == static_cast<uint8>( HTTP2FrameType::Settings ) && listFrame[0]._header._streamID == 0 );
    vector<HTTP2Setting> listSetting;
    SW_ASSERT_TRUE( HTTP2FrameUtil::readSettings( listFrame[0]._body.data(), listFrame[0]._body.size(), listSetting ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listSetting.size() );
    SW_EXPECT_TRUE( listSetting[0]._id == HTTP2FrameUtil::kSettingsEnablePush && listSetting[0]._value == 0 );

    out.clear();
    connection.submit( 1, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    listFrame = parseFrames( out, 0 );
    SW_ASSERT_EQUAL( size_t( 1 ), listFrame.size() );
    SW_EXPECT_TRUE( listFrame[0]._header._type == static_cast<uint8>( HTTP2FrameType::Headers ) && listFrame[0]._header._streamID == 1 );
    SW_EXPECT_TRUE( ( listFrame[0]._header._flags & HTTP2FrameUtil::kFlagEndStream ) != 0 && ( listFrame[0]._header._flags & HTTP2FrameUtil::kFlagEndHeaders ) != 0 );
    HpackDecoder        decoder;
    vector<HpackHeader> listHeader;
    string              error;
    SW_ASSERT_TRUE( decoder.decode( listFrame[0]._body.data(), listFrame[0]._body.size(), listHeader, error ) );
    SW_ASSERT_TRUE( listHeader.size() >= 5 );
    SW_EXPECT_TRUE( listHeader[0]._name == ":method" && listHeader[0]._value == "GET" );
    SW_EXPECT_TRUE( listHeader[1]._name == ":scheme" && listHeader[1]._value == "https" );
    SW_EXPECT_TRUE( listHeader[2]._name == ":authority" && listHeader[2]._value == "push.example.test" );
    SW_EXPECT_TRUE( listHeader[3]._name == ":path" && listHeader[3]._value == "/3/device/abc" );
    SW_EXPECT_TRUE( listHeader[4]._name == "authorization" && listHeader[4]._value == "bearer token" );
    SW_EXPECT_EQUAL( size_t( 5 ), listHeader.size() ); // connection · host 는 빠졌고 GET 은 content-length 가 없다
}

/**
 * @brief [HTTP2ConnectionTest] 서버 SETTINGS 에는 확인이 돌아가고, 응답 HEADERS + DATA(END_STREAM)는 요청 하나를 끝내며 받은 길이만큼 연결 창을 돌려준다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionCompletesResponseAndReturnsWindow )
{
    HTTP2Connection connection{ HTTP2ConnectionConfig{} };
    vector<uint8>   out;
    connection.start( out );
    out.clear();
    vector<uint8> incoming;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeSettings( makeSettings( HTTP2FrameUtil::kSettingsMaxFrameSize, 16384 ), false, incoming ) );
    string error;
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    vector<ParsedFrame> listFrame = parseFrames( out, 0 );
    SW_ASSERT_EQUAL( size_t( 1 ), listFrame.size() );
    SW_EXPECT_TRUE( listFrame[0]._header._type == static_cast<uint8>( HTTP2FrameType::Settings ) && ( listFrame[0]._header._flags & HTTP2FrameUtil::kFlagAck ) != 0 );

    out.clear();
    connection.submit( 7, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    out.clear();
    incoming.clear();
    appendResponseHeaders( incoming, 1, 200, false );
    appendData( incoming, 1, 5, true );
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    vector<HTTPClientResponse> listResponse;
    SW_ASSERT_EQUAL( int32( 1 ), connection.takeResponses( listResponse ) );
    SW_EXPECT_EQUAL( uint64( 7 ), listResponse[0]._requestID );
    SW_EXPECT_EQUAL( int32( 200 ), listResponse[0]._statusCode );
    SW_EXPECT_TRUE( listResponse[0].isSuccess() && listResponse[0].getBodyText().size() == 5 );
    const HTTPHeader* pApnsID = listResponse[0].findHeader( "apns-id" );
    SW_ASSERT_NOT_NULL( pApnsID );
    SW_EXPECT_TRUE( pApnsID->_value == "abc" );
    listFrame = parseFrames( out, 0 );
    SW_EXPECT_EQUAL( int32( 1 ), countFrames( listFrame, HTTP2FrameType::WindowUpdate, 0 ) ); // 연결 창은 돌려주고
    SW_EXPECT_EQUAL( int32( 0 ), countFrames( listFrame, HTTP2FrameType::WindowUpdate, 1 ) ); // 끝난 스트림의 창은 돌려줄 일이 없다
    SW_EXPECT_EQUAL( int32( 0 ), connection.getActiveCount() );
}

/**
 * @brief [HTTP2ConnectionTest] 본문이 상대가 허락한 창(65535)보다 크면 그만큼만 보내고, WINDOW_UPDATE 가 오면 나머지를 보내며 마지막 DATA 에만 END_STREAM 이 붙는다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionPostBodyWaitsForTheSendWindow )
{
    HTTP2Connection connection{ HTTP2ConnectionConfig{} };
    vector<uint8>   out;
    startAndGreet( connection, out );
    constexpr uint32 kBodySize = 70000;
    connection.submit( 1, makeRequest( HTTPMethod::Post, kBodySize ), makeAddress(), out );
    vector<ParsedFrame> listFrame = parseFrames( out, 0 );
    SW_EXPECT_EQUAL( uint64( HTTP2Connection::kDefaultWindowSize ), sumDataBytes( listFrame, 1 ) );
    for ( const ParsedFrame& frame : listFrame )
    {
        SW_EXPECT_TRUE( ( frame._header._flags & HTTP2FrameUtil::kFlagEndStream ) == 0 || frame._header._type != static_cast<uint8>( HTTP2FrameType::Data ) );
    }
    SW_EXPECT_EQUAL( int64( 0 ), connection.getConnectionSendWindow() );

    out.clear();
    vector<uint8> incoming;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeWindowUpdate( 0, 10000, incoming ) );
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeWindowUpdate( 1, 10000, incoming ) );
    string error;
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    listFrame = parseFrames( out, 0 );
    SW_EXPECT_EQUAL( uint64( kBodySize - HTTP2Connection::kDefaultWindowSize ), sumDataBytes( listFrame, 1 ) );
    SW_ASSERT_TRUE( listFrame.empty() == false );
    SW_EXPECT_TRUE( ( listFrame.back()._header._flags & HTTP2FrameUtil::kFlagEndStream ) != 0 );
}

/**
 * @brief [HTTP2ConnectionTest] 상대가 동시 스트림을 1 로 정하면 둘째 요청은 줄을 서고, 첫 스트림이 끝나면 HEADERS 가 나간다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionQueuesRequestsAboveTheConcurrencyLimit )
{
    HTTP2Connection connection{ HTTP2ConnectionConfig{} };
    vector<uint8>   out;
    connection.start( out );
    out.clear();
    vector<uint8> incoming;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeSettings( makeSettings( HTTP2FrameUtil::kSettingsMaxConcurrentStreams, 1 ), false, incoming ) );
    string error;
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    out.clear();
    connection.submit( 1, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    connection.submit( 2, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    vector<ParsedFrame> listFrame = parseFrames( out, 0 );
    SW_EXPECT_EQUAL( int32( 1 ), countFrames( listFrame, HTTP2FrameType::Headers, 1 ) );
    SW_EXPECT_EQUAL( int32( 0 ), countFrames( listFrame, HTTP2FrameType::Headers, 3 ) );
    SW_EXPECT_EQUAL( int32( 2 ), connection.getActiveCount() );

    out.clear();
    incoming.clear();
    appendResponseHeaders( incoming, 1, 200, true );
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    listFrame = parseFrames( out, 0 );
    SW_EXPECT_EQUAL( int32( 1 ), countFrames( listFrame, HTTP2FrameType::Headers, 3 ) ); // 줄 섰던 요청이 열렸다
    vector<HTTPClientResponse> listResponse;
    SW_EXPECT_EQUAL( int32( 1 ), connection.takeResponses( listResponse ) );
    SW_EXPECT_EQUAL( uint64( 1 ), listResponse[0]._requestID );
}

/**
 * @brief [HTTP2ConnectionTest] GOAWAY 는 마지막 스트림 뒤의 요청과 줄 선 요청만 실패로 끝내고 앞의 스트림은 이어 가며, 연결은 새 요청을 거절한다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionGoAwayFailsUnprocessedStreamsOnly )
{
    HTTP2Connection connection{ HTTP2ConnectionConfig{} };
    vector<uint8>   out;
    startAndGreet( connection, out );
    connection.submit( 1, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    connection.submit( 2, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    connection.submit( 3, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    // GOAWAY: 마지막 스트림 1, 오류 없음 — 본문(8 바이트)만 쓴다.
    vector<uint8>    incoming;
    HTTP2FrameHeader goAwayHeader;
    goAwayHeader._length = 8;
    goAwayHeader._type   = static_cast<uint8>( HTTP2FrameType::GoAway );
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeHeader( goAwayHeader, incoming ) );
    const uint8 arrBody[8] = { 0, 0, 0, 1, 0, 0, 0, 0 };
    incoming.insert( incoming.end(), arrBody, arrBody + 8 );
    string error;
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    vector<HTTPClientResponse> listResponse;
    SW_ASSERT_EQUAL( int32( 2 ), connection.takeResponses( listResponse ) );
    for ( const HTTPClientResponse& response : listResponse )
    {
        SW_EXPECT_TRUE( response._bTransportFailed == SW_TRUE && response._requestID != 1 );
    }
    SW_EXPECT_FALSE( connection.isUsable() );
    SW_EXPECT_EQUAL( int32( 1 ), connection.getActiveCount() ); // 스트림 1 은 이어 간다
    connection.submit( 4, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    listResponse.clear();
    SW_ASSERT_EQUAL( int32( 1 ), connection.takeResponses( listResponse ) );
    SW_EXPECT_TRUE( listResponse[0]._requestID == 4 && listResponse[0]._bTransportFailed == SW_TRUE );
}

/**
 * @brief [HTTP2ConnectionTest] RST_STREAM 은 그 스트림만 실패시키고, 본문 상한을 넘은 응답은 그 스트림만 CANCEL 로 끊는다 — 다른 스트림은 그대로 끝난다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionResetAndOversizedBodyFailOnlyThatStream )
{
    HTTP2ConnectionConfig settings;
    settings._maxResponseBodyBytes = 8;
    HTTP2Connection connection{ settings };
    vector<uint8>   out;
    startAndGreet( connection, out );
    connection.submit( 1, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    connection.submit( 2, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    connection.submit( 3, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    out.clear();
    vector<uint8> incoming;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeRstStream( 1, HTTP2ErrorCode::kCancel, incoming ) );
    appendResponseHeaders( incoming, 3, 200, false );
    appendData( incoming, 3, 20, true ); // 상한 8 바이트를 넘는다
    appendResponseHeaders( incoming, 5, 200, false );
    appendData( incoming, 5, 4, true );
    string error;
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    vector<HTTPClientResponse> listResponse;
    SW_ASSERT_EQUAL( int32( 3 ), connection.takeResponses( listResponse ) );
    int32 failed  = 0;
    int32 success = 0;
    for ( const HTTPClientResponse& response : listResponse )
    {
        failed += response._bTransportFailed == SW_TRUE ? 1 : 0;
        success += response.isSuccess() ? 1 : 0;
    }
    SW_EXPECT_EQUAL( int32( 2 ), failed );
    SW_EXPECT_EQUAL( int32( 1 ), success );
    const vector<ParsedFrame> listFrame = parseFrames( out, 0 );
    SW_EXPECT_EQUAL( int32( 1 ), countFrames( listFrame, HTTP2FrameType::RstStream, 3 ) ); // 상한을 넘은 스트림(요청 2 = 스트림 3)만 끊었다
}

/**
 * @brief [HTTP2ConnectionTest] PING 은 같은 8 바이트로 답하고, 깨진 프레임(스트림 0 의 DATA · 푸시 약속)은 연결 오류로 끝나 날아가던 요청이 모두 실패한다
 */
SW_TEST_CASE( HTTP2ConnectionTest, ConnectionAnswersPingAndFailsOnBrokenFrames )
{
    HTTP2Connection connection{ HTTP2ConnectionConfig{} };
    vector<uint8>   out;
    startAndGreet( connection, out );
    const uint8   arrOpaque[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    vector<uint8> incoming;
    SW_ASSERT_TRUE( HTTP2FrameUtil::writePing( arrOpaque, false, incoming ) );
    string error;
    SW_ASSERT_TRUE( feedBytes( connection, incoming, out, error ) );
    vector<ParsedFrame> listFrame = parseFrames( out, 0 );
    SW_ASSERT_EQUAL( size_t( 1 ), listFrame.size() );
    SW_EXPECT_TRUE( listFrame[0]._header._type == static_cast<uint8>( HTTP2FrameType::Ping ) && ( listFrame[0]._header._flags & HTTP2FrameUtil::kFlagAck ) != 0 );
    SW_ASSERT_EQUAL( size_t( 8 ), listFrame[0]._body.size() );
    SW_EXPECT_TRUE( listFrame[0]._body[7] == 8 && listFrame[0]._body[0] == 1 );

    connection.submit( 1, makeRequest( HTTPMethod::Get, 0 ), makeAddress(), out );
    incoming.clear();
    HTTP2FrameHeader brokenHeader; // 스트림 0 의 DATA — 쓰기 도우미가 거절하는 모양이라 머리를 직접 쓴다
    brokenHeader._length = 4;
    brokenHeader._type   = static_cast<uint8>( HTTP2FrameType::Data );
    SW_ASSERT_TRUE( HTTP2FrameUtil::writeHeader( brokenHeader, incoming ) );
    incoming.insert( incoming.end(), 4, 0x42 );
    out.clear();
    SW_EXPECT_FALSE( feedBytes( connection, incoming, out, error ) );
    SW_EXPECT_FALSE( error.empty() );
    vector<HTTPClientResponse> listResponse;
    SW_ASSERT_EQUAL( int32( 1 ), connection.takeResponses( listResponse ) );
    SW_EXPECT_TRUE( listResponse[0]._bTransportFailed == SW_TRUE );
    SW_EXPECT_FALSE( connection.isUsable() );
}
