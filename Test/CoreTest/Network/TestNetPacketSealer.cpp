#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetPacketSealer.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Serialization/BitStream.h"

#include "TestFramework/TestFramework.h"

#include <cstring>

// NetPacketSealer — 봉인한 본문을 반대 방향 키로 열고, 재전송 · 변조 · 짧은 본문을 결과 종류로 가른다.
// 키 유도는 양쪽이 같은 키를 만들고, 키 확인 태그와 증명 태그는 입력(슬롯 번호 · 솔트 · 토큰)이 하나라도 다르면 맞지 않는다.
// 암호 구현은 Engine 이라 바이트를 섞는 가짜 제공자로 본다. 가짜 X25519 는 공개 키 = 비밀 키, 공유 비밀 = 둘의 XOR 이라 양쪽이 같은 값을 얻는다.

using namespace sw;

namespace
{
    /** @brief 바이트를 FNV-1a 64 로 섞습니다. 가짜 AEAD 태그와 가짜 HKDF 출력에 씁니다. */
    struct ByteMixer
    {
        static void mix( uint64& inoutHash, const uint8* pData, int32 size )
        {
            for ( int32 index = 0; index < size; ++index )
            {
                inoutHash = HashUtil::fnvStep64( inoutHash, pData[index] );
            }
        }

        /** @brief @p hash 에서 바이트 @p size 개를 뽑습니다. */
        static void spread( uint64 hash, uint8* pOut, int32 size )
        {
            for ( int32 index = 0; index < size; ++index )
            {
                hash        = HashUtil::fnvStep64( hash, static_cast<uint64>( index ) );
                pOut[index] = static_cast<uint8>( hash >> 56 );
            }
        }
    };

    /** @brief 키 · nonce · AAD · 암호문을 섞은 값을 태그로 쓰는 가짜 AEAD 입니다. 평문은 키 첫 바이트와 XOR 합니다. */
    class MixingAead final : public INetAead
    {
    public:
        explicit MixingAead( const uint8* pKey ) { std::memcpy( _arrKey, pKey, sizeof( _arrKey ) ); }

        [[nodiscard]] bool seal( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pPlain, int32 plainSize, uint8* pOut ) override
        {
            for ( int32 index = 0; index < plainSize; ++index )
            {
                pOut[index] = static_cast<uint8>( pPlain[index] ^ _arrKey[0] );
            }
            computeTag( pNonce, pAad, aadSize, pOut, plainSize, pOut + plainSize );
            return true;
        }

        [[nodiscard]] bool open( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pCipher, int32 cipherSize, uint8* pOut ) override
        {
            const int32 plainSize = cipherSize - NetSecurityConstant::kAeadTagSize;
            if ( plainSize < 0 )
                return false;
            uint8 arrTag[NetSecurityConstant::kAeadTagSize];
            computeTag( pNonce, pAad, aadSize, pCipher, plainSize, arrTag );
            if ( std::memcmp( arrTag, pCipher + plainSize, sizeof( arrTag ) ) != 0 )
                return false;
            for ( int32 index = 0; index < plainSize; ++index )
            {
                pOut[index] = static_cast<uint8>( pCipher[index] ^ _arrKey[0] );
            }
            return true;
        }

    private:
        void computeTag( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pCipher, int32 cipherSize, uint8* pOutTag ) const
        {
            uint64 hash = HashUtil::kFnvOffset64;
            ByteMixer::mix( hash, _arrKey, static_cast<int32>( sizeof( _arrKey ) ) );
            ByteMixer::mix( hash, pNonce, NetSecurityConstant::kAeadNonceSize );
            ByteMixer::mix( hash, pAad, aadSize );
            ByteMixer::mix( hash, pCipher, cipherSize );
            ByteMixer::spread( hash, pOutTag, NetSecurityConstant::kAeadTagSize );
        }

        uint8 _arrKey[NetSecurityConstant::kAeadKeySize]{};
    };

