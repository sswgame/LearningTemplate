/**
 * @file NetSecurityTypes.h
 * @brief 네트워크 암호 창구가 주고받는 값 — 크기 상수, AEAD 종류, X25519 키 쌍, 세션 비밀, 비밀번호 해시 매개변수, TLS 설정입니다.
 * @details 구현(OpenSSL)을 모르는 Core 타입만 둡니다. 비밀 키 · 세션 키는 다 쓰면 `wipe` 로 지운다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @brief 암호 원시 연산의 바이트 크기입니다. */
    struct NetSecurityConstant
    {
        static constexpr int32 kX25519KeySize = 32;
        static constexpr int32 kAeadKeySize   = 32; ///< AES-256-GCM · ChaCha20-Poly1305 모두 32
        static constexpr int32 kAeadNonceSize = 12;
        static constexpr int32 kAeadTagSize   = 16;
        static constexpr int32 kSha256Size    = 32;
        static constexpr int32 kMaxTokenSize  = 64; ///< UDP 연결 때 내미는 세션 토큰 상한
    };
} // namespace sw

namespace sw
{
    /** @brief AEAD 알고리즘입니다. 양쪽이 같아야 한다(협상하지 않는다). */
    enum class NetAeadAlgorithm : uint8
    {
        Aes256Gcm = 0,   ///< AES-NI 가 있는 x64 기본
        ChaCha20Poly1305 ///< AES 가속 없는 장치
    };

    /** @brief TLS 쪽입니다 — 서버는 인증서 · 키, 클라이언트는 신뢰 목록을 듭니다. */
    enum class TlsRole : uint8
    {
        Server = 0,
        Client
    };

    /** @brief TLS 판입니다. 서비스 스트림은 1.3 만 — 1.2 는 다운그레이드 거절 시험에만 쓴다. */
    enum class TlsVersion : uint8
    {
        Tls12 = 0,
        Tls13
    };

    /** @brief TLS 세션의 상태입니다. */
    enum class TlsSessionState : uint8
    {
        Handshaking = 0, ///< 핸드셰이크 중 — 쓴 평문은 모아 둔다
        Established,     ///< 평문을 주고받는다
        Failed,          ///< 핸드셰이크 · 레코드 검증 실패(변조 · 신뢰 실패 · 판 불일치) — 끊는다
        Closed           ///< close_notify 를 보냈거나 받았다
    };
} // namespace sw

namespace sw
{
    /** @brief X25519 키 쌍입니다. 비밀 키는 다 쓰면 `wipe`. */
    struct NetX25519KeyPair
    {
        uint8 _arrPublicKey[NetSecurityConstant::kX25519KeySize]{};
        uint8 _arrPrivateKey[NetSecurityConstant::kX25519KeySize]{};

        /** @brief 비밀 키를 0 으로 지웁니다(컴파일러가 지우지 못하게 volatile 쓰기). */
        SW_API void wipe();
    };
} // namespace sw

namespace sw
{
    /** @brief 로그인 키트가 발급한 세션 비밀 — UDP 키 유도의 소금이 되어 세션 키를 토큰에 묶습니다. */
    struct NetSessionSecret
    {
        uint8 _arrByte[NetSecurityConstant::kSha256Size]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 느린 비밀번호 해시(Argon2id) 매개변수입니다. 계정 레코드에 같이 적혀, 바꾸면 다음 로그인에서 새 값으로 다시 해시한다.
     * @details 기본값은 OWASP 첫 권고(m = 19 MiB · t = 2 · p = 1) — 로그인 한 번에 수십 ms · 19 MiB.
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
    /** @brief TLS 컨텍스트 설정입니다. 인증서 · 키 · 신뢰는 PEM 문자열(파일 경로는 `EngineNetSecurity` 가 읽어 채운다). */
    struct TlsContextSettings
    {
        string     _certificatePem{};             ///< 서버 — 끝 인증서 + 중간 인증서(체인)
        string     _privateKeyPem{};              ///< 서버
        string     _privateKeyPassphrase{};       ///< 서버 — 키가 암호로 잠겼을 때만(비우면 암호 없는 키). 다 쓰면 부르는 쪽이 비운다
        string     _trustPem{};                   ///< 클라이언트 — 이 인증서(들)로 서버 체인을 검증한다(CA 묶음 또는 서버 인증서 그 자체)
        string     _serverName{};                 ///< 클라이언트 — SNI + 이름 검사(비우면 이름 검사 없이 체인만)
        string     _pinnedCertificateSha256Hex{}; ///< 클라이언트 — 있으면 서버 끝 인증서의 SHA-256(소문자 16 진 64 자)이 같아야 한다
        TlsRole    _role{ TlsRole::Server };
        TlsVersion _minVersion{ TlsVersion::Tls13 }; ///< 1.3 만 — 낮추는 것은 다운그레이드 시험뿐
        TlsVersion _maxVersion{ TlsVersion::Tls13 };
    };
} // namespace sw
