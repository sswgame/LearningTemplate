#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Network/EngineNetSecurity.h"

#include "TestFramework/TestFramework.h"

#include <cstring>

// OpenSSL 제공자 — RFC 7748 X25519 · RFC 5869 HKDF · Argon2id(RFC 9106 알고리즘을 따로 짠 참조 구현이 만든 값)의 알려진 벡터(바인딩이 맞는지),
// AEAD 두 가지의 왕복 · 변조 · 다른 키 · 다른 AAD · 다른 nonce 거절, 메모리 위 TLS 1.3 핸드셰이크(자체 서명 신뢰) · 다운그레이드(1.2 클라이언트) 거절 ·
// 고정 불일치 거절, 개발용 인증서로 서버 · 클라이언트 컨텍스트를 여는 길.

using namespace sw;

namespace
{
    uint8 toNibble( utf8 digit ) { return static_cast<uint8>( digit <= '9' ? digit - '0' : digit - 'a' + 10 ); }

    vector<uint8> fromHex( const utf8* pHex )
    {
        vector<uint8> bytes;
        for ( const utf8* pCursor = pHex; pCursor[0] != '\0' && pCursor[1] != '\0'; pCursor += 2 )
        {
            bytes.push_back( static_cast<uint8>( ( toNibble( pCursor[0] ) << 4 ) | toNibble( pCursor[1] ) ) );
        }
        return bytes;
    }

    /** @brief 두 세션 사이에 암호문을 옮깁니다(20 번) — 소켓 없이 핸드셰이크. */
    void pumpTls( ITlsSession& client, ITlsSession& server )
    {
        vector<uint8> bytes;
        for ( int32 round = 0; round < 20; ++round )
        {
            bytes.clear();
            client.takeCiphertext( bytes );
            if ( bytes.empty() == false )
                (void)server.feedCiphertext( bytes.data(), static_cast<int32>( bytes.size() ) );
            bytes.clear();
            server.takeCiphertext( bytes );
            if ( bytes.empty() == false )
                (void)client.feedCiphertext( bytes.data(), static_cast<int32>( bytes.size() ) );
        }
    }

    struct TlsPair
    {
        unique_ptr<ITlsContext> _serverContext{};
        unique_ptr<ITlsContext> _clientContext{};
        string                  _certificatePem{};
    };

    bool makeTlsPair( TlsPair& outPair, TlsVersion clientMaxVersion, const string& pinnedOverride )
    {
        INetSecurityProvider& provider = EngineNetSecurity::getProvider();
        string                privateKeyPem;
        if ( provider.createSelfSignedCertificate( "localhost", 30, outPair._certificatePem, privateKeyPem ) == false )
            return false;
        string             error;
        TlsContextSettings server;
        server._role           = TlsRole::Server;
        server._certificatePem = outPair._certificatePem;
        server._privateKeyPem  = privateKeyPem;
        outPair._serverContext = provider.createTlsContext( server, error );
        TlsContextSettings client;
        client._role                       = TlsRole::Client;
        client._trustPem                   = outPair._certificatePem;
        client._serverName                 = "localhost";
        client._pinnedCertificateSha256Hex = pinnedOverride;
        client._minVersion                 = TlsVersion::Tls12;
        client._maxVersion                 = clientMaxVersion;
        outPair._clientContext             = provider.createTlsContext( client, error );
        return outPair._serverContext != nullptr && outPair._clientContext != nullptr;
    }

    bool isHandshakeCarryingData( ITlsContext& serverContext, ITlsContext& clientContext )
    {
        unique_ptr<ITlsSession> client      = clientContext.createSession();
        unique_ptr<ITlsSession> server      = serverContext.createSession();
        const uint8             arrHello[5] = { 'h', 'e', 'l', 'l', 'o' };
        if ( client->writePlaintext( arrHello, 5 ) == false ) // 핸드셰이크 전 — 모아 두었다가 보낸다
            return false;
        pumpTls( *client, *server );
        vector<uint8> receivedBytes;
        const bool    bEstablished = client->getState() == TlsSessionState::Established && server->getState() == TlsSessionState::Established;
        return bEstablished && server->readPlaintext( receivedBytes ) && receivedBytes.size() == 5 && std::memcmp( receivedBytes.data(), arrHello, 5 ) == 0;
    }
} // namespace

