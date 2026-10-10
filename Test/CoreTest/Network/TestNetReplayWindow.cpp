#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Security/NetReplayWindow.h"
#include "Core/Network/Security/NetSessionKeyUtil.h"

#include "TestFramework/TestFramework.h"

#include <cstring>

// 재전송 방지 창 — 처음 보는 번호 · 창 안의 뒤바뀐 순서는 받고, 본 번호 · 창보다 옛 번호는 거절. 창을 넘어 뛰면 옛 칸이 비워진다.
// 세션 키 유도 — HKDF 에 넘기는 소금 · 정보의 바이트 배치와 출력 나누기, nonce = IV XOR 패킷 번호. 암호 구현은 Engine 이라 HKDF 는 입력을 적는 가짜 제공자로 본다.

using namespace sw;

namespace
{
    /** @brief HKDF 입력을 적고 출력 바이트를 0, 1, 2 … 로 채우는 가짜 제공자입니다. 나머지 연산은 쓰이지 않는다. */
    class RecordingSecurityProvider final : public INetSecurityProvider
    {
    public:
        const utf8* getName() const override { return "Recording"; }

        bool fillRandomBytes( uint8* pOut, int32 size ) override
        {
            (void)pOut;
            (void)size;
            return false;
        }
        bool makeX25519KeyPair( NetX25519KeyPair& outKeyPair ) override
        {
            (void)outKeyPair;
            return false;
        }
        bool computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret ) override
        {
            (void)pPrivateKey;
            (void)pPeerPublicKey;
            (void)pOutSecret;
            return false;
        }
        bool computeHkdfSha256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize, uint8* pOut,
                                int32 outSize ) override
        {
            _secret.assign( pSecret, pSecret + secretSize );
            _salt.assign( pSalt, pSalt + saltSize );
            _info.assign( pInfo, pInfo + infoSize );
            for ( int32 index = 0; index < outSize; ++index )
            {
                pOut[index] = static_cast<uint8>( index );
            }
            return true;
        }
        bool computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut,
                                  int32 outSize ) override
        {
            (void)pPassword;
            (void)passwordSize;
            (void)pSalt;
            (void)saltSize;
            (void)params;
            (void)pOut;
            (void)outSize;
            return false;
        }
        unique_ptr<INetAead> createAead( NetAeadAlgorithm algorithm, const uint8* pKey ) override
        {
            (void)algorithm;
            (void)pKey;
            return nullptr;
        }
        unique_ptr<ITLSContext> createTLSContext( const TLSContextSettings& settings, string& outError ) override
        {
            (void)settings;
            outError = "not supported";
            return nullptr;
        }
        [[nodiscard]] bool createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem ) override
        {
            (void)commonName;
            (void)validDays;
            (void)outCertificatePem;
            (void)outPrivateKeyPem;
            return false;
        }
        bool computeCertificateSha256( const string& certificatePem, string& outHex ) override
        {
            (void)certificatePem;
            (void)outHex;
            return false;
        }
        bool computeSha256( const uint8* pData, int32 dataSize, uint8* pOutDigest ) override
        {
            (void)pData;
            (void)dataSize;
            (void)pOutDigest;
            return false;
        }
        bool verifySignature( const NetPublicKey& publicKey, const uint8* pData, int32 dataSize, const uint8* pSignature, int32 signatureSize ) override
        {
            (void)publicKey;
            (void)pData;
            (void)dataSize;
            (void)pSignature;
            (void)signatureSize;
            return false;
        }
        [[nodiscard]] bool createSigningKeyPair( NetSignatureAlgorithm algorithm, string& outPrivateKeyPem, NetPublicKey& outPublicKey ) override
        {
            (void)algorithm;
            (void)outPrivateKeyPem;
            (void)outPublicKey;
            return false;
        }
        bool signData( NetSignatureAlgorithm algorithm, const string& privateKeyPem, const uint8* pData, int32 dataSize, vector<uint8>& outSignatureBytes ) override
        {
            (void)algorithm;
            (void)privateKeyPem;
            (void)pData;
            (void)dataSize;
            (void)outSignatureBytes;
            return false;
        }

        vector<uint8> _secret{};
        vector<uint8> _salt{};
        vector<uint8> _info{};
    };
} // namespace

