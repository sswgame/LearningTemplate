#include "pch.h"

#include "Core/Network/DevOnlineExtension.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/AccountDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/AccountKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/LoginTypes.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/LoginStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 계정 키트의 서버 조립 — 로그인 서비스 · 바인딩을 만들고 신원(디렉터리) · 접속 상태 · 세션 끊기 · 이름 찾기 · 외부 로그인 연동 조회를 문맥에 내놓는다.
// 설정은 `AccountKitConfig` 이고, 개발 설정의 값(가벼운 해시 · 개발 티켓 키)은 확장점 `AccountDevOptions`(이 파일 아래의 등록이 올린다)가 준다.

namespace sw
{
    namespace
    {
        struct AccountOnlineServerKitInternal
        {
            static constexpr uint64 kSeedRequestTag = 0; ///< 시드 가입의 요청 태그 — 바인딩의 호출 표에 없어 완료가 조용히 버려진다
        };

        /**
         * @class AccountOnlineServerKit
         * @brief 계정 키트의 서버 조립입니다. 로그인 서비스는 저장소 완료가 부르므로 저장소를 종료한 뒤(`shutdown`)에 종료합니다.
         */
        class AccountOnlineServerKit final : public IOnlineServerKit
        {
        public:
            AccountOnlineServerKit()
                : _crypto{ &game::getSubsystem<INetSecurityProvider>() }
                , _secretVault{}
                , _platformProviders{}
                , _loginService{}
                , _accountServer{}
                , _nameIndex{}
                , _externalLinks{ &_crypto }
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                AccountKitConfig kitConfig;
                if ( expected<void> settings = entry.readSettings( kitConfig ); settings.has_value() == false )
                    return settings;
                const expected<optional<AbsolutePath>> providersFile = entry.resolveFile( kitConfig._platformProvidersFile, "_platformProvidersFile" );
                if ( providersFile.has_value() == false )
                    return unexpected( providersFile.error() );
                const expected<optional<AbsolutePath>> trustFile = entry.resolveFile( kitConfig._platformTrustFile, "_platformTrustFile" );
                if ( trustFile.has_value() == false )
                    return unexpected( trustFile.error() );
                LoginServiceConfig loginSettings;
                loginSettings._attemptBurst = kitConfig._attemptBurst;
                // 개발 설정은 DevOnline 단위가 있을 때만 받는다 — 그 단위가 없는 구성(배포)은 기동 오류다.
                const bool bDevOption = kitConfig._bLightPasswordHash || kitConfig._listSeedTestAccount.empty() == false;
                if ( bDevOption )
                {
                    const expected<const AccountDevOptions*> devOptions = DevOnlineExtension<AccountDevOptions>::require();
                    if ( devOptions.has_value() == false )
                        return unexpected( devOptions.error() );
                    if ( kitConfig._bLightPasswordHash )
                        loginSettings._passwordHashParams = ( *devOptions )->_lightPasswordHashParams;
                    for ( const AccountSeedEntry& seed : kitConfig._listSeedTestAccount )
                    {
                        _listSeedAccount.push_back( LoginCredential{ seed._loginName, seed._password } );
                    }
                }
                const string& ticketKeyEnvironment     = kitConfig._ticketKeySecretEnvironment;
                const string& linkSecretKeyEnvironment = kitConfig._linkSecretKeySecretEnvironment;
                uint8         arrTicketKey[LoginTicketAuthority::kMasterKeySize];
                if ( expected<void> result = LoginTicketKey::readFromEnvironment( ticketKeyEnvironment, arrTicketKey ); result.has_value() == false )
                    return result;
                _loginService.initialize( context._pStore, &_crypto, loginSettings, arrTicketKey );
                Memory::wipe( arrTicketKey, sizeof( arrTicketKey ) );
                // 여기부터 실패하면 세운 로그인 서비스를 스스로 내린다 — 초기화에 실패한 키트는 shutdown 을 받지 않는다.
                expected<void> assembled =
                    createPlatformProviders( *providersFile, *trustFile, linkSecretKeyEnvironment, context._clock )
                        .and_then( [&]
                { return exposeAccount( context, entry ); } )
                        .and_then( [&]
                { return addTTLRule( context, entry, LoginStoreLogic::makeSessionTTLRule( kitConfig._expiredSessionTTL.getExpireAfterMs() ), kitConfig._expiredSessionTTL ); } );
                if ( assembled.has_value() == false )
                    shutdown();
                return assembled;
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                // 바인딩이 첫 틱에 호스트의 캐시 · 버스를 잇는다(접속 상태 · account.revoke). 탈퇴 정리에는 모든 키트의 훅을 넘긴다 — 모든 키트의 initialize 뒤다.
                _loginService.setAfterDeleteAccount( context._listAfterDeleteAccount );
                return {};
            }