SW_TEST_CASE( NetSecurityProviderTest, X25519MatchesRfc7748Vector )
{
    INetSecurityProvider& provider     = EngineNetSecurity::getProvider();
    const vector<uint8>   alicePrivate = fromHex( "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a" );
    const vector<uint8>   bobPublic    = fromHex( "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f" );
    const vector<uint8>   expected     = fromHex( "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742" );
    uint8                 arrShared[NetSecurityConstant::kX25519KeySize];
    SW_ASSERT_TRUE( provider.computeX25519SharedSecret( alicePrivate.data(), bobPublic.data(), arrShared ) );
    SW_EXPECT_TRUE( vector<uint8>( arrShared, arrShared + NetSecurityConstant::kX25519KeySize ) == expected );

    // 새 키 쌍 둘은 같은 공유 비밀을 만든다. 0 공개 키(작은 차수 점)는 거절.
    NetX25519KeyPair alice;
    NetX25519KeyPair bob;
    SW_ASSERT_TRUE( provider.makeX25519KeyPair( alice ) && provider.makeX25519KeyPair( bob ) );
    uint8 arrA[NetSecurityConstant::kX25519KeySize];
    uint8 arrB[NetSecurityConstant::kX25519KeySize];
    SW_ASSERT_TRUE( provider.computeX25519SharedSecret( alice._arrPrivateKey, bob._arrPublicKey, arrA ) );
    SW_ASSERT_TRUE( provider.computeX25519SharedSecret( bob._arrPrivateKey, alice._arrPublicKey, arrB ) );
    SW_EXPECT_TRUE( std::memcmp( arrA, arrB, sizeof( arrA ) ) == 0 );
    const uint8 arrZero[NetSecurityConstant::kX25519KeySize] = {};
    SW_EXPECT_FALSE( provider.computeX25519SharedSecret( alice._arrPrivateKey, arrZero, arrA ) );

    alice.wipe();
    SW_EXPECT_TRUE( std::memcmp( alice._arrPrivateKey, arrZero, sizeof( arrZero ) ) == 0 );
}

SW_TEST_CASE( NetSecurityProviderTest, HkdfMatchesRfc5869Case1 )
{
    const vector<uint8> secret( 22, 0x0b );
    const vector<uint8> salt     = fromHex( "000102030405060708090a0b0c" );
    const vector<uint8> info     = fromHex( "f0f1f2f3f4f5f6f7f8f9" );
    const vector<uint8> expected = fromHex( "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865" );
    vector<uint8>       output( 42 );
    SW_ASSERT_TRUE( EngineNetSecurity::getProvider().computeHkdfSha256( secret.data(), 22, salt.data(), 13, info.data(), 10, output.data(), 42 ) );
    SW_EXPECT_TRUE( output == expected );
}

