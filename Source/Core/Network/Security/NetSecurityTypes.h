/**
 * @file NetSecurityTypes.h
 * @brief 네트워크 암호 함수가 주고받는 값을 정의합니다. 크기 상수, AEAD 종류, X25519 키 쌍, 세션 비밀, 비밀번호 해시 매개변수, TLS 설정이 있습니다.
 * @details 구현(OpenSSL)을 모르는 Core 타입만 둔다. 비밀 키와 세션 키는 다 쓰면 `wipe` 로 지운다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 암호 원시 연산의 바이트 크기입니다. */
    struct NetSecurityConstant
    {
        static constexpr int32 kX25519KeySize = 32;
        static constexpr int32 kAeadKeySize   = 32; ///< AES-256-GCM과 ChaCha20-Poly1305 모두 32.
        static constexpr int32 kAeadNonceSize = 12;
        static constexpr int32 kAeadTagSize   = 16;
        static constexpr int32 kSha256Size    = 32;
        static constexpr int32 kMaxTokenSize  = 64; ///< UDP 연결 때 제시하는 세션 토큰의 상한.
    };
} // namespace sw

namespace sw
{
    /** @brief AEAD 알고리즘입니다. 양쪽이 같아야 하며 협상하지 않습니다. */
    enum class NetAeadAlgorithm : uint8
    {
        Aes256Gcm = 0,   ///< AES-NI 가 있는 x64 의 기본값.
        ChaCha20Poly1305 ///< AES 가속이 없는 장치.
    };

    /** @brief TLS 설정의 역할입니다. 서버는 인증서와 키를, 클라이언트는 신뢰 목록을 가집니다. */
    enum class TLSRole : uint8
    {
        Server = 0,
        Client
    };

    /** @brief TLS 버전입니다. 서비스 스트림은 1.3 만 쓰며, 1.2 는 다운그레이드 거절 테스트에만 씁니다. */
    enum class TLSVersion : uint8
    {
        TLS12 = 0,
        TLS13
    };

    /** @brief TLS 세션의 상태입니다. */
    enum class TLSSessionState : uint8
    {
        Handshaking = 0, ///< 핸드셰이크 중이다. 쓴 평문은 모아 둔다.
        Established,     ///< 평문을 주고받는다.
        Failed,          ///< 핸드셰이크나 레코드 검증에 실패했다(변조, 신뢰 실패, 버전 불일치). 연결을 끊는다.
        Closed           ///< close_notify 를 보냈거나 받았다.
    };
} // namespace sw

namespace sw
{
    /** @brief X25519 키 쌍입니다. 비밀 키는 다 쓰면 `wipe` 로 지웁니다. */
    struct NetX25519KeyPair
    {
        uint8 _arrPublicKey[NetSecurityConstant::kX25519KeySize]{};
        uint8 _arrPrivateKey[NetSecurityConstant::kX25519KeySize]{};

        /** @brief 비밀 키를 0으로 지웁니다. 컴파일러가 쓰기를 없애지 못하게 volatile 로 씁니다. */
        SW_API void wipe();
    };
} // namespace sw

namespace sw
{
    /** @brief 서명 알고리즘입니다(외부 로그인 ID 토큰, 클라이언트 비밀 JWT). JWS 이름은 RS256과 ES256입니다. */
    enum class NetSignatureAlgorithm : uint8
    {
        RsaPkcs1Sha256 = 0, ///< RS256. RSASSA-PKCS1-v1_5와 SHA-256.
        EcdsaP256Sha256     ///< ES256. P-256과 SHA-256이며, 서명은 r과 s를 이은 값이다(각 32바이트, JWS 형식이고 DER 이 아니다).
    };

    /** @brief 공개 키의 구성 요소입니다(JWK 의 n, e 또는 x, y). 모두 빅 엔디언 바이트입니다. */
    struct NetPublicKey
    {
        vector<uint8>         _modulus{};  ///< RSA n
        vector<uint8>         _exponent{}; ///< RSA e
        vector<uint8>         _x{};        ///< EC P-256 x(32바이트).
        vector<uint8>         _y{};        ///< EC P-256 y(32바이트).
        NetSignatureAlgorithm _algorithm{ NetSignatureAlgorithm::RsaPkcs1Sha256 };
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 키트가 발급한 세션 비밀입니다. UDP 키 유도의 솔트가 되어 세션 키를 토큰에 묶습니다. */
    struct NetSessionSecret
    {
        uint8 _arrByte[NetSecurityConstant::kSha256Size]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 느린 비밀번호 해시(Argon2id)의 매개변수입니다. 계정 레코드에 함께 기록되며, 값을 바꾸면 다음 로그인에서 새 값으로 다시 해시한다.
     * @details 기본값은 OWASP 의 첫 번째 권고(m = 19MiB, t = 2, p = 1)이며, 로그인 한 번에 수십 ms 와 19MiB 가 든다.
     */
    struct NetPasswordHashParams
    {
        uint32 _memoryKiB{ 19456 };
        uint32 _iterationCount{ 2 };
        uint32 _parallelism{ 1 };

        bool operator==( const NetPasswordHashParams& other ) const
        {
            return _memoryKiB == other._memoryKiB && _iterationCount == other._iterationCount && _parallelism == other._parallelism;
        }
        bool operator!=( const NetPasswordHashParams& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief TLS 컨텍스트 설정입니다. 인증서, 키, 신뢰 목록은 PEM 문자열이며, 파일 경로는 `NetSecurity` 가 읽어서 채웁니다. */
    struct TLSContextConfig
    {
        string     _certificatePem{};             ///< 서버 전용. 끝 인증서와 중간 인증서(체인).
        string     _privateKeyPem{};              ///< 서버 전용.
        string     _privateKeyPassphrase{};       ///< 서버 전용. 키가 암호로 잠겼을 때만 쓴다(비우면 암호 없는 키). 다 쓰면 부르는 쪽이 비운다.
        string     _trustPem{};                   ///< 클라이언트 전용. 이 인증서로 서버 체인을 검증한다(CA 번들 또는 서버 인증서 자체).
        string     _serverName{};                 ///< 클라이언트 전용. SNI 와 이름 검사에 쓴다(비우면 이름 검사 없이 체인만 검증한다).
        string     _pinnedCertificateSha256Hex{}; ///< 클라이언트 전용. 있으면 서버 끝 인증서의 SHA-256(소문자 16진 64자)이 같아야 한다.
        TLSRole    _role{ TLSRole::Server };
        TLSVersion _minVersion{ TLSVersion::TLS13 }; ///< 1.3 만 쓴다. 낮추는 것은 다운그레이드 테스트 용도뿐이다.
        TLSVersion _maxVersion{ TLSVersion::TLS13 };
    };
} // namespace sw