SW_TEST_CASE( NetReplayWindowTest, AcceptsNewAndReorderedRejectsSeenAndStale )
{
    NetReplayWindow window;
    SW_EXPECT_TRUE( window.isAcceptable( 0 ) );
    window.markReceived( 0 );
    SW_EXPECT_FALSE( window.isAcceptable( 0 ) ); // 같은 번호 두 번
    window.markReceived( 5 );
    SW_EXPECT_TRUE( window.isAcceptable( 3 ) ); // 창 안의 늦게 온 번호
    window.markReceived( 3 );
    SW_EXPECT_FALSE( window.isAcceptable( 3 ) );
    SW_EXPECT_TRUE( window.isAcceptable( 4 ) );

    window.markReceived( 2000 );
    SW_EXPECT_FALSE( window.isAcceptable( 2000 - NetReplayWindow::kWindowSize ) ); // 창 밖 — 받았는지 모르니 거절
    // 창 밖의 이 번호가 앉는 칸(975)은 비어 있다 — 칸만 보면 "처음 보는 번호" 로 받아 버린다. 창 너비 검사가 거절한다.
    SW_EXPECT_FALSE( window.isAcceptable( 2000 - NetReplayWindow::kWindowSize - 1 ) );
    SW_EXPECT_TRUE( window.isAcceptable( 2000 - NetReplayWindow::kWindowSize + 1 ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 2000 ), window.getHighest() );

    // 창 너비보다 크게 뛰면 모두 비운다.
    window.markReceived( 1000000 );
    SW_EXPECT_TRUE( window.isAcceptable( 1000000 - 1 ) );
    SW_EXPECT_FALSE( window.isAcceptable( 1000000 ) );

    window.reset();
    SW_EXPECT_FALSE( window.hasReceived() );
    SW_EXPECT_TRUE( window.isAcceptable( 0 ) );
}

SW_TEST_CASE( NetReplayWindowTest, AdvancingClearsSlotsOfOldNumbers )
{
    // 창 너비보다 작게 앞으로 가면 새로 창에 들어오는 번호가 옛 번호와 같은 칸에 앉는다 — 그 칸을 비우지 않으면 처음 보는 번호를 "본 번호" 로 거절한다.
    NetReplayWindow window;
    window.markReceived( 5 );
    window.markReceived( 1000 );
    SW_EXPECT_TRUE( window.isAcceptable( 5 + NetReplayWindow::kWindowSize ) ); // 창보다 앞 — 늘 받는다
    window.markReceived( 1100 );
    // 1029 는 5 가 쓰던 칸(5 % 1024)에 앉고 창(77..1100) 안이다 — 처음 보는 번호다.
    SW_EXPECT_TRUE( window.isAcceptable( 5 + NetReplayWindow::kWindowSize ) );
    SW_EXPECT_FALSE( window.isAcceptable( 1000 ) );
}

SW_TEST_CASE( NetReplayWindowTest, NonceIsIvXorBigEndianPacketNumber )
{
    uint8 arrIv[NetSecurityConstant::kAeadNonceSize];
    for ( int32 index = 0; index < NetSecurityConstant::kAeadNonceSize; ++index )
    {
        arrIv[index] = static_cast<uint8>( 0xA0 + index );
    }
    uint8 arrNonce[NetSecurityConstant::kAeadNonceSize];
    NetSessionKeyUtil::makeNonce( arrIv, 0x0102ull, arrNonce );
    SW_EXPECT_EQUAL( arrIv[0], arrNonce[0] );
    SW_EXPECT_EQUAL( arrIv[9], arrNonce[9] );
    SW_EXPECT_EQUAL( static_cast<uint8>( arrIv[10] ^ 0x01 ), arrNonce[10] );
    SW_EXPECT_EQUAL( static_cast<uint8>( arrIv[11] ^ 0x02 ), arrNonce[11] );
    uint8 arrOther[NetSecurityConstant::kAeadNonceSize];
    NetSessionKeyUtil::makeNonce( arrIv, 0x0103ull, arrOther );
    SW_EXPECT_TRUE( arrOther[11] != arrNonce[11] ); // 번호마다 nonce 가 다르다
}