SW_TEST_CASE( NetSecurityProviderTest, PasswordHashMatchesArgon2idVectors )
{
    // 기대값은 RFC 9106 의 Argon2id 를 따로 짠 참조 구현(RFC 9106 5.3 벡터로 먼저 맞춘 것)이 낸 값이다 — 제공자는 비밀 · 연관 자료를 받지 않으므로 그 둘 없는 변형.
    INetSecurityProvider& provider      = EngineNetSecurity::getProvider();
    const utf8            arrPassword[] = "password";
    const utf8            arrSalt[]     = "somesalt12345678";
    NetPasswordHashParams params;
    params._memoryKiB      = 64;
    params._iterationCount = 2;
    params._parallelism    = 1;
    uint8 arrHash[32];
    SW_ASSERT_TRUE( provider.computePasswordHash( reinterpret_cast<const uint8*>( arrPassword ), 8, reinterpret_cast<const uint8*>( arrSalt ), 16, params, arrHash, 32 ) );
    SW_EXPECT_TRUE( vector<uint8>( arrHash, arrHash + 32 ) == fromHex( "d1dd9f4bfa15a408c1ae553f4c84aa83d73afcdb94a3b5b597e8eb201489b96b" ) );

    // 레인 넷(병렬도) · 반복 셋 — 매개변수가 제자리에 들어가는지.
    const vector<uint8> password( 32, 0x01 );
    vector<uint8>       salt( 16, 0x02 );
    params._memoryKiB      = 32;
    params._iterationCount = 3;
    params._parallelism    = 4;
    SW_ASSERT_TRUE( provider.computePasswordHash( password.data(), 32, salt.data(), 16, params, arrHash, 32 ) );
    SW_EXPECT_TRUE( vector<uint8>( arrHash, arrHash + 32 ) == fromHex( "03aab965c12001c9d7d0d2de33192c0494b684bb148196d73c1df1acaf6d0c2e" ) );

    // 소금 한 비트가 다르면 다른 해시.
    uint8 arrOther[32];
    salt[0] ^= 0x01;
    SW_ASSERT_TRUE( provider.computePasswordHash( password.data(), 32, salt.data(), 16, params, arrOther, 32 ) );
    SW_EXPECT_TRUE( std::memcmp( arrHash, arrOther, sizeof( arrHash ) ) != 0 );

    // 기본값(OWASP m = 19 MiB · t = 2 · p = 1)도 돈다 — 같은 입력이면 같은 값.
    const NetPasswordHashParams defaultParams;
    uint8                       arrFirst[32];
    uint8                       arrSecond[32];
    SW_ASSERT_TRUE( provider.computePasswordHash( password.data(), 32, salt.data(), 16, defaultParams, arrFirst, 32 ) );
    SW_ASSERT_TRUE( provider.computePasswordHash( password.data(), 32, salt.data(), 16, defaultParams, arrSecond, 32 ) );
    SW_EXPECT_TRUE( std::memcmp( arrFirst, arrSecond, sizeof( arrFirst ) ) == 0 );
}

SW_TEST_CASE( NetSecurityProviderTest, AeadRoundTripsAndRejectsTampering )
{
    INetSecurityProvider&  provider       = EngineNetSecurity::getProvider();
    const NetAeadAlgorithm arrAlgorithm[] = { NetAeadAlgorithm::Aes256Gcm, NetAeadAlgorithm::ChaCha20Poly1305 };
    for ( const NetAeadAlgorithm algorithm : arrAlgorithm )
    {
        uint8 arrKey[NetSecurityConstant::kAeadKeySize];
        uint8 arrOtherKey[NetSecurityConstant::kAeadKeySize];
        SW_ASSERT_TRUE( provider.fillRandomBytes( arrKey, sizeof( arrKey ) ) && provider.fillRandomBytes( arrOtherKey, sizeof( arrOtherKey ) ) );
        unique_ptr<INetAead> aead  = provider.createAead( algorithm, arrKey );
        unique_ptr<INetAead> other = provider.createAead( algorithm, arrOtherKey );
        SW_ASSERT_NOT_NULL( aead.get() );
        SW_ASSERT_NOT_NULL( other.get() );
        const uint8   arrNonce[NetSecurityConstant::kAeadNonceSize] = { 1, 2, 3 };
        const uint8   arrAad[5]                                     = { 9, 9, 9, 9, 9 };
        const uint8   arrPlain[40]                                  = { 42 };
        vector<uint8> sealed( 40 + NetSecurityConstant::kAeadTagSize );
        vector<uint8> opened( 40 );
        SW_ASSERT_TRUE( aead->seal( arrNonce, arrAad, 5, arrPlain, 40, sealed.data() ) );
        SW_EXPECT_TRUE( aead->open( arrNonce, arrAad, 5, sealed.data(), 56, opened.data() ) && opened[0] == 42 );
        sealed[3] ^= 0x01; // 암호문 한 비트
        SW_EXPECT_FALSE( aead->open( arrNonce, arrAad, 5, sealed.data(), 56, opened.data() ) );
        sealed[3] ^= 0x01;
        sealed[55] ^= 0x80; // 태그
        SW_EXPECT_FALSE( aead->open( arrNonce, arrAad, 5, sealed.data(), 56, opened.data() ) );
        sealed[55] ^= 0x80;
        const uint8 arrOtherAad[5] = { 9, 9, 9, 9, 8 };
        SW_EXPECT_FALSE( aead->open( arrNonce, arrOtherAad, 5, sealed.data(), 56, opened.data() ) );
        SW_EXPECT_FALSE( other->open( arrNonce, arrAad, 5, sealed.data(), 56, opened.data() ) ); // 다른 키
        const uint8 arrOtherNonce[NetSecurityConstant::kAeadNonceSize] = { 1, 2, 4 };
        SW_EXPECT_FALSE( aead->open( arrOtherNonce, arrAad, 5, sealed.data(), 56, opened.data() ) );
        SW_EXPECT_FALSE( aead->open( arrNonce, arrAad, 5, sealed.data(), NetSecurityConstant::kAeadTagSize - 1, opened.data() ) ); // 태그보다 짧다
        SW_EXPECT_TRUE( aead->open( arrNonce, arrAad, 5, sealed.data(), 56, opened.data() ) );                                     // 실패 뒤에도 같은 문맥이 다시 연다
    }
}

