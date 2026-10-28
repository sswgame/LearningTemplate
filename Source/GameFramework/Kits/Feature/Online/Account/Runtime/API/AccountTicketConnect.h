/**
 * @file AccountTicketConnect.h
 * @brief 로그인 서버에 계정으로 로그인해 게임 접속 티켓을 받아 오는 막는 도우미입니다(OnlineKits F5 의 클라이언트 쪽 구동기).
 * @details 로그인 서비스 포트에 연결해 Hello → 로그인 → 티켓 요청까지 시한 안에 진행하고 `NetConnectCredentials` 를 돌려준다. 그 뒤 부르는 쪽이
 *          `GameNetSession::makeTicketClientParams( 게임 주소, 자격 )` 로 세션 인자를 만들어 `initialize` 한다(`net.ticketconnect` 개발 명령이 그 조립이다).
 *          티켓은 계정 세션에 묶여 있어 로그인 연결을 열어 둔 채 쓴다 — 도우미 객체가 살아 있는 동안 연결이 유지되고 `shutdown` 이 닫는다.
 *          막는 호출이라 게임 루프가 아닌 자리(개발 명령 · 도구 · 시나리오 구동기)에서 쓴다. 비밀번호는 요청 구조체에만 있고 로그인을 보낸 뒤 지운다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountClient.h"

namespace sw
{
    class IStreamTransport;
    struct NetConnectCredentials;

    /** @brief 티켓 받기 요청입니다. 모든 칸이 필수입니다(빈 값으로 조용히 시도하지 않는다). */
    struct AccountTicketRequest
    {
        string _loginAddress{}; ///< 로그인 서비스 포트 `host:port`(서버 설정 `_servicePort`)
        string _loginName{};
        string _password{};
        string _serverID{};                   ///< 티켓을 받을 게임 서버 이름(서버 조립 `gameConnect` 의 `serverName` 과 같아야 한다)
        string _gameBuild{};                  ///< Hello 와 로그인에 싣는 게임 빌드 글
        string _platform{};                   ///< 로그인에 싣는 플랫폼 글(`[a-z0-9_]`)
        string _tlsTrustFile{};               ///< 로그인 서버의 TLS 인증서를 믿을 CA 번들 PEM(프로젝트 루트 기준). 비면 Dev 만 `Saved/Certificates/` 의 개발용 인증서를 못 박아 믿는다(서버 서비스 스트림은 늘 TLS 이다)
        int64  _timeoutMilliseconds{ 10000 }; ///< 연결부터 티켓까지 통틀어 기다리는 시한
    };

    /**
     * @class AccountTicketConnector
     * @brief 로그인 연결 하나를 들고 티켓을 받아 줍니다.
     */
    class SW_KIT_API AccountTicketConnector
    {
    public:
        AccountTicketConnector();
        ~AccountTicketConnector();

        AccountTicketConnector( const AccountTicketConnector& )            = delete;
        AccountTicketConnector& operator=( const AccountTicketConnector& ) = delete;

        /**
         * @brief 로그인해 티켓을 받습니다. 시한 안에 끝나지 않거나 서버가 거절하면 이유를 @p outError 에 적고 false 입니다(자격 없이 붙는 길로 대신하지 않는다).
         * @details 이미 연결이 있으면 먼저 닫는다. 성공하면 연결은 열린 채로 남는다(`shutdown` 으로 닫는다).
         */
        [[nodiscard]] bool acquire( AccountTicketRequest request, NetConnectCredentials& outCredentials, string& outError );
        /** @brief 로그인 연결을 닫습니다. 두 번 불러도 됩니다. */
        void shutdown();

    private:
        unique_ptr<ITLSContext>      _tlsContext; ///< 로그인 연결의 TLS(클라이언트 `_client` 가 빌린다 — 먼저 선언해 나중에 지운다)
        unique_ptr<IStreamTransport> _pTransport;
        OnlineServiceClient          _client;
        AccountClient                _account;
        bool                         _bServiceRegistered;
    };
} // namespace sw
