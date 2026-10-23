#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Engine/Net/GameConnectSecurity.h"
#include "Engine/Net/GameNetSession.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Identity/ConnectAdmission.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountConnectAuthenticator.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketAuthority.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"

// 계정 키트의 게임 서버 조립(`gameConnect`, OnlineKits F5) — 전용 게임 서버의 UDP 게임 포트가 로그인 서버가 낸 접속 티켓만 받게 한다.
// 로그인 서버와 같은 주 키로 티켓 확인기(`LoginTicketAuthority`)만 세우고(저장소 없음), 접속 인증기를 엔진의 자리(`GameConnectSecurity`)에 묶는다.
// 서버 설정 `_bGameConnectTicket` 이 게임 포트를 그 자리에 잇는다. 매칭 키트의 `gameServer` 가 있으면 배정받은 계정만, 없으면 티켓이 맞는 모든 계정을 받는다.
// 옵션:
//   ticketKeySecretEnvironment  로그인 서버와 같은 티켓 주 키의 환경 변수(`account` 와 같은 규칙 — Dev 는 비우면 개발용 키).
//   serverName                  티켓이 가리켜야 하는 이 서버의 이름(필수 — 클라이언트가 `AccountClient::issueGameTicket` 에 주는 이름).

namespace sw
{
    namespace
    {
        /**
         * @class AccountGameConnectOnlineServerKit
         * @brief 게임 포트의 접속 티켓 확인 조립입니다. 인증기는 네트워크 스레드에서 불리므로 시각만 서버 루프가 넣는다.
         */
        class AccountGameConnectOnlineServerKit final : public IOnlineServerKit
        {
        public:
            AccountGameConnectOnlineServerKit()
                : _crypto{ &NetSecurity::getProvider() }
                , _ticketAuthority{}
                , _admitEveryAccount{}
                , _pAuthenticator{}
                , _serverName{}
                , _pSecurity{ nullptr }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                (void)context;
                string ticketKeyEnvironment;
                if ( entry.readOptions( {
                                            {"ticketKeySecretEnvironment", ticketKeyEnvironment},
                                            {                "serverName",          _serverName}
                },
                                        outError ) == false )
                    return false;
                if ( _serverName.empty() )
                {
                    outError = "gameConnect needs serverName - the name game tickets are issued for";
                    return false;
                }
                uint8 arrTicketKey[LoginTicketAuthority::kMasterKeySize];
                if ( LoginTicketKey::readFromEnvironment( ticketKeyEnvironment, arrTicketKey, outError ) == false )
                    return false;
                _ticketAuthority.initialize( &_crypto, arrTicketKey );
                Memory::set( arrTicketKey, 0, sizeof( arrTicketKey ) );
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                // 받아들이기 정책은 매칭 키트(`gameServer`)가 `initialize` 에서 내놓는다 — 없으면 티켓이 맞는 계정은 모두 받는 서버다(배정 없는 상시 서버).
                const IConnectAdmission& admission = context._pConnectAdmission != nullptr ? *context._pConnectAdmission
                                                                                           : static_cast<const IConnectAdmission&>( _admitEveryAccount );
                _pAuthenticator                    = make_unique<AccountConnectAuthenticator>( &_ticketAuthority, hashed_string( _serverName.c_str() ), admission );
                _pAuthenticator->setNowMs( context._nowMs );
                // 접속 보안 자리는 엔진의 네트워크 세션이 들고 있다(서비스).
                GameNetSession* pNetSession = game::findSubsystem<GameNetSession>();
                if ( pNetSession == nullptr )
                {
                    outError = "gameConnect needs the engine net session service to bind the game connect authenticator";
                    _pAuthenticator.reset();
                    return false;
                }
                GameConnectSecurity& security = pNetSession->getConnectSecurity();
                if ( security.bindAuthenticator( _pAuthenticator.get() ) == false )
                {
                    outError = "gameConnect could not bind the game connect authenticator (another assembly already did)";
                    _pAuthenticator.reset();
                    return false;
                }
                _pSecurity = &security;
                return true;
            }

            void update( int64 nowMs ) override
            {
                if ( _pAuthenticator != nullptr )
                    _pAuthenticator->setNowMs( nowMs );
            }

            void stop() override
            {
                // 풀고 나면 엔진 자리가 이 인증기를 부르지 않는다(자리의 잠금) — 그다음에 내린다.
                if ( _pSecurity != nullptr )
                    _pSecurity->unbindAuthenticator( _pAuthenticator.get() );
                _pSecurity = nullptr;
                _pAuthenticator.reset();
            }

        private:
            NetSecurityLoginCrypto                  _crypto;
            LoginTicketAuthority                    _ticketAuthority;
            AdmitEveryAccount                       _admitEveryAccount;
            unique_ptr<AccountConnectAuthenticator> _pAuthenticator;
            string                                  _serverName;
            GameConnectSecurity*                    _pSecurity; ///< 인증기를 묶은 세션의 접속 보안 자리(묶지 않았으면 nullptr)
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AccountGameConnectOnlineServerKit, "gameConnect", OnlineServerKitOrder::kIdentity + 1 );
} // namespace sw
