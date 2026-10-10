// Base64 — RFC 4648 시험 벡터(표준 · URL 안전), 채움 있고 없음, 알파벳 밖 글자 · 남는 비트 거절, 왕복.
#include "pch.h"

#include "Core/String/Base64Util.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct TestBase64UtilInternal
    {
        static string encodeText( const utf8* pText ) { return Base64Util::encode( reinterpret_cast<const uint8*>( pText ), string_view( pText ).size() ); }

        static string decodeText( string_view text, bool bURL )
        {
            vector<uint8> bytes;
            const bool    bDecoded = bURL ? Base64Util::decodeURL( text, bytes ) : Base64Util::decode( text, bytes );
            return bDecoded ? string( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() ) : string( "<fail>" );
        }
    };
} // namespace

SW_TEST_CASE( Base64UtilTest, MatchesRfc4648Vectors )
{
    using Internal = TestBase64UtilInternal;
    SW_EXPECT_EQUAL( string( "" ), Internal::encodeText( "" ) );
    SW_EXPECT_EQUAL( string( "Zg==" ), Internal::encodeText( "f" ) );
    SW_EXPECT_EQUAL( string( "Zm8=" ), Internal::encodeText( "fo" ) );
    SW_EXPECT_EQUAL( string( "Zm9v" ), Internal::encodeText( "foo" ) );
    SW_EXPECT_EQUAL( string( "Zm9vYg==" ), Internal::encodeText( "foob" ) );
    SW_EXPECT_EQUAL( string( "Zm9vYmE=" ), Internal::encodeText( "fooba" ) );
    SW_EXPECT_EQUAL( string( "Zm9vYmFy" ), Internal::encodeText( "foobar" ) );
    SW_EXPECT_EQUAL( string( "foobar" ), Internal::decodeText( "Zm9vYmFy", false ) );
    SW_EXPECT_EQUAL( string( "fooba" ), Internal::decodeText( "Zm9vYmE=", false ) );
    SW_EXPECT_EQUAL( string( "fooba" ), Internal::decodeText( "Zm9vYmE", false ) ); // 채움 없이도
}

SW_TEST_CASE( Base64UtilTest, URLAlphabetHasNoPaddingAndRoundTrips )
{
    const uint8 arrByte[] = { 0xFB, 0xFF, 0xBF, 0x00, 0x10 };
    SW_EXPECT_EQUAL( string( "-_-_ABA" ), Base64Util::encodeURL( arrByte, sizeof( arrByte ) ) );
    SW_EXPECT_EQUAL( string( "+/+/ABA=" ), Base64Util::encode( arrByte, sizeof( arrByte ) ) );
    vector<uint8> decoded;
    SW_ASSERT_TRUE( Base64Util::decodeURL( "-_-_ABA", decoded ) );
    SW_ASSERT_EQUAL( sizeof( arrByte ), decoded.size() );
    for ( size_t index = 0; index < decoded.size(); ++index )
    {
        SW_EXPECT_EQUAL( arrByte[index], decoded[index] );
    }
    vector<uint8> listByte;
    for ( int32 value = 0; value < 256; ++value )
    {
        listByte.push_back( static_cast<uint8>( value ) );
    }
    SW_ASSERT_TRUE( Base64Util::decodeURL( Base64Util::encodeURL( listByte.data(), listByte.size() ), decoded ) );
    SW_EXPECT_TRUE( decoded == listByte );
}

SW_TEST_CASE( Base64UtilTest, RejectsForeignCharactersAndNonZeroLeftoverBits )
{
    vector<uint8> decoded;
    SW_EXPECT_FALSE( Base64Util::decodeURL( "ab+c", decoded ) ); // 표준 글자는 URL 알파벳 밖
    SW_EXPECT_FALSE( Base64Util::decode( "ab-c", decoded ) );
    SW_EXPECT_FALSE( Base64Util::decode( "Zm9v!", decoded ) );
    SW_EXPECT_FALSE( Base64Util::decode( "Z", decoded ) );  // 6 비트 하나
    SW_EXPECT_FALSE( Base64Util::decode( "Zh", decoded ) ); // 'f' 는 "Zg" — 남는 4 비트가 0 이 아니다
    SW_EXPECT_TRUE( Base64Util::decode( "Zg", decoded ) );
}
