/**
 * @file LoginTicketAuthority.h
 * @brief 게임(UDP) 접속 표 — 로그인 서버가 서명해 주고, 게임 서버가 저장소 없이 확인합니다. 표 비밀은 UDP 세션 키 유도의 소금이 됩니다.
 * @details 표(64 B) = 본문 48 B(계정 id · 세션 id · 시한 ms · 서버 id 해시 — 모두 LE 8 B — · 난수 16 B) ‖ 태그 16 B.
 *          태그 = HKDF(주 키, 정보 = "sw-ticket-tag-v1" ‖ 본문), 표 비밀 = HKDF(주 키, 정보 = "sw-ticket-secret-v1" ‖ 본문).
 *          주 키(32 B)는 로그인 서버와 게임 서버가 나눠 가진다(서버 설정 — 같은 프로세스면 시작 때 난수). 표는 시한 안에서 여러 번 쓸 수 있다(UDP 재접속) —
 *          가로챈 표만으로는 접속하지 못한다(핸드셰이크가 표 비밀을 아는지 증명하게 한다).
 *          세션이 밀려나도 이미 낸 표는 시한까지 산다 — 시한을 짧게(기본 60 초) 두고, 이미 붙은 UDP 연결은 게임이 `LoginEvent::Revoked` 로 끊는다.
 *          netcode.io 의 connect token 과 같은 모양이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ILoginCrypto;

    /** @brief 클라이언트가 받는 표입니다(TLS 로) — 표는 UDP 연결 때 내밀고, 비밀은 내밀지 않고 키 유도에만 쓴다. */
    struct NetGameTicket
    {
        static constexpr int32 kBodySize   = 48;
        static constexpr int32 kTagSize    = 16;
        static constexpr int32 kTokenSize  = kBodySize + kTagSize; ///< 64
        static constexpr int32 kSecretSize = 32;

        uint8 _arrToken[kTokenSize]{};
        uint8 _arrSecret[kSecretSize]{};
        int64 _expiresAtMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 서버가 표에서 읽은 것입니다. */
    struct NetGameTicketClaim
    {
        uint64 _accountId{ 0 };
        uint64 _sessionId{ 0 };
        int64  _expiresAtMs{ 0 };
        uint8  _arrSecret[NetGameTicket::kSecretSize]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoginTicketAuthority
     * @brief 표를 내고 확인합니다. `initialize` 뒤로는 읽기만 하므로 여러 스레드가 동시에 `verifyTicket` 해도 된다(암호 창구가 스레드 안전이면).
     */
    class SW_GF_API LoginTicketAuthority
    {
    public:
        static constexpr int32 kMasterKeySize = 32;

        LoginTicketAuthority();

        void initialize( ILoginCrypto* pCrypto, const uint8 ( &arrMasterKey )[kMasterKeySize] );
        void shutdown();

        [[nodiscard]] bool issueTicket( uint64 accountId, uint64 sessionId, const hashed_string& serverId, int64 expiresAtMs, NetGameTicket& outTicket ) const;
        /** @brief 태그 · 서버 · 시한을 봅니다. 틀리면 false 이고 @p outClaim 은 그대로입니다. */
        [[nodiscard]] bool verifyTicket( const uint8* pToken, int32 tokenSize, const hashed_string& serverId, int64 nowMs, NetGameTicketClaim& outClaim ) const;

        bool isInitialized() const { return _pCrypto != nullptr; }

    private:
        [[nodiscard]] bool computeTag( const uint8* pBody, uint8 ( &outTag )[NetGameTicket::kTagSize] ) const;
        [[nodiscard]] bool computeSecret( const uint8* pBody, uint8 ( &outSecret )[NetGameTicket::kSecretSize] ) const;

        ILoginCrypto* _pCrypto;
        uint8         _arrMasterKey[kMasterKeySize];
    };
} // namespace sw
