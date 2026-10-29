/**
 * @file INetSecurityProvider.h
 * @brief 네트워크 암호 API — AEAD · X25519 · HKDF-SHA256 · 느린 비밀번호 해시(Argon2id) · 난수 · 메모리 위 TLS 1.3 세션 · 개발용 인증서.
 *        Core 는 이 인터페이스만 알고 구현(OpenSSL)은 GameFramework 가 줍니다(모듈 서브시스템 `OpenSSLNetSecurityProvider` — 프로세스 범위에 공개하고 이 인터페이스 타입으로 찾는다, `GameFramework/Base/Online/Security`).
 * @details 암호를 직접 짜지 않는다. TLS 세션은 소켓을 모른다 — 엔드포인트가 받은 암호문을 넣고 평문을 꺼낸다(OpenSSL 메모리 BIO · SChannel 버퍼와 같은 모양).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/NetSecurityTypes.h"

namespace sw
{
    /**
     * @class INetAead
     * @brief AEAD 하나(키 고정)입니다. `seal` 은 암호문 뒤에 태그 16 바이트를 붙이고, `open` 은 태그가 틀리면 false 입니다.
     * @details 한 스레드가 씁니다(연결마다 하나). nonce 는 키마다 한 번만 — 부르는 쪽이 패킷 번호로 만든다(`NetSessionKeyUtil::makeNonce`).
     */
    class SW_API INetAead
    {
    public:
        INetAead()          = default;
        virtual ~INetAead() = default;

        INetAead( const INetAead& )            = delete;
        INetAead& operator=( const INetAead& ) = delete;

        /** @brief @p pOut 은 plainSize + kAeadTagSize 바이트입니다. */
        [[nodiscard]] virtual bool seal( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pPlain, int32 plainSize, uint8* pOut ) = 0;
        /** @brief @p cipherSize 는 태그 포함입니다. @p pOut 은 cipherSize - kAeadTagSize 바이트. 태그가 틀리면 false 이고 @p pOut 은 믿지 않는다. */
        [[nodiscard]] virtual bool open( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pCipher, int32 cipherSize, uint8* pOut ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITLSSession
     * @brief 메모리 위의 TLS 세션입니다 — 소켓을 모른다. 받은 암호문을 넣고 평문을 꺼내고, 평문을 넣고 보낼 암호문을 꺼냅니다.
     * @details 한 스레드가 씁니다(연결마다 하나). 핸드셰이크 · 레코드 검증이 실패하면 상태가 `Failed` 가 되고 다시 살아나지 않는다.
     */
    class SW_API ITLSSession
    {
    public:
        ITLSSession()          = default;
        virtual ~ITLSSession() = default;

        ITLSSession( const ITLSSession& )            = delete;
        ITLSSession& operator=( const ITLSSession& ) = delete;

        virtual TLSSessionState getState() const = 0;
        /** @brief 저쪽에서 받은 암호문을 넣습니다. false = 실패(변조 · 핸드셰이크 실패) — 상태 `Failed`. */
        [[nodiscard]] virtual bool feedCiphertext( const uint8* pData, int32 size ) = 0;
        /** @brief 풀린 평문을 @p outBytes 뒤에 붙입니다. false = 실패. */
        [[nodiscard]] virtual bool readPlaintext( vector<uint8>& outBytes ) = 0;
        /** @brief 보낼 평문을 넣습니다. 핸드셰이크 전이면 모아 두었다가 핸드셰이크가 끝나면 보낸다. */
        [[nodiscard]] virtual bool writePlaintext( const uint8* pData, int32 size ) = 0;
        /** @brief 저쪽에 보낼 레코드를 @p outBytes 뒤에 붙입니다. */
        virtual void takeCiphertext( vector<uint8>& outBytes ) = 0;
        /** @brief close_notify 를 보냅니다(보낼 레코드는 `takeCiphertext` 로). */
        virtual void        close()                = 0;
        virtual const utf8* getFailureText() const = 0;
        /** @brief 핸드셰이크가 끝난 뒤 협상된 ALPN 프로토콜 이름입니다. 협상하지 않았거나 합의가 없으면 빈 문자열(`TLSContextConfig::_alpnProtocol`). */
        virtual string getNegotiatedProtocol() const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ITLSContext
     * @brief TLS 컨텍스트(인증서 · 신뢰 · 판)입니다 — 연결마다 세션을 만듭니다. `createSession` 은 아무 스레드에서나.
     */
    class SW_API ITLSContext
    {
    public:
        ITLSContext()          = default;
        virtual ~ITLSContext() = default;

        ITLSContext( const ITLSContext& )            = delete;
        ITLSContext& operator=( const ITLSContext& ) = delete;

        virtual unique_ptr<ITLSSession> createSession() = 0;
        virtual TLSRole                 getRole() const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetSecurityProvider
     * @brief 암호 원시 연산의 API 입니다. 모든 함수는 아무 스레드에서나 부른다(구현이 스레드 안전).
     */
    class SW_API INetSecurityProvider
    {
    public:
        INetSecurityProvider()          = default;
        virtual ~INetSecurityProvider() = default;

        INetSecurityProvider( const INetSecurityProvider& )            = delete;
        INetSecurityProvider& operator=( const INetSecurityProvider& ) = delete;

        virtual const utf8* getName() const = 0;

        /** @brief 암호용 난수(CSPRNG)로 채웁니다. */
        [[nodiscard]] virtual bool fillRandomBytes( uint8* pOut, int32 size )        = 0;
        [[nodiscard]] virtual bool makeX25519KeyPair( NetX25519KeyPair& outKeyPair ) = 0;
        /** @brief X25519 공유 비밀 32 바이트입니다. 작은 차수 점(공유 비밀이 0)이면 false. */
        [[nodiscard]] virtual bool computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret ) = 0;
        /** @brief HKDF-SHA256(RFC 5869) — 소금 · 정보는 비워도 된다(크기 0). */
        [[nodiscard]] virtual bool computeHkdfSHA256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize,
                                                      uint8* pOut, int32 outSize ) = 0;
        /**
         * @brief 느린 비밀번호 해시 Argon2id(RFC 9106) → @p outSize 바이트입니다.
         * @details 느리다(기본 매개변수로 수십 ms · 19 MiB) — 게임 스레드에서 부르지 않는다(계정 서버는 저장소 워커에서). 비밀번호 · 소금 외의 입력(비밀 · 연관 자료)은 쓰지 않는다.
         */
        [[nodiscard]] virtual bool computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params,
                                                        uint8* pOut, int32 outSize ) = 0;
        /** @brief 키를 박은 AEAD 를 만듭니다(@p pKey 는 kAeadKeySize 바이트). 실패하면 nullptr. */
        virtual unique_ptr<INetAead> createAead( NetAeadAlgorithm algorithm, const uint8* pKey ) = 0;

        /** @brief TLS 컨텍스트를 만듭니다. 실패하면 nullptr 이고 @p outError 에 까닭. */
        virtual unique_ptr<ITLSContext> createTLSContext( const TLSContextConfig& settings, string& outError ) = 0;
        /** @brief 개발용 자체 서명 인증서(EC P-256, SAN = @p commonName · localhost · 127.0.0.1)와 키를 PEM 으로 만듭니다. */
        [[nodiscard]] virtual bool createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem ) = 0;
        /** @brief PEM 인증서(첫 장)의 DER SHA-256 을 소문자 16 진 64 자로 줍니다. */
        [[nodiscard]] virtual bool computeCertificateSHA256( const string& certificatePem, string& outHex ) = 0;
        /** @brief SHA-256 다이제스트(32 B)입니다 — PKCE 코드 확인 · 내용 해시. */
        [[nodiscard]] virtual bool computeSHA256( const uint8* pData, int32 dataSize, uint8* pOutDigest ) = 0;
        /**
         * @brief 서명을 확인합니다(외부 로그인 ID 토큰). 키 구성 요소가 틀렸거나 서명이 맞지 않으면 false 입니다.
         * @details ES256 서명은 JWS 형식(r ‖ s, 64 B)이다. RSA 는 2048 비트 미만 키를 거절한다.
         */
        [[nodiscard]] virtual bool verifySignature( const NetPublicKey& publicKey, const uint8* pData, int32 dataSize, const uint8* pSignature, int32 signatureSize ) = 0;
        /** @brief 서명 키 쌍을 만듭니다 — 비밀 키는 PEM(PKCS#8), 공개 키는 구성 요소. 테스트의 가짜 발급자 · 개발 서버가 쓴다. */
        [[nodiscard]] virtual bool createSigningKeyPair( NetSignatureAlgorithm algorithm, string& outPrivateKeyPem, NetPublicKey& outPublicKey ) = 0;
        /** @brief PEM 비밀 키로 서명합니다(ES256 은 r ‖ s 64 B) — 클라이언트 비밀 JWT(애플) · 테스트 발급자. */
        [[nodiscard]] virtual bool signData( NetSignatureAlgorithm algorithm, const string& privateKeyPem, const uint8* pData, int32 dataSize, vector<uint8>& outSignatureBytes ) = 0;
        /**
         * @brief 인증서 사슬(DER, 리프가 먼저)이 신뢰 루트(@p trustedRootsPem — PEM 여러 장)까지 이어지고 시각 @p unixSeconds 에 유효한지 확인해 리프의 공개 키를 꺼냅니다(스토어가 JWS 의 `x5c` 로 주는 사슬).
         * @param leafRequiredOid 비어 있지 않으면 리프가 이 OID 의 확장을 가져야 합니다. @param issuerRequiredOid 비어 있지 않으면 리프를 발급한 인증서가 가져야 합니다(애플의 사슬 표지).
         * @details 구현하지 않는 제공자는 false 입니다(지원하지 않음 — 확인한 것처럼 true 를 주지 않는다). 사슬 길이는 8 장까지.
         */
        [[nodiscard]] virtual bool verifyCertificateChain( const vector<vector<uint8>>& listChainDer, const string& trustedRootsPem, int64 unixSeconds, string_view leafRequiredOid,
                                                           string_view issuerRequiredOid, NetPublicKey& outLeafPublicKey )
        {
            (void)listChainDer;
            (void)trustedRootsPem;
            (void)unixSeconds;
            (void)leafRequiredOid;
            (void)issuerRequiredOid;
            outLeafPublicKey = NetPublicKey{};
            return false;
        }
        /**
         * @brief 루트 → 중간 → 리프(EC P-256) 인증서 사슬과 리프의 서명 키를 만듭니다 — 사슬을 확인하는 코드의 테스트가 가짜 스토어의 서명자로 씁니다.
         * @param outListChainPem 리프, 중간, 루트 순(PEM). @param leafOid · @param issuerOid 는 비어 있지 않으면 리프 · 중간 인증서에 그 OID 의 확장을 넣는다.
         */
        [[nodiscard]] virtual bool createTestCertificateChain( int32 validDays, string_view leafOid, string_view issuerOid, vector<string>& outListChainPem, string& outLeafPrivateKeyPem )
        {
            (void)validDays;
            (void)leafOid;
            (void)issuerOid;
            outListChainPem.clear();
            outLeafPrivateKeyPem.clear();
            return false;
        }
    };
} // namespace sw