SW_TEST_CASE( NetSecurityProviderTest, TlsHandshakeCarriesDataAndRefusesDowngradeAndPinMismatch )
{
    TlsPair pair;
    SW_ASSERT_TRUE( makeTlsPair( pair, TlsVersion::Tls13, "" ) );
    SW_EXPECT_TRUE( isHandshakeCarryingData( *pair._serverContext, *pair._clientContext ) );

    // 다운그레이드 — 1.2 까지만 말하는 클라이언트는 1.3 만 받는 서버와 핸드셰이크하지 못한다.
    TlsPair old;
    SW_ASSERT_TRUE( makeTlsPair( old, TlsVersion::Tls12, "" ) );
    unique_ptr<ITlsSession> oldClient = old._clientContext->createSession();
    unique_ptr<ITlsSession> oldServer = old._serverContext->createSession();
    pumpTls( *oldClient, *oldServer );
    SW_EXPECT_TRUE( oldServer->getState() == TlsSessionState::Failed );
    SW_EXPECT_TRUE( oldClient->getState() != TlsSessionState::Established );

    // 고정 불일치 — 체인은 믿지만 고정한 SHA-256 이 다르다.
    TlsPair pinned;
    SW_ASSERT_TRUE( makeTlsPair( pinned, TlsVersion::Tls13, string( 64, '0' ) ) );
    unique_ptr<ITlsSession> pinnedClient = pinned._clientContext->createSession();
    unique_ptr<ITlsSession> pinnedServer = pinned._serverContext->createSession();
    pumpTls( *pinnedClient, *pinnedServer );
    SW_EXPECT_TRUE( pinnedClient->getState() == TlsSessionState::Failed );

    // 맞는 고정 값이면 선다.
    string hex;
    SW_ASSERT_TRUE( EngineNetSecurity::getProvider().computeCertificateSha256( pair._certificatePem, hex ) );
    SW_EXPECT_EQUAL( 64, static_cast<int32>( hex.size() ) );
    TlsContextSettings matching;
    matching._role                       = TlsRole::Client;
    matching._trustPem                   = pair._certificatePem;
    matching._serverName                 = "localhost";
    matching._pinnedCertificateSha256Hex = hex;
    string                  error;
    unique_ptr<ITlsContext> matchingClient = EngineNetSecurity::getProvider().createTlsContext( matching, error );
    SW_ASSERT_NOT_NULL( matchingClient.get() );
    SW_EXPECT_TRUE( isHandshakeCarryingData( *pair._serverContext, *matchingClient ) );

    // 신뢰하지 않는 인증서 — 다른 자체 서명 인증서를 신뢰 목록에 둔 클라이언트는 검증에서 진다.
    TlsPair stranger;
    SW_ASSERT_TRUE( makeTlsPair( stranger, TlsVersion::Tls13, "" ) );
    unique_ptr<ITlsSession> strangerClient = stranger._clientContext->createSession();
    unique_ptr<ITlsSession> otherServer    = pair._serverContext->createSession();
    pumpTls( *strangerClient, *otherServer );
    SW_EXPECT_TRUE( strangerClient->getState() == TlsSessionState::Failed );
}

