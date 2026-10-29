/**
 * @file OpenSSLNetSecurityProvider.h
 * @brief OpenSSL 3 로 구현한 `INetSecurityProvider` 입니다. OpenSSL 헤더는 이 폴더의 .cpp 에서만 include 한다.
 * @details GameFramework 의 모듈 서브시스템이다(`SW_MODULE_SUBSYSTEM( Process, … )`) — 프로세스 범위 아래 GameFramework 의 자식 범위에 서서 게임 · 키트 모듈 리로드와
 *          엔진 기동 단계 다시 세우기에 살아남고, GameFramework 가 내려갈 때 함께 걷힌다. 다른 코드는 이 헤더가 아니라 공개한 인터페이스로 찾는다
 *          (`game::getSubsystem<INetSecurityProvider>()`, 엔진은 `engine::findSubsystem<INetSecurityProvider>()`). 언리얼 FSSLModule 처럼 모듈이 소유하고,
 *          함수 정적 객체 · 엔진 쪽 함수 칸을 두지 않는다.
 *          모든 함수는 스레드 안전하다(컨텍스트를 호출마다 만들거나, 세션과 AEAD 는 한 스레드가 전담한다). 상태가 없어 초기화 · 종료가 할 일이 없다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class OpenSSLNetSecurityProvider final : public INetSecurityProvider, public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "NetSecurity";

        // ISubsystem — 상태가 없다(컨텍스트 · 세션은 호출마다 만들어 부른 쪽이 소유한다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        const utf8* getName() const override { return "OpenSSL"; }

        [[nodiscard]] bool      fillRandomBytes( uint8* pOut, int32 size ) override;
        [[nodiscard]] bool      makeX25519KeyPair( NetX25519KeyPair& outKeyPair ) override;
        [[nodiscard]] bool      computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret ) override;
        [[nodiscard]] bool      computeHkdfSHA256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize, uint8* pOut,
                                                   int32 outSize ) override;
        [[nodiscard]] bool      computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params,
                                                     uint8* pOut, int32 outSize ) override;
        unique_ptr<INetAead>    createAead( NetAeadAlgorithm algorithm, const uint8* pKey ) override;
        unique_ptr<ITLSContext> createTLSContext( const TLSContextConfig& settings, string& outError ) override;
        [[nodiscard]] bool      createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem ) override;
        [[nodiscard]] bool      computeCertificateSHA256( const string& certificatePem, string& outHex ) override;
        [[nodiscard]] bool      computeSHA256( const uint8* pData, int32 dataSize, uint8* pOutDigest ) override;
        [[nodiscard]] bool      verifySignature( const NetPublicKey& publicKey, const uint8* pData, int32 dataSize, const uint8* pSignature, int32 signatureSize ) override;
        [[nodiscard]] bool      createSigningKeyPair( NetSignatureAlgorithm algorithm, string& outPrivateKeyPem, NetPublicKey& outPublicKey ) override;
        [[nodiscard]] bool      signData( NetSignatureAlgorithm algorithm, const string& privateKeyPem, const uint8* pData, int32 dataSize, vector<uint8>& outSignatureBytes ) override;
        [[nodiscard]] bool      verifyCertificateChain( const vector<vector<uint8>>& listChainDer, const string& trustedRootsPem, int64 unixSeconds, string_view leafRequiredOid,
                                                        string_view issuerRequiredOid, NetPublicKey& outLeafPublicKey ) override;
        [[nodiscard]] bool      createTestCertificateChain( int32 validDays, string_view leafOid, string_view issuerOid, vector<string>& outListChainPem,
                                                            string& outLeafPrivateKeyPem ) override;
    };
} // namespace sw
