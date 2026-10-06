/**
 * @file NetHostSecurity.h
 * @brief UDP 연결의 보안 설정 — 방식(평문 · 암호화), 서버의 세션 토큰 인증기, 클라이언트가 연결 때 내미는 자격입니다.
 * @details 암호화(`NetSecurityMode::Encrypted`)는 연결 수립에 일회 X25519 키 교환, 그 뒤 데이터 · 끊기 패킷마다 AEAD(nonce = 방향 IV XOR 64 비트 패킷 번호,
 *          1024 재전송 방지 창)다 — Valve GameNetworkingSockets · 언리얼 AESGCMHandlerComponent 와 같은 모양. 방식 · 토큰 결속 여부는 프로토콜 id 에 섞여
 *          협상하지 않는다(`NetProtocolFeature` — 한쪽만이면 SecurityMismatch).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Network/Security/NetSecurityTypes.h"

namespace sw
{
    class INetSecurityProvider;
} // namespace sw

namespace sw
{
    /** @brief UDP 보안 방식입니다 — 양쪽이 같아야 한다(다르면 `SecurityMismatch` 로 거절, 협상 없음 = 다운그레이드 없음). */
    enum class NetSecurityMode : uint8
    {
        Off = 0,  ///< 평문(시험 · LAN)
        Encrypted ///< X25519 + AEAD. 인증기가 있으면 세션 비밀에 묶이고(중간자 불가), 없으면 일회 키만(도청만 막는다 — 개발 빌드 전용, Shipping 서버는 listen 실패)
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetConnectAuthenticator
     * @brief 서버 — 클라이언트가 내민 세션 토큰으로 세션 비밀과 주체(계정)를 찾습니다(로그인 키트가 구현).
     * @warning 네트워크 스레드(`NetHost::update` 를 도는 스레드)에서 **`NetHost` 잠금을 쥔 채** 불린다 — `NetHost` 를 부르지 말고 빨라야 하며 스레드 안전해야 한다.
     */
    class SW_API INetConnectAuthenticator
    {
    public:
        INetConnectAuthenticator()          = default;
        virtual ~INetConnectAuthenticator() = default;

        INetConnectAuthenticator( const INetConnectAuthenticator& )            = delete;
        INetConnectAuthenticator& operator=( const INetConnectAuthenticator& ) = delete;

        /** @brief 모르는 토큰이면 false 입니다. */
        [[nodiscard]] virtual bool findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalId ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief `NetHostSettings::_security` 입니다. */
    struct NetSecuritySettings
    {
        INetSecurityProvider*     _pProvider{ nullptr };      ///< Encrypted 면 필수(`EngineNetSecurity::getProvider()`)
        INetConnectAuthenticator* _pAuthenticator{ nullptr }; ///< 서버 — 있으면 토큰 없는 · 모르는 토큰 · 증명이 틀린 연결을 거절한다
        NetSecurityMode           _mode{ NetSecurityMode::Off };
        NetAeadAlgorithm          _algorithm{ NetAeadAlgorithm::Aes256Gcm };
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트가 연결 때 내미는 것 — 로그인 키트가 TLS 로 받은 세션 토큰과 세션 비밀입니다. */
    struct NetConnectCredentials
    {
        uint8            _arrToken[NetSecurityConstant::kMaxTokenSize]{};
        int32            _tokenSize{ 0 };
        NetSessionSecret _secret{};
    };
} // namespace sw
