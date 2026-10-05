/**
 * @file OpenSslNetSecurityProvider.h
 * @brief OpenSSL 3 로 구현한 `INetSecurityProvider` 입니다. OpenSSL 헤더는 이 폴더의 .cpp 에서만 include 한다(CheckThirdPartyIsolation).
 * @details 다른 코드는 이 헤더가 아니라 `EngineNetSecurity::getProvider()` 로 받는다. 모든 함수는 스레드 안전(문맥을 호출마다 만들거나, 세션 · AEAD 는 한 스레드 몫).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Network/Security/INetSecurityProvider.h"

namespace sw
{
    class OpenSslNetSecurityProvider final : public INetSecurityProvider
    {
    public:
        const utf8* getName() const override { return "OpenSSL"; }

        [[nodiscard]] bool      fillRandomBytes( uint8* pOut, int32 size ) override;
        [[nodiscard]] bool      makeX25519KeyPair( NetX25519KeyPair& outKeyPair ) override;
        [[nodiscard]] bool      computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret ) override;
        [[nodiscard]] bool      computeHkdfSha256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize, uint8* pOut,
                                                   int32 outSize ) override;
        [[nodiscard]] bool      computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params,
                                                     uint8* pOut, int32 outSize ) override;
        unique_ptr<INetAead>    createAead( NetAeadAlgorithm algorithm, const uint8* pKey ) override;
        unique_ptr<ITlsContext> createTlsContext( const TlsContextSettings& settings, string& outError ) override;
        [[nodiscard]] bool      createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem ) override;
        [[nodiscard]] bool      computeCertificateSha256( const string& certificatePem, string& outHex ) override;
    };
} // namespace sw
