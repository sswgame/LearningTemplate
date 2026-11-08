/**
 * @file FakeNetSecurityProvider.h
 * @brief 온라인 키트 테스트가 함께 쓰는 가짜 보안 제공자입니다. 키가 없어도 서명 · 사슬 확인 규칙을 가짜 답으로 볼 수 있게 합니다(`OnlineHostTestUtil.h` 와 같은 자리).
 */
#pragma once
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"

namespace sw::test
{
    /**
     * @brief 키 · nonce · AAD · 암호문을 FNV-1a 로 섞은 값을 태그로 쓰는 가짜 AEAD 입니다. 평문은 키 첫 바이트와 XOR 합니다.
     * @details 암호가 아니다 — 봉인 형식 · 문맥(AAD) · 변조 거절 규칙을 키 없이 보는 용도이다. 키나 AAD 나 nonce 나 암호문이 하나라도 다르면 열리지 않는다.
     */
    class FakeAead final : public INetAead
    {
    public:
        explicit FakeAead( const uint8* pKey ) { Memory::copy( _arrKey, pKey, sizeof( _arrKey ) ); }

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
            for ( int32 index = 0; index < NetSecurityConstant::kAeadTagSize; ++index )
            {
                if ( arrTag[index] != pCipher[plainSize + index] )
                    return false;
            }
            for ( int32 index = 0; index < plainSize; ++index )
            {
                pOut[index] = static_cast<uint8>( pCipher[index] ^ _arrKey[0] );
            }
            return true;
        }

    private:
        static void mix( uint64& inoutHash, const uint8* pData, int32 size )
        {
            for ( int32 index = 0; index < size; ++index )
            {
                inoutHash = HashUtil::fnvStep64( inoutHash, pData[index] );
            }
        }

        void computeTag( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pCipher, int32 cipherSize, uint8* pOutTag ) const
        {
            uint64 hash = HashUtil::kFNVOffset64;
            mix( hash, _arrKey, static_cast<int32>( sizeof( _arrKey ) ) );
            mix( hash, pNonce, NetSecurityConstant::kAeadNonceSize );
            mix( hash, pAad, aadSize );
            mix( hash, pCipher, cipherSize );
            for ( int32 index = 0; index < NetSecurityConstant::kAeadTagSize; ++index )
            {
                hash           = HashUtil::fnvStep64( hash, static_cast<uint64>( index ) );
                pOutTag[index] = static_cast<uint8>( hash >> 56 );
            }
        }

    private:
        uint8 _arrKey[NetSecurityConstant::kAeadKeySize]{};
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @brief 서명 쪽을 답하는 가짜 제공자입니다. 사슬 확인은 `_bChainOk`, 서명 만들기는 `_bSignOk` 를 따르고 서명 확인은 늘 맞다.
     * @details 난수(`_bRandomOk`, 셈 값으로 채운다), AEAD(`_bAeadOk`, `FakeAead`), SHA-256(`_bHashOk`, `computeFakeDigest`)은 켠 테스트만 쓴다 — 기본은 실패라 쓰지 않는 테스트의 길이 바뀌지 않는다. 나머지 연산은 쓰이지 않는다.
     */
    class FakeNetSecurityProvider final : public INetSecurityProvider
    {
    public:
        uint8 _bChainOk{ SW_TRUE };
        uint8 _bSignOk{ SW_TRUE };
        uint8 _bRandomOk{ SW_FALSE };
        uint8 _bAeadOk{ SW_FALSE };
        uint8 _bHashOk{ SW_FALSE }; ///< 참이면 computeSHA256 이 가짜 요약(`computeFakeDigest`)을 준다
        uint8 _nextRandomByte{ 1 }; ///< 난수 대신 채우는 셈 값이다. 부를 때마다 이어지므로 nonce 가 겹치지 않는다

        const utf8* getName() const override { return "FakeNetSecurity"; }
        bool        fillRandomBytes( uint8* pOut, int32 size ) override
        {
            if ( _bRandomOk == SW_FALSE )
                return false;
            for ( int32 index = 0; index < size; ++index )
            {
                pOut[index] = _nextRandomByte++;
            }
            return true;
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
        bool computeHkdfSHA256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize, uint8* pOut, int32 outSize ) override
        {
            (void)pSecret;
            (void)secretSize;
            (void)pSalt;
            (void)saltSize;
            (void)pInfo;
            (void)infoSize;
            (void)pOut;
            (void)outSize;
            return false;
        }
        bool computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params, uint8* pOut, int32 outSize ) override
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
            if ( _bAeadOk == SW_FALSE )
                return nullptr;
            return make_unique<FakeAead>( pKey );
        }
        expected<unique_ptr<ITLSContext>> createTLSContext( const TLSContextConfig& settings ) override
        {
            (void)settings;
            return unexpected( Error{ ErrorKind::Unsupported, "not supported" } );
        }
        bool createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem ) override
        {
            (void)commonName;
            (void)validDays;
            (void)outCertificatePem;
            (void)outPrivateKeyPem;
            return false;
        }
        bool computeCertificateSHA256( const string& certificatePem, string& outHex ) override
        {
            (void)certificatePem;
            (void)outHex;
            return false;
        }
        bool computeSHA256( const uint8* pData, int32 dataSize, uint8* pOutDigest ) override
        {
            if ( _bHashOk == SW_FALSE )
                return false;
            computeFakeDigest( pData, dataSize, pOutDigest );
            return true;
        }

        /** @brief 가짜 SHA-256(`_bHashOk` 일 때 `computeSHA256` 이 쓰는 값) — 입력을 FNV-1a 로 섞어 32 바이트로 편다. 테스트가 기대값을 같은 식으로 만든다. */
        static void computeFakeDigest( const uint8* pData, int32 dataSize, uint8* pOutDigest )
        {
            uint64 hash = HashUtil::kFNVOffset64;
            for ( int32 index = 0; index < dataSize; ++index )
            {
                hash = HashUtil::fnvStep64( hash, pData[index] );
            }
            for ( int32 index = 0; index < NetSecurityConstant::kSHA256Size; ++index )
            {
                hash              = HashUtil::fnvStep64( hash, static_cast<uint64>( index ) );
                pOutDigest[index] = static_cast<uint8>( hash >> 56 );
            }
        }
        bool verifySignature( const NetPublicKey& publicKey, const uint8* pData, int32 dataSize, const uint8* pSignature, int32 signatureSize ) override
        {
            (void)publicKey;
            (void)pData;
            (void)dataSize;
            (void)pSignature;
            (void)signatureSize;
            return true;
        }
        bool createSigningKeyPair( NetSignatureAlgorithm algorithm, string& outPrivateKeyPem, NetPublicKey& outPublicKey ) override
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
            outSignatureBytes.assign( 1, uint8( 's' ) );
            return _bSignOk == SW_TRUE;
        }
        bool verifyCertificateChain( const vector<vector<uint8>>& listChainDer, const string& trustedRootsPem, int64 unixSeconds, string_view leafRequiredOid, string_view issuerRequiredOid,
                                     NetPublicKey& outLeafPublicKey ) override
        {
            (void)listChainDer;
            (void)trustedRootsPem;
            (void)unixSeconds;
            (void)leafRequiredOid;
            (void)issuerRequiredOid;
            outLeafPublicKey            = NetPublicKey{};
            outLeafPublicKey._algorithm = NetSignatureAlgorithm::EcdsaP256Sha256;
            return _bChainOk == SW_TRUE;
        }
    };
} // namespace sw::test
