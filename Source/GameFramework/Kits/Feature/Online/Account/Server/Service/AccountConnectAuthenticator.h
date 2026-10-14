/**
 * @file AccountConnectAuthenticator.h
 * @brief 게임 서버(UDP)가 클라이언트가 제시한 접속 티켓을 확인합니다. Core 의 `INetConnectAuthenticator` 구현입니다. 저장소는 읽지 않고 티켓의 서명, 서버, 시한만 확인합니다.
 * @details - 네트워크 스레드에서 `NetHost` 락을 잡은 채 호출된다. 티켓 확인(HKDF 두 번)만 하므로 빠르다. 시각은 게임 스레드가 틱마다 `setNowMs` 로 넣으며 원자 변수이다.
 *          - 게임 서버가 로그인 서버와 다른 프로세스이면 같은 마스터 키로 `LoginTicketAuthority` 만 만든다. 저장소는 없다. 마스터 키는 서버 설정의 비밀(환경 변수)에서 읽는다.
 *            키 배포와 교체 절차는 아직 정하지 않았다. 로그인 서버와 게임 서버가 키를 나눠 갖고 시한은 60초로 하는 것까지 사용자가 정했다.
 *          - 티켓 비밀이 UDP 키 유도의 솔트가 되므로 가로챈 티켓만으로는 접속할 수 없다. netcode.io 의 connect token 과 같은 구조이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Network/Connection/NetHostSecurity.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class LoginTicketAuthority;

    /**
     * @class AccountConnectAuthenticator
     * @brief 접속 티켓 확인기입니다.
     */
    class SW_KIT_API AccountConnectAuthenticator final : public INetConnectAuthenticator
    {
    public:
        /** @brief @p pAuthority 는 초기화를 마친 것을 빌려 씁니다. @p serverID 는 티켓이 가리켜야 하는 이 게임 서버의 id 입니다. */
        AccountConnectAuthenticator( const LoginTicketAuthority* pAuthority, const hashed_string& serverID );

        void setNowMs( int64 nowMs ) { _nowMs.store( nowMs, std::memory_order_relaxed ); }

        [[nodiscard]] bool findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalID ) override;

    private:
        hashed_string               _serverID;
        const LoginTicketAuthority* _pAuthority;
        atomic<int64>               _nowMs;
    };
} // namespace sw
