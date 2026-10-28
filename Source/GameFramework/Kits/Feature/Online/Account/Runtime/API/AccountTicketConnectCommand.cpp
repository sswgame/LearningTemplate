#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Network/Connection/NetHostSecurity.h"
#include "Core/Network/NetTypes.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Net/GameNetSession.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountTicketConnect.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `net.ticketconnect` — 로그인 서버에 로그인해 접속 티켓을 받고, 그 티켓으로 게임 서버에 붙는 클라이언트 세션을 엽니다(OnlineKits F5 두 프로세스 시나리오).
         * @details `-gv_standardInputCommands=1` 로 띄운 App 의 표준 입력으로 보내면 시나리오 구동기가 계정 로그인 → 티켓 → 암호화 UDP 접속을 한 줄로 한다.
         *          막는 호출이라 시한(10 초)까지 게임 스레드가 선다. 세션이 이미 열려 있으면 거절한다(자격 없는 세션으로 대신하지 않는다).
         */
        struct AccountTicketConnectCommandInternal
        {
            static constexpr int64 kTimeoutMilliseconds = 10000;

            /** @brief 티켓 연결의 수명입니다. 티켓이 계정 세션에 묶여 있어 로그인 연결을 프로세스가 끝날 때까지 쥔다(명령은 한 번에 하나만 성공한다). */
            static AccountTicketConnector& getConnector()
            {
                static AccountTicketConnector s_connector;
                return s_connector;
            }

            static bool run( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 5 || listArgument.size() > 7 )
                    return false;
                GameNetSession* pSession = game::findSubsystem<GameNetSession>();
                if ( pSession == nullptr )
                {
                    outReply = "ticketconnect: no game net session service";
                    return true;
                }
                if ( pSession->isActive() )
                {
                    outReply = "ticketconnect: the net session is already open - start without -gv_netConnectAddress / -gv_netListenPort";
                    return true;
                }
                NetAddress gameAddress;
                if ( NetAddress::parse( listArgument[4], 0, gameAddress ) == false || gameAddress.isValid() == false )
                {
                    outReply = "ticketconnect: the game address needs host:port";
                    return true;
                }
                AccountTicketRequest request;
                request._loginAddress = listArgument[0];
                request._loginName    = listArgument[1];
                request._password     = listArgument[2];
                request._serverID     = listArgument[3];
                request._gameBuild    = listArgument.size() >= 6 ? listArgument[5] : "1.0.0";
                request._tlsTrustFile = listArgument.size() == 7 ? listArgument[6] : "";
    #if SW_PLATFORM_WINDOWS
                request._platform = "windows";
    #else
                request._platform = "linux";
    #endif
                request._timeoutMilliseconds = kTimeoutMilliseconds;
                NetConnectCredentials credentials;
                string                error;
                if ( getConnector().acquire( std::move( request ), credentials, error ) == false )
                {
                    outReply = "ticketconnect: " + error;
                    return true;
                }
                GameNetSessionParams params;
                if ( pSession->makeTicketClientParams( gameAddress, credentials, params ) == false || pSession->initialize( params ) == false )
                {
                    outReply = "ticketconnect: the ticket client session did not open (reason in the log)";
                    getConnector().shutdown();
                    return true;
                }
                outReply = "ticketconnect: connecting to " + listArgument[4] + " with the account ticket";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( NetTicketConnect, "net.ticketconnect", "net.ticketconnect <loginHost:port> <account> <password> <serverID> <gameHost:port> [build [trustFile]]",
                    "Log in to the login service, get a game connect ticket and open the encrypted client session with it", &AccountTicketConnectCommandInternal::run );
} // namespace sw
#endif
