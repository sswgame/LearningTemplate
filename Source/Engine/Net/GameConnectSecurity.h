/**
 * @file GameConnectSecurity.h
 * @brief 전용 서버 게임 포트(UDP)의 접속 보안 자리입니다 — 암호 제공자와 접속 인증기(접속 티켓 확인)를 서버 조립이 끼우는 곳입니다(OnlineKits F5).
 * @details 게임 포트는 게임 인스턴스가 설 때 열린다(`GameNetSession::openFromLaunch`). 그런데 티켓을 확인하는 인증기(계정 키트의 `AccountConnectAuthenticator`)는
 *          그 뒤에 서는 서버 서비스(온라인 키트)가 만든다. 그래서 세션은 이 객체를 인증기로 끼워 두고, 키트가 설 때 진짜 인증기를 묶는다(`bindAuthenticator`).
 *          - 묶이기 전에 온 접속은 거절한다(모르는 토큰과 같다 — 열린 채로 받지 않는다). `ServerApp` 은 서비스 시작 뒤 묶이지 않았으면 기동을 멈춘다.
 *          - 암호 제공자(OpenSSL)는 GameFramework 에 있어 Engine 이 모른다 — GameFramework 의 게임 인스턴스가 설 때 `setProvider` 로 넣는다.
 *          - 서버 설정 `ServerConfig::_bGameConnectTicket` 이 켜졌을 때만 세션이 이 자리를 쓴다(그때 게임 포트는 암호화 · 토큰 결속이다).
 * @warning `findSessionSecret` 은 네트워크 스레드에서 `NetEndpoint` 잠금을 쥔 채 불린다. 묶기 · 풀기와는 이 객체의 잠금 하나로 맞물린다
 *          (푼 뒤에는 옛 인증기를 부르지 않으므로, 푼 쪽이 바로 인증기를 내려도 된다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Network/Connection/NetEndpointSecurity.h"

namespace sw
{
    class INetSecurityProvider;

    /**
     * @class GameConnectSecurity
     * @brief 세션마다 하나입니다(`GameNetSession::getConnectSecurity`). 세션의 인증기 자리에 끼워지고 묶인 인증기에 넘깁니다.
     */
    class SW_API GameConnectSecurity final : public INetConnectAuthenticator
    {
    public:
        GameConnectSecurity();

        /** @brief 암호 제공자를 넣습니다(GameFramework 의 게임 인스턴스가 서며 넣고, 내려갈 때 nullptr 로 뺍니다). 소유하지 않으며 사용하는 동안 살아 있어야 합니다. */
        void setProvider( INetSecurityProvider* pProvider );
        /** @brief 암호 제공자입니다. 넣지 않았으면 nullptr 입니다. */
        INetSecurityProvider* findProvider() const;

        /** @brief 접속 인증기를 묶습니다. 이미 다른 것이 묶였으면 오류를 남기고 false 입니다(조립이 둘이면 어느 쪽 티켓인지 모른다). */
        [[nodiscard]] bool bindAuthenticator( INetConnectAuthenticator* pAuthenticator );
        /** @brief @p pAuthenticator 가 묶여 있으면 풉니다. 돌아온 뒤에는 그 인증기를 부르지 않습니다. */
        void unbindAuthenticator( INetConnectAuthenticator* pAuthenticator );
        bool isAuthenticatorBound() const;
        /** @brief 인증기가 묶이기 전에 와서 거절한 접속 수입니다(로그 · 지표). */
        uint64 getRefusedWhileUnboundCount() const { return _refusedWhileUnbound.load( std::memory_order_relaxed ); }

        [[nodiscard]] bool findSessionSecret( const uint8* pToken, int32 tokenSize, NetSessionSecret& outSecret, uint64& outPrincipalID ) override;

    private:
        mutable mutex                             _mutex;
        INetSecurityProvider* _pProvider          SW_GUARDED_BY( _mutex );
        INetConnectAuthenticator* _pAuthenticator SW_GUARDED_BY( _mutex );
        atomic<uint64>                            _refusedWhileUnbound;
    };
} // namespace sw
