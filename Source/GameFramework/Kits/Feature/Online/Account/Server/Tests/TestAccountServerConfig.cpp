// LoginTicketKeyTest · PlatformLoginProviderConfigTest — 계정 키트 서버의 설정 읽기: 티켓 주 키의 16 진 글, 외부 로그인 제공자 항목(방식별 필수 키 · 모르는 키 · 이름 규칙 · 기본 주체 경로).
// 철자가 틀린 설정이 조용히 기본값이 되지 않는지 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"

using namespace sw;

namespace
{
    struct AccountConfigTestUtil
    {
        [[nodiscard]] static bool readProvider( string_view text, PlatformLoginProviderConfig& outSettings )
        {
            JSONDocument document;
            SW_EXPECT_TRUE( document.tryParse( text ) );
            string     error;
            const bool bRead = PlatformLoginProviderFactory::readEntry( document.getRoot(), outSettings, error );
            SW_EXPECT_EQUAL( bRead, error.empty() ); // 실패면 까닭이 있다
            return bRead;
        }
    };
} // namespace

/** @brief [LoginTicketKeyTest] 주 키는 16 진수 64 자이고 앞이 높은 자리다. 대문자 · 앞뒤 공백은 받고, 길이 · 글자가 틀리면 거절한다 */
SW_TEST_CASE( LoginTicketKeyTest, ParseHex )
{
    static_assert( LoginTicketAuthority::kMasterKeySize == 32 );
    string text;
    for ( int32 index = 0; index < LoginTicketAuthority::kMasterKeySize; ++index )
    {
        static constexpr const utf8* kDigits = "0123456789abcdef";
        text.push_back( kDigits[( index >> 4 ) & 0xF] );
        text.push_back( kDigits[index & 0xF] );
    }
    uint8 arrKey[LoginTicketAuthority::kMasterKeySize]{};
    SW_REQUIRE_TRUE( LoginTicketKey::parseHex( text, arrKey ) );
    for ( int32 index = 0; index < LoginTicketAuthority::kMasterKeySize; ++index )
    {
        SW_EXPECT_EQUAL( static_cast<uint8>( index ), arrKey[index] );
    }

    string upper = "  " + text + "\n";
    for ( utf8& character : upper )
    {
        character = 'a' <= character && character <= 'f' ? static_cast<utf8>( character - 'a' + 'A' ) : character;
    }
    uint8 arrUpper[LoginTicketAuthority::kMasterKeySize]{};
    SW_EXPECT_TRUE( LoginTicketKey::parseHex( upper, arrUpper ) );
    SW_EXPECT_EQUAL( uint8( 31 ), arrUpper[31] );

    uint8 arrBad[LoginTicketAuthority::kMasterKeySize]{};
    SW_EXPECT_FALSE( LoginTicketKey::parseHex( "", arrBad ) );
    SW_EXPECT_FALSE( LoginTicketKey::parseHex( text.substr( 2 ), arrBad ) );
    SW_EXPECT_FALSE( LoginTicketKey::parseHex( text + "00", arrBad ) );
    string notHex = text;
    notHex[10]    = 'g';
    SW_EXPECT_FALSE( LoginTicketKey::parseHex( notHex, arrBad ) );
}

