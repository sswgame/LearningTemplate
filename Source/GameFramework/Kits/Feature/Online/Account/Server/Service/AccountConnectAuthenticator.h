/**
 * @file AccountConnectAuthenticator.h
 * @brief 게임 서버(UDP)가 클라이언트가 내민 접속 표를 확인합니다 — Core `INetConnectAuthenticator`. 저장소를 읽지 않고 표 서명 · 서버 · 시한만 본다.
 * @details - 네트워크 스레드에서 `NetHost` 잠금을 쥔 채 불린다 — 표 확인(HKDF 둘)만 하고 빠르다. 시각은 게임 스레드가 틱마다 `setNowMs` 로 넣는다(원자 변수).
 *          - 게임 서버(로그인 서버와 다른 프로세스)는 같은 주 키로 `LoginTicketAuthority` 만 세운다(저장소 없음). 주 키는 서버 설정의 비밀(환경 변수)에서 —
 *            키 배포 · 교체 절차는 백로그(사용자 결정: 로그인 · 게임 서버가 나눠 가짐, 시한 60 초).
 *          - 표 비밀이 UDP 키 유도의 소금이 되어 가로챈 표만으로는 접속하지 못한다. netcode.io 의 connect token 과 같은 모양.
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
     * @brief 접속 표 확인기입니다.
     */
    class SW_GF_API AccountConnectAuthenticator final : public INetConnectAuthenticator
    {
    public:
        /** @brief @p pAuthority 는 빌려 쓴다(초기화된 것). @p serverID 는 표가 가리켜야 하는 이 게임 서버의 id. */
        AccountConnectAuthenticator( const LoginTicketAuthority* pAuthority, const hashed_string& serverID );

        void setNowMs( int64 nowMs ) { _nowMs.store( nowMs, std::memory_order_relaxed ); }

        [[nodiscard]] bool findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalID ) override;

    private:
        hashed_string               _serverID;
        const LoginTicketAuthority* _pAuthority;
        atomic<int64>               _nowMs;
    };
} // namespace sw
