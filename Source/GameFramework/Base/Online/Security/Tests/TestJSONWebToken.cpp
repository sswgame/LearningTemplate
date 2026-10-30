#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/JSON/JSONWriter.h"

#include "GameFramework/Base/Online/Security/JSONWebToken.h"
#include "GameFramework/Base/Online/Security/OpenSSL/OpenSSLNetSecurityProvider.h"

// JSONWebTokenTest — 신뢰 경계: 서명 · 클레임(iss · aud · exp) 확인을 통과해야 본문을 읽는다. 바꾼 본문 · 다른 키 · 지난 토큰 · 알고리즘 none 을 거절하는지 본다(nogpu).

namespace
{
    constexpr int64       kNowMs    = 1700000000000ll;
    constexpr const utf8* kIssuer   = "https://issuer.test";
    constexpr const utf8* kAudience = "game-client";

    /** @brief 클레임을 담은 본문입니다. */
    sw::JSONText makePayload( sw::string_view issuer, sw::string_view audience, int64 expSeconds )
    {
        sw::JSONWriter writer;
        writer.beginObject();
        writer.key( "iss" );
        writer.valueString( issuer );
        writer.key( "aud" );
        writer.valueString( audience );
        writer.key( "sub" );
        writer.valueString( "player-7" );
        writer.key( "exp" );
        writer.valueInt( expSeconds );
        writer.endObject();
        std::optional<sw::JSONText> text = writer.finish();
        SW_EXPECT_TRUE( text.has_value() );
        return std::move( *text );
    }

    struct SigningKey
    {
        sw::string       _privateKeyPem{};
        sw::NetPublicKey _publicKey{};
    };

    SigningKey makeKey( sw::INetSecurityProvider& provider )
    {
        SigningKey key;
        SW_EXPECT_TRUE( provider.createSigningKeyPair( sw::NetSignatureAlgorithm::EcdsaP256Sha256, key._privateKeyPem, key._publicKey ) );
        return key;
    }

    sw::string sign( sw::INetSecurityProvider& provider, const SigningKey& key, const sw::JSONText& payload )
    {
        sw::string compact;
        SW_EXPECT_TRUE( sw::JWTUtil::makeSigned( provider, sw::NetSignatureAlgorithm::EcdsaP256Sha256, "kid-1", payload, key._privateKeyPem, compact ) );
        return compact;
    }

    /** @brief 읽고 ID 토큰 규칙으로 확인합니다. 읽기부터 틀리면 비어 있습니다. */
    std::optional<sw::JWTVerified> verifyIDToken( sw::INetSecurityProvider& provider, sw::string_view compact, const sw::NetPublicKey& publicKey )
    {
        static const sw::vector<sw::string> s_listAudience{ kAudience };
        sw::JWTUnverified                   token;
        if ( token.parse( compact ) == false )
            return std::nullopt;
        return std::move( token ).verify( provider, publicKey, sw::JWTClaimRule::makeIDToken( kIssuer, s_listAudience, kNowMs, 60000 ) );
    }
} // namespace

/**
 * @brief [JSONWebTokenTest] 맞게 서명한 토큰은 확인을 통과해 클레임을 읽는다
 */
SW_TEST_CASE( JSONWebTokenTest, AcceptsAValidToken )
{
    sw::OpenSSLNetSecurityProvider provider;
    const SigningKey               key     = makeKey( provider );
    const sw::string               compact = sign( provider, key, makePayload( kIssuer, kAudience, kNowMs / 1000 + 3600 ) );

    const std::optional<sw::JWTVerified> verified = verifyIDToken( provider, compact, key._publicKey );
    SW_REQUIRE_TRUE( verified.has_value() );
    sw::string subject;
    SW_EXPECT_TRUE( verified->findText( "sub", subject ) );
    SW_EXPECT_EQUAL( sw::string( "player-7" ), subject );
    SW_EXPECT_TRUE( verified->hasAudience( kAudience ) );
}

/**
 * @brief [JSONWebTokenTest] 다른 키 · 바꾼 본문 · 다른 발급자 · 다른 대상 · 지난 토큰은 모두 거절한다
 */
SW_TEST_CASE( JSONWebTokenTest, RejectsForgedOrStaleTokens )
{
    sw::OpenSSLNetSecurityProvider provider;
    const SigningKey               key   = makeKey( provider );
    const SigningKey               other = makeKey( provider );
    const sw::string               valid = sign( provider, key, makePayload( kIssuer, kAudience, kNowMs / 1000 + 3600 ) );

    SW_EXPECT_FALSE( verifyIDToken( provider, valid, other._publicKey ).has_value() );

    // 본문을 다른 토큰의 본문으로 바꿔 끼운다(서명은 그대로) — 서명이 본문을 덮지 않으면 통과한다.
    const sw::string otherBody = sign( provider, key, makePayload( kIssuer, kAudience, kNowMs / 1000 + 7200 ) );
    const size_t     firstDot  = valid.find( '.' );
    const size_t     lastDot   = valid.rfind( '.' );
    const sw::string swapped   = valid.substr( 0, firstDot ) + otherBody.substr( otherBody.find( '.' ), otherBody.rfind( '.' ) - otherBody.find( '.' ) ) + valid.substr( lastDot );
    SW_EXPECT_FALSE( verifyIDToken( provider, swapped, key._publicKey ).has_value() );

    SW_EXPECT_FALSE( verifyIDToken( provider, sign( provider, key, makePayload( "https://evil.test", kAudience, kNowMs / 1000 + 3600 ) ), key._publicKey ).has_value() );
    SW_EXPECT_FALSE( verifyIDToken( provider, sign( provider, key, makePayload( kIssuer, "other-client", kNowMs / 1000 + 3600 ) ), key._publicKey ).has_value() );
    SW_EXPECT_FALSE( verifyIDToken( provider, sign( provider, key, makePayload( kIssuer, kAudience, kNowMs / 1000 - 3600 ) ), key._publicKey ).has_value() );
}

/**
 * @brief [JSONWebTokenTest] 알고리즘 none(서명 없는 토큰)과 깨진 형식은 읽기에서 거절한다 — 알고리즘 바꿔치기 공격
 */
SW_TEST_CASE( JSONWebTokenTest, RejectsAlgorithmNoneAndMalformed )
{
    // {"alg":"none","typ":"JWT"} . {"sub":"admin"} . (빈 서명)
    sw::JWTUnverified none;
    SW_EXPECT_FALSE( none.parse( "eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0.eyJzdWIiOiJhZG1pbiJ9." ) );
    sw::JWTUnverified hs256; // {"alg":"HS256","typ":"JWT"}
    SW_EXPECT_FALSE( hs256.parse( "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJhZG1pbiJ9.c2ln" ) );
    sw::JWTUnverified malformed;
    SW_EXPECT_FALSE( malformed.parse( "not-a-token" ) );
    sw::JWTUnverified twoParts;
    SW_EXPECT_FALSE( twoParts.parse( "eyJhbGciOiJFUzI1NiJ9.eyJzdWIiOiJhIn0" ) );
}