    /** @brief 가짜 키 교환 · HKDF · AEAD 만 하는 제공자입니다. 나머지 연산은 쓰이지 않는다. */
    class MixingSecurityProvider final : public INetSecurityProvider
    {
    public:
        const utf8* getName() const override { return "Mixing"; }

        bool fillRandomBytes( uint8* pOut, int32 size ) override
        {
            (void)pOut;
            (void)size;
            return false;
        }
        bool makeX25519KeyPair( NetX25519KeyPair& outKeyPair ) override
        {
            ++_keyPairCount;
            for ( int32 index = 0; index < NetSecurityConstant::kX25519KeySize; ++index )
            {
                outKeyPair._arrPrivateKey[index] = static_cast<uint8>( _keyPairCount * 37 + index );
                outKeyPair._arrPublicKey[index]  = outKeyPair._arrPrivateKey[index];
            }
            return true;
        }
        bool computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret ) override
        {
            for ( int32 index = 0; index < NetSecurityConstant::kX25519KeySize; ++index )
            {
                pOutSecret[index] = static_cast<uint8>( pPrivateKey[index] ^ pPeerPublicKey[index] );
            }
            return true;
        }
        bool computeHkdfSha256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize, uint8* pOut,
                                int32 outSize ) override
        {
            uint64 hash = HashUtil::kFnvOffset64;
            ByteMixer::mix( hash, pSecret, secretSize );
            ByteMixer::mix( hash, pSalt, saltSize );
            ByteMixer::mix( hash, pInfo, infoSize );
            ByteMixer::spread( hash, pOut, outSize );
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
            return make_unique<MixingAead>( pKey );
        }
        unique_ptr<ITLSContext> createTLSContext( const TLSContextConfig& settings, string& outError ) override
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

        int32 _keyPairCount{ 0 };
    };

    struct NetPacketSealerTestInternal
    {
        static constexpr uint64 kClientSalt = 0x1122334455667788ull;
        static constexpr uint64 kServerSalt = 0x0102030405060708ull;
        static constexpr uint32 kProtocolID = 0xAABBCCDDu;

        /** @brief 클라이언트와 서버의 암호 상태를 같은 키로 만듭니다. */
        static bool makeKeyedPair( MixingSecurityProvider& provider, NetSlotSecurity& outClient, NetSlotSecurity& outServer )
        {
            if ( provider.makeX25519KeyPair( outClient._keyPair ) == false )
                return false;
            if ( NetPacketSealer::deriveServerKeys( provider, NetAeadAlgorithm::Aes256Gcm, outClient._keyPair._arrPublicKey, nullptr, kClientSalt, kServerSalt, kProtocolID,
                                                    outServer ) == false )
                return false;
            return NetPacketSealer::deriveClientKeys( provider, NetAeadAlgorithm::Aes256Gcm, outServer._keyPair._arrPublicKey, nullptr, kClientSalt, kServerSalt, kProtocolID,
                                                      outClient );
        }
    };
} // namespace