            void update( UnixMilliseconds nowMs ) override
            {
                // 시드 계정은 첫 틱에 가입시킨다 — 서비스 시각이 필요하고, 완료(이미 있음 포함)는 요청 태그가 없어 바인딩이 조용히 버린다. 결과는 로그인으로 드러난다.
                if ( _listSeedAccount.empty() )
                    return;
                for ( const LoginCredential& credential : _listSeedAccount )
                {
                    _loginService.seedAccount( credential, nowMs, AccountOnlineServerKitInternal::kSeedRequestTag );
                }
                SW_LOG_INFO_ID( "AccountOnlineServerKit.SeededTestAccountDevelopment", "Seeded %# test account(s) (development setting _listSeedTestAccount)", static_cast<uint32>( _listSeedAccount.size() ) );
                _listSeedAccount.clear();
            }

            /** @brief 계정 키트의 표는 탈퇴 정리(`LoginStoreLogic::purgeAccount`)가 계정과 한 트랜잭션에 지운다 — 따로 훅이 없다. */
            const IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 맡긴 로그인 · 가입 · 티켓 요청(외부 확인 대기 포함). */
            int32 getOpenJobCount() const override { return _loginService.getPendingCount(); }

            void stop() override {}

            void shutdown() override
            {
                // 저장소를 종료하고 남은 완료를 돌린 뒤다 — 로그인 계약의 종료 순서(저장소 → 완료 → 서비스).
                _accountServer.shutdown();
                _loginService.shutdown();
                _platformProviders.shutdown(); // 로그인 서비스가 빌려 쓰던 제공자다 — 서비스 뒤에 종료한다
                _secretVault.shutdown();       // 로그인 서비스가 빌려 쓰던 연동 비밀 도구다
            }

        private:
            /** @brief 바인딩을 호스트에 올리고 신원 · 접속 상태 · 세션 끊기 · 이름 찾기 · 외부 로그인 연동 조회를 문맥에 겁니다. */
            [[nodiscard]] expected<void> exposeAccount( OnlineServerKitContext& context, const OnlineServerKitEntry& entry )
            {
                _loginService.setRemoteConfig( context._pRemoteConfig );
                AccountServerConfig serverSettings;
                serverSettings._pMetricRegistry = context._pMetricRegistry;
                _accountServer.initialize( &_loginService, serverSettings );
                return registerServerService( context, entry, &_accountServer )
                    .and_then( [&]
                { return exposeInterface<IAccountDirectory>( context, entry, _loginService ); } )
                    .and_then( [&]
                { return exposeInterface<IAccountPresence>( context, entry, *_accountServer.getPresence() ); } )
                    .and_then( [&]
                { return exposeInterface<IAccountSessionControl>( context, entry, _accountServer.getSessionControl() ); } )
                    .and_then( [&]
                { return exposeInterface<IAccountNameIndex>( context, entry, _nameIndex ); } )
                    .and_then( [&]
                { return exposeInterface<IAccountExternalLinks>( context, entry, _externalLinks ); } );
            }

