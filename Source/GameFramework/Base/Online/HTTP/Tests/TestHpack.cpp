#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/HTTP/Hpack.h"

// HpackTest — HTTP/2 헤더 압축(RFC 7541). 부록 C 의 예제 바이트(허프만 · 동적 표가 이어지는 세 요청)를 풀고, 표 밖 색인 · 상한을 넘는 표 크기 · 깨진 블록을 거절한다(nogpu).

namespace
{
    sw::vector<uint8> fromHex( sw::string_view hex )
    {
        sw::vector<uint8> listByte;
        uint32            nibbleCount = 0;
        uint8             current     = 0;
        for ( const utf8 ch : hex )
        {
            int32 value = -1;
            if ( '0' <= ch && ch <= '9' )
                value = ch - '0';
            else if ( 'a' <= ch && ch <= 'f' )
                value = ch - 'a' + 10;
            if ( value < 0 )
                continue; // 공백
            current = static_cast<uint8>( ( current << 4 ) | value );
            if ( ++nibbleCount % 2 == 0 )
                listByte.push_back( current );
        }
        return listByte;
    }

    bool decode( sw::HpackDecoder& decoder, sw::string_view hex, sw::vector<sw::HpackHeader>& outListHeader )
    {
        const sw::vector<uint8> listByte = fromHex( hex );
        sw::string              error;
        return decoder.decode( listByte.data(), listByte.size(), outListHeader, error );
    }

    void expectHeader( const sw::vector<sw::HpackHeader>& listHeader, size_t index, sw::string_view name, sw::string_view value )
    {
        SW_REQUIRE_TRUE( index < listHeader.size() );
        SW_EXPECT_EQUAL( sw::string( name ), listHeader[index]._name );
        SW_EXPECT_EQUAL( sw::string( value ), listHeader[index]._value );
    }
} // namespace

/**
 * @brief [HpackTest] RFC 7541 C.4 — 허프만으로 쓴 세 요청을 한 디코더로 차례로 풀면 동적 표가 이어져 같은 헤더가 나온다
 */
SW_TEST_CASE( HpackTest, DecodesRFC7541HuffmanRequests )
{
    sw::HpackDecoder            decoder;
    sw::vector<sw::HpackHeader> listHeader;

    SW_REQUIRE_TRUE( decode( decoder, "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff", listHeader ) );
    SW_REQUIRE_EQUAL( size_t( 4 ), listHeader.size() );
    expectHeader( listHeader, 0, ":method", "GET" );
    expectHeader( listHeader, 1, ":scheme", "http" );
    expectHeader( listHeader, 2, ":path", "/" );
    expectHeader( listHeader, 3, ":authority", "www.example.com" );
    SW_EXPECT_EQUAL( uint32( 57 ), decoder.getDynamicTableSize() );

    SW_REQUIRE_TRUE( decode( decoder, "8286 84be 5886 a8eb 1064 9cbf", listHeader ) );
    SW_REQUIRE_EQUAL( size_t( 5 ), listHeader.size() );
    expectHeader( listHeader, 3, ":authority", "www.example.com" ); // 동적 표에서
    expectHeader( listHeader, 4, "cache-control", "no-cache" );

    SW_REQUIRE_TRUE( decode( decoder, "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925 a849 e95b b8e8 b4bf", listHeader ) );
    SW_REQUIRE_EQUAL( size_t( 5 ), listHeader.size() );
    expectHeader( listHeader, 1, ":scheme", "https" );
    expectHeader( listHeader, 2, ":path", "/index.html" );
    expectHeader( listHeader, 4, "custom-key", "custom-value" );
    SW_EXPECT_EQUAL( size_t( 3 ), decoder.getDynamicTableCount() );
    SW_EXPECT_EQUAL( uint32( 164 ), decoder.getDynamicTableSize() );
}

/**
 * @brief [HpackTest] 쓴 블록을 다시 풀면 같은 헤더이고, 대문자 이름은 쓰지 않는다
 */
SW_TEST_CASE( HpackTest, EncoderRoundTrips )
{
    const sw::vector<sw::HpackHeader> listHeader{
        {     ":method",             "POST"},
        {       ":path",        "/v1/token"},
        {"content-type", "application/json"},
        {     "x-trace",           "abc123"}
    };
    sw::vector<uint8> block;
    SW_REQUIRE_TRUE( sw::HpackEncoder::encode( listHeader, block ) );

    sw::HpackDecoder            decoder;
    sw::vector<sw::HpackHeader> decoded;
    sw::string                  error;
    SW_REQUIRE_TRUE( decoder.decode( block.data(), block.size(), decoded, error ) );
    SW_REQUIRE_EQUAL( listHeader.size(), decoded.size() );
    for ( size_t index = 0; index < listHeader.size(); ++index )
    {
        expectHeader( decoded, index, listHeader[index]._name, listHeader[index]._value );
    }

    sw::vector<uint8> refused;
    SW_EXPECT_FALSE( sw::HpackEncoder::encode( {
                                                   { "Content-Type", "x" }
    },
                                               refused ) );
    SW_EXPECT_TRUE( refused.empty() );
}

/**
 * @brief [HpackTest] 표 밖 색인, 알린 상한보다 큰 표 크기 갱신, 끝이 잘린 문자열, 틀린 허프만 패딩을 거절하고 목록을 비운다
 */
SW_TEST_CASE( HpackTest, RefusesMalformedBlocks )
{
    sw::vector<sw::HpackHeader> listHeader;
    sw::HpackDecoder            outOfTable;
    SW_EXPECT_FALSE( decode( outOfTable, "be", listHeader ) ); // 색인 62 — 동적 표가 비었다
    SW_EXPECT_TRUE( listHeader.empty() );
    sw::HpackDecoder zeroIndex;
    SW_EXPECT_FALSE( decode( zeroIndex, "80", listHeader ) ); // 색인 0 은 없다

    sw::HpackDecoder small( 256 );
    SW_EXPECT_FALSE( decode( small, "3fe1 1f", listHeader ) ); // 표 크기 갱신 4096 > 알린 256

    sw::HpackDecoder truncated;
    SW_EXPECT_FALSE( decode( truncated, "4088 25a8 49e9", listHeader ) ); // 이름 길이 8 인데 4 바이트뿐

    sw::HpackDecoder badPadding;
    SW_EXPECT_FALSE( decode( badPadding, "4081 00 8125", listHeader ) ); // 패딩이 1 이 아닌 허프만 이름
}