SW_TEST_CASE( NetPacketSealerTest, SealedBodyOpensOnceWithTheOppositeKey )
{
    MixingSecurityProvider provider;
    NetSlotSecurity        client;
    NetSlotSecurity        server;
    SW_ASSERT_TRUE( NetPacketSealerTestInternal::makeKeyedPair( provider, client, server ) );
    // 서버는 키를 만든 뒤 비밀 키를 지운다(공개 키는 수락을 다시 보낼 때 쓴다).
    SW_EXPECT_EQUAL( 0, static_cast<int32>( server._keyPair._arrPrivateKey[1] ) );
    SW_EXPECT_TRUE( server._keyPair._arrPublicKey[1] != 0 );

    NetPacketSealer clientSealer;
    NetPacketSealer serverSealer;
    BitWriter       writer;
    const uint8     arrPlain[] = { 10, 20, 30, 40, 50 };
    SW_ASSERT_TRUE( clientSealer.seal( client, 5, 0xCAFEBABEu, NetPacketSealerTestInternal::kProtocolID, false, arrPlain, 5, writer ) );
    SW_EXPECT_EQUAL( NetPacketSealer::kSecureHeadSize + 5 + NetSecurityConstant::kAeadTagSize, writer.getByteCount() );
    const vector<uint8> body( writer.getBytes().begin(), writer.getBytes().begin() + writer.getByteCount() );
    SW_EXPECT_EQUAL( 5, static_cast<int32>( body[0] ) );    // 종류
    SW_EXPECT_EQUAL( 0xBE, static_cast<int32>( body[1] ) ); // 연결 값은 리틀 엔디언
    SW_EXPECT_EQUAL( 0, static_cast<int32>( body[12] ) );   // 첫 패킷 번호 0 의 마지막 바이트(빅 엔디언)
    SW_EXPECT_EQUAL( static_cast<uint64>( 1 ), client._sendPacketNumber );

    SW_ASSERT_TRUE( serverSealer.open( server, NetPacketSealerTestInternal::kProtocolID, true, body.data(), static_cast<int32>( body.size() ) ) ==
                    NetPacketSealer::OpenResult::Opened );
    SW_ASSERT_EQUAL( 5, static_cast<int32>( serverSealer.getOpened().size() ) );
    SW_EXPECT_TRUE( std::memcmp( serverSealer.getOpened().data(), arrPlain, sizeof( arrPlain ) ) == 0 );

    // 같은 번호를 다시 받으면 재전송이다.
    SW_EXPECT_TRUE( serverSealer.open( server, NetPacketSealerTestInternal::kProtocolID, true, body.data(), static_cast<int32>( body.size() ) ) ==
                    NetPacketSealer::OpenResult::Replayed );
    // 같은 방향 키(서버가 서버 키로 연다)로는 열리지 않는다.
    SW_EXPECT_TRUE( serverSealer.open( client, NetPacketSealerTestInternal::kProtocolID, true, body.data(), static_cast<int32>( body.size() ) ) ==
                    NetPacketSealer::OpenResult::AuthenticationFailed );
    // 헤더와 태그보다 짧은 본문.
    SW_EXPECT_TRUE( serverSealer.open( server, NetPacketSealerTestInternal::kProtocolID, true, body.data(), NetPacketSealer::kSecureHeadSize ) ==
                    NetPacketSealer::OpenResult::Malformed );
}

SW_TEST_CASE( NetPacketSealerTest, TamperedOrForeignBodyFailsWithoutBurningThePacketNumber )
{
    MixingSecurityProvider provider;
    NetSlotSecurity        client;
    NetSlotSecurity        server;
    SW_ASSERT_TRUE( NetPacketSealerTestInternal::makeKeyedPair( provider, client, server ) );
    NetPacketSealer sealer;
    BitWriter       writer;
    const uint8     arrPlain[] = { 1, 2, 3 };
    SW_ASSERT_TRUE( sealer.seal( client, 5, 7u, NetPacketSealerTestInternal::kProtocolID, false, arrPlain, 3, writer ) );
    vector<uint8> body( writer.getBytes().begin(), writer.getBytes().begin() + writer.getByteCount() );

    vector<uint8> tampered = body;
    tampered[NetPacketSealer::kSecureHeadSize] ^= 0x01u;
    SW_EXPECT_TRUE( sealer.open( server, NetPacketSealerTestInternal::kProtocolID, true, tampered.data(), static_cast<int32>( tampered.size() ) ) ==
                    NetPacketSealer::OpenResult::AuthenticationFailed );
    // 다른 프로토콜 ID 는 AAD 가 달라 열리지 않는다.
    SW_EXPECT_TRUE( sealer.open( server, NetPacketSealerTestInternal::kProtocolID + 1, true, body.data(), static_cast<int32>( body.size() ) ) ==
                    NetPacketSealer::OpenResult::AuthenticationFailed );
    // 실패한 열기는 번호를 표시하지 않는다 — 진짜 패킷은 그 뒤에도 열린다.
    SW_EXPECT_TRUE( sealer.open( server, NetPacketSealerTestInternal::kProtocolID, true, body.data(), static_cast<int32>( body.size() ) ) ==
                    NetPacketSealer::OpenResult::Opened );
}