            /** @brief 제공자 설정 파일을 읽어 제공자를 만들고 로그인 서비스에 등록합니다. 파일을 정하지 않았으면 외부 로그인이 없습니다. */
            [[nodiscard]] expected<void> createPlatformProviders( const optional<AbsolutePath>& providersFile, const optional<AbsolutePath>& trustFile, const string& linkSecretKeyEnvironment,
                                                                  const IClock& clock )
            {
                vector<PlatformLoginProviderConfig> listSettings;
                if ( expected<void> result = OnlineProviderFile::read<PlatformLoginProviderConfig>( providersFile, trustFile, "_platformProvidersFile", "_platformTrustFile", &PlatformLoginProviderConfig::readEntry,
                                                                                                    listSettings );
                     result.has_value() == false )
                    return result;
                if ( expected<void> result = _platformProviders.initialize( listSettings, OnlineProviderFile::getTrustText( trustFile ), clock ); result.has_value() == false )
                    return result;
                bool bNeedsSecretVault = false;
                for ( int32 index = 0; index < _platformProviders.getProviderCount(); ++index )
                {
                    if ( _loginService.registerPlatformProvider( _platformProviders.getProvider( index ) ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "platform login provider names must be unique and valid" } );
                    bNeedsSecretVault = bNeedsSecretVault || _platformProviders.getProvider( index )->supportsRevocation();
                }
                if ( bNeedsSecretVault == false )
                    return {};
                // 탈퇴 때 토큰을 철회하는 제공자가 있다 — 연동 비밀을 암호화할 키가 반드시 있어야 한다(개발용 키로 대신하지 않는다).
                if ( linkSecretKeyEnvironment.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a login provider revokes link secrets on account deletion - set _linkSecretKeySecretEnvironment" } );
                uint8 arrVaultKey[LoginSecretVault::kKeySize];
                if ( expected<void> result = LoginTicketKey::readFromEnvironment( linkSecretKeyEnvironment, arrVaultKey ); result.has_value() == false )
                    return result;
                _secretVault.initialize( &game::getSubsystem<INetSecurityProvider>(), arrVaultKey );
                Memory::wipe( arrVaultKey, sizeof( arrVaultKey ) );
                _loginService.setSecretVault( &_secretVault );
                return {};
            }

        private:
            NetSecurityLoginCrypto   _crypto;
            LoginSecretVault         _secretVault;       ///< 로그인 서비스보다 오래 산다(먼저 선언)
            PlatformLoginProviderSet _platformProviders; ///< 로그인 서비스보다 오래 산다(먼저 선언)
            LoginService             _loginService;
            AccountServer            _accountServer;
            AccountNameIndex         _nameIndex;
            AccountExternalLinks     _externalLinks;
            vector<LoginCredential>  _listSeedAccount; ///< 첫 틱에 가입시킬 개발용 계정(`_listSeedTestAccount`)
        };

        struct AccountDevOnlineInternal
        {
            static constexpr uint32 kLightHashMemoryKiB  = 256; ///< 테스트용 가벼운 Argon2id
            static constexpr uint32 kLightHashIterations = 1;
            static constexpr uint8  kDevTicketKeyByte    = 0x5A; ///< 개발 티켓 주 키의 바이트(모든 바이트 같은 값) — 로그인 서버와 게임 서버가 같은 값을 본다

            static constexpr AccountDevOptions kDevOptions{
                NetPasswordHashParams{ kLightHashMemoryKiB, kLightHashIterations },
                kDevTicketKeyByte
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEFINE_DEV_ONLINE_EXTENSION( AccountDevOptions, "the Account kit development settings (_bLightPasswordHash, _listSeedTestAccount, empty _ticketKeySecretEnvironment)" )

    SW_REGISTER_ONLINE_SERVER_KIT( AccountOnlineServerKit, OnlineServerKitName::kAccount );

    SW_REGISTER_DEV_ONLINE_EXTENSION( AccountDevOptions, AccountDevOnlineInternal::kDevOptions );
} // namespace sw