SW_TEST_CASE( NetReplayWindowTest, SessionKeysBindSaltsProtocolAndSessionSecret )
{
    RecordingSecurityProvider provider;
    uint8                     arrShared[NetSecurityConstant::kX25519KeySize];
    for ( int32 index = 0; index < NetSecurityConstant::kX25519KeySize; ++index )
    {
        arrShared[index] = static_cast<uint8>( 0x40 + index );
    }
    NetSessionKeys keys;
    SW_ASSERT_TRUE( NetSessionKeyUtil::computeSessionKeys( provider, arrShared, nullptr, 0x1122334455667788ull, 0x0102030405060708ull, 0xAABBCCDDu, keys ) );

    SW_EXPECT_EQUAL( NetSecurityConstant::kX25519KeySize, static_cast<int32>( provider._secret.size() ) );
    SW_EXPECT_TRUE( std::memcmp( provider._secret.data(), arrShared, sizeof( arrShared ) ) == 0 );
    // 세션 비밀이 없으면 소금은 0 32 바이트.
    SW_EXPECT_TRUE( provider._salt == vector<uint8>( NetSecurityConstant::kSha256Size, 0 ) );
    // 정보 = "sw-net-v1" ‖ 클라이언트 소금(LE 8) ‖ 서버 소금(LE 8) ‖ 프로토콜 id(LE 4).
    const vector<uint8> expectedInfo = { 's', 'w', '-', 'n', 'e', 't', '-', 'v', '1', 0x88, 0x77, 0x66, 0x55, 0x44, 0x33,
                                         0x22, 0x11, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0xDD, 0xCC, 0xBB, 0xAA };
    SW_EXPECT_TRUE( provider._info == expectedInfo );
    // 출력 88 바이트 = 클라이언트→서버 키 32 · IV 12 · 서버→클라이언트 키 32 · IV 12 순.
    SW_EXPECT_EQUAL( 0, static_cast<int32>( keys._clientToServer._arrKey[0] ) );
    SW_EXPECT_EQUAL( 32, static_cast<int32>( keys._clientToServer._arrIv[0] ) );
    SW_EXPECT_EQUAL( 44, static_cast<int32>( keys._serverToClient._arrKey[0] ) );
    SW_EXPECT_EQUAL( 76, static_cast<int32>( keys._serverToClient._arrIv[0] ) );
    SW_EXPECT_EQUAL( 87, static_cast<int32>( keys._serverToClient._arrIv[NetSecurityConstant::kAeadNonceSize - 1] ) );

    // 세션 비밀이 있으면 그것이 소금이다 — 비밀을 모르는 중간자는 같은 키를 못 만든다.
    NetSessionSecret secret;
    for ( int32 index = 0; index < NetSecurityConstant::kSha256Size; ++index )
    {
        secret._arrByte[index] = static_cast<uint8>( 0xF0 ^ index );
    }
    SW_ASSERT_TRUE( NetSessionKeyUtil::computeSessionKeys( provider, arrShared, &secret, 1, 2, 3, keys ) );
    SW_EXPECT_TRUE( std::memcmp( provider._salt.data(), secret._arrByte, sizeof( secret._arrByte ) ) == 0 );

    keys.wipe();
    SW_EXPECT_EQUAL( 0, static_cast<int32>( keys._serverToClient._arrIv[NetSecurityConstant::kAeadNonceSize - 1] ) );

    uint8 arrProofKey[NetSecurityConstant::kAeadKeySize];
    SW_ASSERT_TRUE( NetSessionKeyUtil::computeProofKey( provider, secret, arrProofKey ) );
    SW_EXPECT_TRUE( provider._salt.empty() );
    SW_EXPECT_TRUE( provider._info == vector<uint8>( { 's', 'w', '-', 'n', 'e', 't', '-', 'p', 'r', 'o', 'o', 'f', '-', 'v', '1' } ) );
}