SW_TEST_CASE( NetPacketSealerTest, KeyConfirmTagBindsSlotIndexAndServerSalt )
{
    MixingSecurityProvider provider;
    NetSlotSecurity        client;
    NetSlotSecurity        server;
    SW_ASSERT_TRUE( NetPacketSealerTestInternal::makeKeyedPair( provider, client, server ) );
    SW_ASSERT_TRUE( NetPacketSealer::sealKeyConfirmTag( server, 3, NetPacketSealerTestInternal::kServerSalt ) );
    SW_EXPECT_TRUE( NetPacketSealer::isKeyConfirmTagValid( client, 3, NetPacketSealerTestInternal::kServerSalt, server._arrKeyConfirmTag ) );
    SW_EXPECT_FALSE( NetPacketSealer::isKeyConfirmTagValid( client, 4, NetPacketSealerTestInternal::kServerSalt, server._arrKeyConfirmTag ) );
    SW_EXPECT_FALSE( NetPacketSealer::isKeyConfirmTagValid( client, 3, NetPacketSealerTestInternal::kServerSalt + 1, server._arrKeyConfirmTag ) );
}

SW_TEST_CASE( NetPacketSealerTest, ProofTagBindsSecretSaltsKeyAndToken )
{
    MixingSecurityProvider provider;
    NetSessionSecret       secret;
    for ( int32 index = 0; index < NetSecurityConstant::kSha256Size; ++index )
    {
        secret._arrByte[index] = static_cast<uint8>( 0xF0 ^ index );
    }
    uint8 arrPublic[NetSecurityConstant::kX25519KeySize]{};
    arrPublic[0] = 9;
    uint8 arrToken[NetSecurityConstant::kMaxTokenSize]{};
    arrToken[0] = 0x42;
    uint8 arrTag[NetSecurityConstant::kAeadTagSize]{};
    SW_ASSERT_TRUE( NetPacketSealer::computeProofTag( provider, NetAeadAlgorithm::Aes256Gcm, secret, 1, 2, arrPublic, arrToken, 8, arrTag ) );
    SW_EXPECT_TRUE( NetPacketSealer::isProofTagValid( provider, NetAeadAlgorithm::Aes256Gcm, secret, 1, 2, arrPublic, arrToken, 8, arrTag ) );
    SW_EXPECT_FALSE( NetPacketSealer::isProofTagValid( provider, NetAeadAlgorithm::Aes256Gcm, secret, 1, 3, arrPublic, arrToken, 8, arrTag ) );
    SW_EXPECT_FALSE( NetPacketSealer::isProofTagValid( provider, NetAeadAlgorithm::Aes256Gcm, secret, 1, 2, arrPublic, arrToken, 7, arrTag ) );
    NetSessionSecret otherSecret = secret;
    otherSecret._arrByte[0] ^= 0x01u;
    SW_EXPECT_FALSE( NetPacketSealer::isProofTagValid( provider, NetAeadAlgorithm::Aes256Gcm, otherSecret, 1, 2, arrPublic, arrToken, 8, arrTag ) );
    // 상한을 넘는 토큰은 AAD 를 만들지 않고 거절한다(스택 버퍼를 넘지 않는다).
    SW_EXPECT_FALSE( NetPacketSealer::computeProofTag( provider, NetAeadAlgorithm::Aes256Gcm, secret, 1, 2, arrPublic, arrToken, NetSecurityConstant::kMaxTokenSize + 1,
                                                       arrTag ) );
}
