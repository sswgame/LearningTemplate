/**
 * @file FakeNetSecurityProvider.h
 * @brief 온라인 키트 테스트가 함께 쓰는 가짜 보안 제공자입니다. 키가 없어도 서명 · 사슬 확인 규칙을 가짜 답으로 볼 수 있게 합니다(`OnlineHostTestUtil.h` 와 같은 자리).
 */
#pragma once
#include "Core/Network/Security/INetSecurityProvider.h"

namespace sw::test
{
    /** @brief 서명 쪽만 답하는 가짜 제공자입니다. 사슬 확인은 `_bChainOk`, 서명 만들기는 `_bSignOk` 를 따르고 서명 확인은 늘 맞다. 나머지 연산은 쓰이지 않는다. */
    class FakeNetSecurityProvider final : public INetSecurityProvider
    {
    public:
        uint8 _bChainOk{ SW_TRUE };
        uint8 _bSignOk{ SW_TRUE };

        const utf8* getName() const override { return "FakeNetSecurity"; }
        bool        fillRandomBytes( uint8* pOut, int32 size ) override
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
            (void)pKey;
            return nullptr;
        }
        unique_ptr<ITLSContext> createTLSContext( const TLSContextConfig& settings, string& outError ) override
        {
            (void)settings;
            outError = "not supported";
            return nullptr;
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