SW_TEST_CASE( NetSecurityProviderTest, EngineContextsUseDevCertificateWhenPathsAreEmpty )
{
    string                  error;
    unique_ptr<ITlsContext> server = EngineNetSecurity::createServerTlsContext( "", "", "", error );
    SW_ASSERT_TRUE_MSG( server != nullptr, error.c_str() );
    unique_ptr<ITlsContext> client = EngineNetSecurity::createClientTlsContext( "", "localhost", error );
    SW_ASSERT_TRUE_MSG( client != nullptr, error.c_str() );
    SW_EXPECT_TRUE( server->getRole() == TlsRole::Server && client->getRole() == TlsRole::Client );
    SW_EXPECT_TRUE( isHandshakeCarryingData( *server, *client ) );

    // 인증서 · 키 중 하나만 있으면 설정 오류다(개발용으로 메우지 않는다).
    error.clear();
    SW_EXPECT_TRUE( EngineNetSecurity::createServerTlsContext( "server.cert.pem", "", "", error ) == nullptr );
    SW_EXPECT_FALSE( error.empty() );
}

SW_TEST_CASE( NetSecurityProviderTest, Sha256MatchesFips180Vector )
{
    INetSecurityProvider& provider  = EngineNetSecurity::getProvider();
    const utf8            arrText[] = "abc";
    uint8                 arrDigest[NetSecurityConstant::kSha256Size];
    SW_ASSERT_TRUE( provider.computeSha256( reinterpret_cast<const uint8*>( arrText ), 3, arrDigest ) );
    const vector<uint8> expected = fromHex( "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
    SW_EXPECT_EQUAL( 0, std::memcmp( arrDigest, expected.data(), expected.size() ) );
}

SW_TEST_CASE( NetSecurityProviderTest, SignaturesVerifyAndRejectTamperingAndOtherKeys )
{
    INetSecurityProvider&       provider       = EngineNetSecurity::getProvider();
    const NetSignatureAlgorithm arrAlgorithm[] = { NetSignatureAlgorithm::RsaPkcs1Sha256, NetSignatureAlgorithm::EcdsaP256Sha256 };
    const utf8                  arrMessage[]   = "header.payload";
    const uint8*                pMessage       = reinterpret_cast<const uint8*>( arrMessage );
    const int32                 messageSize    = static_cast<int32>( sizeof( arrMessage ) - 1 );
    for ( const NetSignatureAlgorithm algorithm : arrAlgorithm )
    {
        string       privateKeyPem;
        NetPublicKey publicKey;
        SW_ASSERT_TRUE( provider.createSigningKeyPair( algorithm, privateKeyPem, publicKey ) );
        vector<uint8> signature;
        SW_ASSERT_TRUE( provider.signData( algorithm, privateKeyPem, pMessage, messageSize, signature ) );
        if ( algorithm == NetSignatureAlgorithm::EcdsaP256Sha256 )
            SW_EXPECT_EQUAL( size_t( 64 ), signature.size() ); // JWS 형식 r ‖ s
        SW_EXPECT_TRUE( provider.verifySignature( publicKey, pMessage, messageSize, signature.data(), static_cast<int32>( signature.size() ) ) );
        SW_EXPECT_FALSE( provider.verifySignature( publicKey, pMessage, messageSize - 1, signature.data(), static_cast<int32>( signature.size() ) ) );
        vector<uint8> tampered = signature;
        tampered[tampered.size() / 2] ^= 0x01;
        SW_EXPECT_FALSE( provider.verifySignature( publicKey, pMessage, messageSize, tampered.data(), static_cast<int32>( tampered.size() ) ) );
        string       otherPem;
        NetPublicKey otherKey;
        SW_ASSERT_TRUE( provider.createSigningKeyPair( algorithm, otherPem, otherKey ) );
        SW_EXPECT_FALSE( provider.verifySignature( otherKey, pMessage, messageSize, signature.data(), static_cast<int32>( signature.size() ) ) );
        NetPublicKey wrongAlgorithm = publicKey; // 같은 바이트를 다른 알고리즘 키로 — 키를 만들지 못하거나 서명이 맞지 않는다
        wrongAlgorithm._algorithm   = algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 ? NetSignatureAlgorithm::EcdsaP256Sha256 : NetSignatureAlgorithm::RsaPkcs1Sha256;
        SW_EXPECT_FALSE( provider.verifySignature( wrongAlgorithm, pMessage, messageSize, signature.data(), static_cast<int32>( signature.size() ) ) );
    }
}
