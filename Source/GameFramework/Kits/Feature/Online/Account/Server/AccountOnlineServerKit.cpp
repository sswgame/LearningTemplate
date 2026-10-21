#include "pch.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"

// 계정 키트의 서버 조립 — 로그인 서비스 · 바인딩을 올리고 신원(디렉터리) · 접속 상태 · 세션 끊기 · 이름 찾기를 문맥에 내놓는다.
// 옵션:
//   ticketKeySecretEnvironment  게임 접속 티켓 주 키(32 B, 16 진수 64 자)를 담은 환경 변수 이름. 비우면 Dev 만 개발용 키, Shipping 은 기동 실패.
//   attemptBurst                주소마다 한꺼번에 받는 로그인 시도 수(부하 봇은 크게).
//   lightPasswordHash           1 이면 가벼운 Argon2id(테스트 · 부하 봇 — Dev 만, 실제 비용을 재려면 끈다).
//   platformProvidersFile       외부 로그인 제공자 설정 JSON(프로젝트 루트 기준, 형식은 `PlatformLoginProviderFactory::readSettings`). 비우면 외부 로그인이 없다.
//   platformTrustFile           제공자 HTTPS 를 검증할 CA 묶음 PEM(프로젝트 루트 기준). `https://` 제공자가 있으면 필수다.

namespace sw
{
    SW_LOG_CALLER( "AccountOnlineServerKit" );

    namespace
    {
        struct AccountOnlineServerKitInternal
        {
            [[maybe_unused]] static constexpr uint32 kLightHashMemoryKiB  = 256; ///< 테스트용 가벼운 Argon2id
            [[maybe_unused]] static constexpr uint32 kLightHashIterations = 1;
        };

        /**
         * @class AccountOnlineServerKit
         * @brief 계정 키트의 서버 조립입니다. 로그인 서비스는 저장소 완료가 부르므로 저장소를 내린 뒤(`shutdown`)에 내립니다.
         */
        class AccountOnlineServerKit final : public IOnlineServerKit
        {
        public:
            AccountOnlineServerKit()
                : _crypto{ &NetSecurity::getProvider() }
                , _platformProviders{}
                , _loginService{}
                , _accountServer{}
                , _nameIndex{}
                , _bLoginStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                using Internal [[maybe_unused]] = AccountOnlineServerKitInternal;
                LoginServiceConfig loginSettings;
                bool               bLightHash = false;
                string             ticketKeyEnvironment;
                string             providersFile;
                string             trustFile;
                const bool         bOptionOk = entry.readOptions( {
                                                              {"ticketKeySecretEnvironment",        ticketKeyEnvironment},
                                                              {              "attemptBurst", loginSettings._attemptBurst},
                                                              {         "lightPasswordHash",                  bLightHash},
                                                              {     "platformProvidersFile",               providersFile},
                                                              {         "platformTrustFile",                   trustFile}
                },
                                                                  outError );
                if ( bOptionOk == false )
                    return false;
                if ( bLightHash )
                {
#if defined( SW_SHIPPING )
                    outError = "lightPasswordHash is a development option and is refused in Shipping";
                    return false;
#else
                    loginSettings._passwordHashParams._memoryKiB      = Internal::kLightHashMemoryKiB;
                    loginSettings._passwordHashParams._iterationCount = Internal::kLightHashIterations;
#endif
                }
                uint8 arrTicketKey[LoginTicketAuthority::kMasterKeySize];
                if ( LoginTicketKey::readFromEnvironment( ticketKeyEnvironment, arrTicketKey, outError ) == false )
                    return false;
                _loginService.initialize( context._pStore, &_crypto, loginSettings, arrTicketKey );
                Memory::set( arrTicketKey, 0, sizeof( arrTicketKey ) );
                _bLoginStarted = true;
                if ( createPlatformProviders( providersFile, trustFile, outError ) == false )
                    return false;
                _loginService.setRemoteConfig( context._pRemoteConfig );
                _accountServer.initialize( &_loginService, AccountServerConfig{} );
                if ( registerHostService( context, entry, &_accountServer, outError ) == false )
                    return false;
                context._pAccountDirectory = &_loginService;
                context._pPresence         = _accountServer.getPresence();
                context._pSessionControl   = &_accountServer;
                context._pNameIndex        = &_nameIndex;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                // 바인딩이 첫 틱에 호스트의 캐시 · 버스를 잇는다(접속 상태 · account.revoke). 여기서 할 일은 없다.
                (void)context;
                (void)outError;
                return true;
            }

            void stop() override {}

            void shutdown() override
            {
                // 저장소를 내리고 남은 완료를 돌린 뒤다 — 로그인 계약의 종료 순서(저장소 → 완료 → 서비스).
                _accountServer.shutdown();
                if ( _bLoginStarted )
                    _loginService.shutdown();
                _bLoginStarted = false;
                _platformProviders.shutdown(); // 로그인 서비스가 빌려 쓰던 제공자다 — 서비스 뒤에 내린다
            }

        private:
            /** @brief 제공자 설정 파일을 읽어 제공자를 만들고 로그인 서비스에 등록합니다. 파일을 정하지 않았으면 외부 로그인이 없습니다. */
            [[nodiscard]] bool createPlatformProviders( const string& providersFile, const string& trustFile, string& outError )
            {
                if ( providersFile.empty() )
                {
                    if ( trustFile.empty() == false )
                    {
                        outError = "platformTrustFile is set but platformProvidersFile is not";
                        return false;
                    }
                    return true;
                }
                const string providersPath = NetSecurity::makeProjectPath( providersFile );
                string       jsonText;
                if ( FileUtil::readTextFile( providersPath, jsonText ) == false )
                {
                    outError = "cannot read the platform login providers file " + providersPath;
                    return false;
                }
                vector<PlatformLoginProviderConfig> listSettings;
                if ( PlatformLoginProviderFactory::readSettings( jsonText, listSettings, outError ) == false )
                    return false;
                if ( _platformProviders.initialize( listSettings, trustFile, outError ) == false )
                    return false;
                for ( int32 index = 0; index < _platformProviders.getProviderCount(); ++index )
                {
                    if ( _loginService.registerPlatformProvider( _platformProviders.getProvider( index ) ) == false )
                    {
                        outError = "platform login provider names must be unique and valid";
                        return false;
                    }
                }
                return true;
            }

            NetSecurityLoginCrypto   _crypto;
            PlatformLoginProviderSet _platformProviders; ///< 로그인 서비스보다 오래 산다(먼저 선언)
            LoginService             _loginService;
            AccountServer            _accountServer;
            AccountNameIndex         _nameIndex;
            bool                     _bLoginStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AccountOnlineServerKit, "account", OnlineServerKitOrder::kIdentity );
} // namespace sw
