/**
 * @file LoginTicketAuthority.h
 * @brief 게임(UDP) 접속 티켓입니다. 로그인 서버가 서명해 발급하고, 게임 서버가 저장소 없이 확인합니다. 티켓 비밀은 UDP 세션 키를 유도할 때 솔트가 됩니다.
 * @details 티켓(64 B)은 본문 48 B 뒤에 태그 16 B 를 붙인 것이다. 본문은 계정 id, 세션 id, 시한(ms), 서버 id 해시를 각각 리틀 엔디언 8 B 로 담고 난수 16 B 를 더한 것이다.
 *          태그는 HKDF(마스터 키, 정보 = "sw-ticket-tag-v1" ‖ 본문)이고, 티켓 비밀은 HKDF(마스터 키, 정보 = "sw-ticket-secret-v1" ‖ 본문)이다.
 *          마스터 키(32 B)는 로그인 서버와 게임 서버가 나눠 가진다. 서버 설정으로 정하며, 같은 프로세스이면 시작할 때 난수로 만든다.
 *          티켓은 시한 안에서 여러 번 쓸 수 있다(UDP 재접속). 가로챈 티켓만으로는 접속할 수 없다. 핸드셰이크에서 티켓 비밀을 아는지 증명해야 하기 때문이다.
 *          세션이 밀려나도 이미 발급한 티켓은 시한까지 유효하다. 그래서 시한을 짧게(기본 60초) 두고, 이미 접속한 UDP 연결은 게임이 `LoginEvent::Revoked` 로 끊는다.
 *          netcode.io 의 connect token 과 같은 구조이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountTypes.h"

namespace sw
{
    class ILoginCrypto;

    /** @brief 게임 서버가 표에서 읽은 것입니다. */
    struct NetGameTicketClaim
    {
        uint64 _accountID{ 0 };
        uint64 _sessionID{ 0 };
        int64  _expiresAtMs{ 0 };
        uint8  _arrSecret[NetGameTicket::kSecretSize]{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class LoginTicketAuthority
     * @brief 티켓을 발급하고 확인합니다. `initialize` 뒤에는 읽기만 하므로 암호 인터페이스가 스레드 안전이면 여러 스레드가 동시에 `verifyTicket` 을 불러도 됩니다.
     */
    class SW_GF_API LoginTicketAuthority
    {
    public:
        static constexpr int32 kMasterKeySize = 32;

        LoginTicketAuthority();

        void initialize( ILoginCrypto* pCrypto, const uint8 ( &arrMasterKey )[kMasterKeySize] );
        void shutdown();

        [[nodiscard]] bool issueTicket( uint64 accountID, uint64 sessionID, const hashed_string& serverID, int64 expiresAtMs, NetGameTicket& outTicket ) const;
        /** @brief 태그, 서버, 시한을 확인합니다. 틀리면 false 를 반환하고 @p outClaim 은 그대로 둡니다. */
        [[nodiscard]] bool verifyTicket( const uint8* pToken, int32 tokenSize, const hashed_string& serverID, int64 nowMs, NetGameTicketClaim& outClaim ) const;

        bool isInitialized() const { return _pCrypto != nullptr; }

    private:
        [[nodiscard]] bool computeTag( const uint8* pBody, uint8 ( &outTag )[NetGameTicket::kTagSize] ) const;
        [[nodiscard]] bool computeSecret( const uint8* pBody, uint8 ( &outSecret )[NetGameTicket::kSecretSize] ) const;

        ILoginCrypto* _pCrypto;
        uint8         _arrMasterKey[kMasterKeySize];
    };
} // namespace sw