/** @brief [PlatformLoginProviderConfigTest] 방식마다 필수 키를 읽고, 주체 경로의 기본값은 OIDC 가 `sub` · 프로필이 `id` 다 */
SW_TEST_CASE( PlatformLoginProviderConfigTest, ReadsEachKind )
{
    {
        PlatformLoginProviderConfig settings;
        SW_REQUIRE_TRUE( AccountConfigTestUtil::readProvider( R"({"name":"google","kind":"oidc","issuer":"https://i","jwksUrl":"https://j","clientIds":["a","b"],"requireNonce":true})", settings ) );
        SW_EXPECT_TRUE( settings._kind == PlatformLoginProviderKind::Oidc );
        SW_EXPECT_EQUAL( string( "sub" ), settings._subjectPath );
        SW_EXPECT_EQUAL( size_t( 2 ), settings._listClientID.size() );
        SW_EXPECT_TRUE( settings._bRequireNonce == SW_TRUE );
    }
    {
        PlatformLoginProviderConfig settings;
        SW_REQUIRE_TRUE( AccountConfigTestUtil::readProvider( R"({"name":"naver","kind":"profile","profileUrl":"https://p","subjectPath":"response.id"})", settings ) );
        SW_EXPECT_TRUE( settings._kind == PlatformLoginProviderKind::AccessTokenProfile );
        SW_EXPECT_EQUAL( string( "response.id" ), settings._subjectPath );
    }
    {
        PlatformLoginProviderConfig settings;
        SW_REQUIRE_TRUE( AccountConfigTestUtil::readProvider( R"({"name":"plain","kind":"profile","profileUrl":"https://p"})", settings ) );
        SW_EXPECT_EQUAL( string( "id" ), settings._subjectPath );
    }
    {
        PlatformLoginProviderConfig settings;
        SW_REQUIRE_TRUE( AccountConfigTestUtil::readProvider( R"({"name":"steam","kind":"ticketQuery","profileUrl":"https://q","ticketParam":"ticket","keyParam":"key","keySecretEnvironment":"K","resultPath":"response.params.result","resultValue":"OK"})", settings ) );
        SW_EXPECT_TRUE( settings._kind == PlatformLoginProviderKind::TicketQuery );
    }
    {
        PlatformLoginProviderConfig settings;
        SW_REQUIRE_TRUE( AccountConfigTestUtil::readProvider( R"({"name":"apple","kind":"appleSignIn","issuer":"https://i","jwksUrl":"https://j","clientIds":["a"],"teamId":"T","signingKeyId":"S","keySecretEnvironment":"K"})", settings ) );
        SW_EXPECT_TRUE( settings._kind == PlatformLoginProviderKind::AppleSignIn );
        SW_EXPECT_EQUAL( string( "sub" ), settings._subjectPath );
    }
}

/** @brief [PlatformLoginProviderConfigTest] 모르는 키 · 모르는 방식 · 빠진 필수 키 · 규칙 밖 이름 · 참거짓이 아닌 requireNonce 는 오류다 */
SW_TEST_CASE( PlatformLoginProviderConfigTest, RefusesBrokenEntries )
{
    SW_EXPECT_LOG( "JSON.WrongType" ); // requireNonce 가 글이다 — JSON 읽기가 남긴다
    for ( const utf8* pText : {
              R"({"name":"google","kind":"oidc","issuer":"https://i","jwksUrl":"https://j","clientIds":["a"],"typo":1})",
              R"({"name":"google","kind":"saml"})",
              R"({"name":"google","kind":"oidc","issuer":"https://i","jwksUrl":"https://j"})",
              R"({"name":"google","kind":"oidc","jwksUrl":"https://j","clientIds":["a"]})",
              R"({"name":"Google","kind":"profile","profileUrl":"https://p"})",
              R"({"name":"","kind":"profile","profileUrl":"https://p"})",
              R"({"name":"averyveryverylongname","kind":"profile","profileUrl":"https://p"})",
              R"({"name":"naver","kind":"profile"})",
              R"({"name":"steam","kind":"ticketQuery","profileUrl":"https://q","ticketParam":"ticket","keyParam":"key","resultPath":"r","resultValue":"OK"})",
              R"({"name":"apple","kind":"appleSignIn","issuer":"https://i","jwksUrl":"https://j","clientIds":["a"],"teamId":"T","keySecretEnvironment":"K"})",
              R"({"name":"google","kind":"oidc","issuer":"https://i","jwksUrl":"https://j","clientIds":["a"],"requireNonce":"yes"})",
          } )
    {
        PlatformLoginProviderConfig settings;
        SW_EXPECT_FALSE_MSG( AccountConfigTestUtil::readProvider( pText, settings ), pText );
    }
}

/** @brief [PlatformLoginProviderConfigTest] 공장은 틀린 설정이나 HTTP 클라이언트 없이는 만들지 않는다. 애플 로그인은 공장이 아니라 제공자 묶음이 만든다 */
SW_TEST_CASE( PlatformLoginProviderConfigTest, FactoryRefusesWhatItCannotBuild )
{
    PlatformLoginProviderConfig settings;
    settings._name       = "naver";
    settings._kind       = PlatformLoginProviderKind::AccessTokenProfile;
    settings._profileURL = "https://p";
    SW_EXPECT_TRUE( PlatformLoginProviderFactory::isValidSettings( settings ) );
    SW_EXPECT_NULL( PlatformLoginProviderFactory::create( settings, nullptr, nullptr ) );

    settings._subjectPath.clear();
    SW_EXPECT_FALSE( PlatformLoginProviderFactory::isValidSettings( settings ) );
}
