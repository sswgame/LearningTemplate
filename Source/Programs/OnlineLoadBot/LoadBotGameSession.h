/**
 * @file LoadBotGameSession.h
 * @brief 봇의 게임 UDP 세션 하나 — 전용 서버의 게임 포트(UDP)에 붙는 클라이언트 `NetHost` 입니다(서버 디버깅 S5: Profiler 탭이 보는 `net.stats` 는 이 세션의 것이다).
 * @details 서비스 연결(TCP — 로그인 · 채팅 …)과 따로 산다. 봇 수천이 한 스레드에서 `update` 로 펌프하므로 네트워크 스레드를 두지 않는다(`NetHostThread` 는 봇마다 스레드).
 *          자격 없이 붙는 길이라 티켓을 요구하는 서버(`_bGameConnectTicket`)는 `SecurityMismatch` 로 거절한다 — 그 서버는 시나리오 `net-pair` 가 본다. 게임 안 메시지는 보내지 않는다
 *          (연결 유지 패킷과 RTT 가 서버 쪽 연결 통계를 채운다 — 서버의 연결 수 · 틱 · 드레인을 재는 것이 목적이다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/UDPNetTransport.h"

namespace sw
{
    /** @brief 게임 UDP 세션 하나입니다. 실행기 스레드 하나에서만 부릅니다. */
    class LoadBotGameSession
    {
    public:
        enum class State : uint8
        {
            Idle = 0,
            Connecting,
            Connected,
            Failed ///< 거절 · 시간 초과 · 끊김 — `getFailReason` 이 까닭
        };

        LoadBotGameSession() = default;
        ~LoadBotGameSession() { stop(); }

        LoadBotGameSession( const LoadBotGameSession& )            = delete;
        LoadBotGameSession& operator=( const LoadBotGameSession& ) = delete;

        /** @brief 소켓을 열고 연결을 시작합니다. 이미 있으면 먼저 닫습니다. 소켓을 열 수 없으면 false 입니다. */
        [[nodiscard]] bool start( const NetAddress& serverAddress );
        /** @brief 연결을 끊고 소켓을 닫습니다. 두 번 불러도 됩니다. */
        void stop();
        /** @brief 호스트를 돌리고 사건으로 상태를 갱신합니다. */
        void update( float64 nowSeconds );

        State               getState() const { return _state; }
        NetDisconnectReason getFailReason() const { return _failReason; }

    private:
        UDPNetTransport      _transport{};
        NetHost              _host{};
        vector<NetHostEvent> _listEvent{};
        NetDisconnectReason  _failReason{ NetDisconnectReason::None };
        State                _state{ State::Idle };
        bool                 _bOpen{ false };
    };
} // namespace sw
