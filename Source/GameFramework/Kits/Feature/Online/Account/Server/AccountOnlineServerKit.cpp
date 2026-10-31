#include "pch.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/Config/DevOnlineExtension.h"
#include "GameFramework/Base/Online/Server/OnlineProviderFile.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/AccountDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginSecretVault.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginTicketKey.h"

// 계정 키트의 서버 조립 — 로그인 서비스 · 바인딩을 만들고 신원(디렉터리) · 접속 상태 · 세션 끊기 · 이름 찾기를 문맥에 내놓는다.
// 옵션:
//   ticketKeySecretEnvironment  게임 접속 티켓 주 키(32 B, 16 진수 64 자)를 담은 환경 변수 이름. 비우면 개발 키(DevOnline 단위), 그 단위가 없는 구성(배포)은 기동 실패.
//   attemptBurst                주소마다 한꺼번에 받는 로그인 시도 수(부하 봇은 크게).
//   lightPasswordHash           1 이면 가벼운 Argon2id(테스트 · 부하 봇 — DevOnline 단위만, 실제 비용을 재려면 끈다).
//   seedTestAccounts            `이름:비밀번호` 를 쉼표로 이은 목록. 서버가 서자마자 이 계정들을 가입시켜 둔다(개발 · 두 프로세스 시나리오 `Scripts/net/NetPairRun.py` 용 — 이미 있으면 그대로 둔다).
//                               **DevOnline 단위만** — 그 단위가 없는 구성(배포)은 기동 실패(비밀번호를 설정 파일에 적어 두는 길을 운영에 남기지 않는다). 상용 엔진의 테스트 계정 시드(언리얼 서버의 `-ini` 계정 · 유니티 UGS 의 테스트 플레이어)와 같은 자리다.
//   개발 옵션의 값(가벼운 해시 · 시드 읽기 · 개발 티켓 키)은 확장점 `AccountDevOptions`(`AccountDevOnline.cpp` 가 올린다)가 준다.
//   platformProvidersFile       외부 로그인 제공자 설정 JSON(프로젝트 루트 기준, 항목 형식은 `PlatformLoginProviderFactory::readEntry`). 기동 때 절대 경로로 풀고 못 풀면 기동 실패. 비우면 외부 로그인이 없다.
//   platformTrustFile           제공자 HTTPS 를 검증할 CA 번들 PEM(프로젝트 루트 기준). `https://` 제공자가 있으면 필수다.
//   linkSecretKeySecretEnvironment  연동 비밀(애플의 refresh token)을 저장소에 쓰기 전에 암호화할 키(32 B, 16 진수 64 자)를 담은 환경 변수 이름.
//                               탈퇴 때 연동 토큰을 철회하는 제공자(애플 로그인)를 쓰면 필수다 — 평문으로 저장하지 않는다.

namespace sw
{
    SW_LOG_CALLER( "AccountOnlineServerKit" );

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
            {
            }

            [[nodiscard]] bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                LoginServiceConfig          loginSettings;
                bool                        bLightHash = false;
                string                      ticketKeyEnvironment;
                std::optional<AbsolutePath> providersFile;
                std::optional<AbsolutePath> trustFile;
                string                      linkSecretKeyEnvironment;
                string                      seedTestAccounts;
                const bool                  bOptionOk = entry.readOptions( {
                                                              {    "ticketKeySecretEnvironment",        ticketKeyEnvironment},
                                                              {                  "attemptBurst", loginSettings._attemptBurst},
                                                              {             "lightPasswordHash",                  bLightHash},
                                                              {         "platformProvidersFile",               providersFile},
                                                              {             "platformTrustFile",                   trustFile},
                                                              {"linkSecretKeySecretEnvironment",    linkSecretKeyEnvironment},
                                                              {              "seedTestAccounts",            seedTestAccounts}
                },
                                                                           outError );
                if ( bOptionOk == false )
                    return false;
                // 개발 옵션은 DevOnline 단위가 준 값으로만 푼다 — 그 단위가 없는 구성(배포)은 기동 오류다.
                const bool bDevOption = bLightHash || seedTestAccounts.empty() == false;
                if ( bDevOption )
                {
                    const AccountDevOptions* pDevOptions = AccountDevOptions::s_devOnline.require( outError );
                    if ( pDevOptions == nullptr )
                        return false;
                    if ( bLightHash )
                        loginSettings._passwordHashParams = pDevOptions->_lightPasswordHashParams;
                    if ( seedTestAccounts.empty() == false && pDevOptions->_pParseSeedTestAccounts( seedTestAccounts, _listSeedAccount, outError ) == false )
                        return false;
                }
                uint8 arrTicketKey[LoginTicketAuthority::kMasterKeySize];
                if ( LoginTicketKey::readFromEnvironment( ticketKeyEnvironment, arrTicketKey, outError ) == false )
                    return false;
                _loginService.initialize( context._pStore, &_crypto, loginSettings, arrTicketKey );
                Memory::set( arrTicketKey, 0, sizeof( arrTicketKey ) );
                // 여기부터 실패하면 세운 로그인 서비스를 스스로 내린다 — 초기화에 실패한 키트는 shutdown 을 받지 않는다.
                const bool bAssembled = createPlatformProviders( providersFile, trustFile, linkSecretKeyEnvironment, context._clock, outError ) && exposeAccount( context, entry, outError );
                if ( bAssembled == false )
                    shutdown();
                return bAssembled;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                // 바인딩이 첫 틱에 호스트의 캐시 · 버스를 잇는다(접속 상태 · account.revoke). 여기서 할 일은 없다.
                (void)context;
                (void)outError;
                return true;
            }

            void update( UnixMilliseconds nowMs ) override
            {
                // 시드 계정은 첫 틱에 가입시킨다 — 서비스 시각이 필요하고, 완료(이미 있음 포함)는 요청 태그가 없어 바인딩이 조용히 버린다. 결과는 로그인으로 드러난다.
                if ( _listSeedAccount.empty() )
                    return;
                for ( const LoginCredential& credential : _listSeedAccount )
                {
                    _loginService.registerAccount( credential, nowMs, AccountOnlineServerKitInternal::kSeedRequestTag );
                }
                SW_LOG_INFO( "Seeded %# test account(s) (development option seedTestAccounts)", static_cast<uint32>( _listSeedAccount.size() ) );
                _listSeedAccount.clear();
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 맡긴 로그인 · 가입 · 티켓 요청(외부 확인 대기 포함). */
            int32 getOpenWorkCount() const override { return _loginService.getPendingCount(); }

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
            /** @brief 바인딩을 호스트에 올리고 신원 · 접속 상태 · 세션 끊기 · 이름 찾기를 문맥에 겁니다. */
            [[nodiscard]] bool exposeAccount( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError )
            {
                _loginService.setRemoteConfig( context._pRemoteConfig );
                _accountServer.initialize( &_loginService, AccountServerConfig{} );
                return registerServerService( context, entry, &_accountServer, outError ) && exposeInterface<IAccountDirectory>( context, entry, _loginService, outError ) && exposeInterface<IAccountPresence>( context, entry, *_accountServer.getPresence(), outError ) && exposeInterface<IAccountSessionControl>( context, entry, _accountServer.getSessionControl(), outError ) && exposeInterface<IAccountNameIndex>( context, entry, _nameIndex, outError );
            }

            /** @brief 제공자 설정 파일을 읽어 제공자를 만들고 로그인 서비스에 등록합니다. 파일을 정하지 않았으면 외부 로그인이 없습니다. */
            [[nodiscard]] bool createPlatformProviders( const std::optional<AbsolutePath>& providersFile, const std::optional<AbsolutePath>& trustFile, const string& linkSecretKeyEnvironment,
                                                        const IClock& clock, string& outError )
            {
                vector<PlatformLoginProviderConfig> listSettings;
                if ( OnlineProviderFile::read<PlatformLoginProviderConfig>( providersFile, trustFile, "platformProvidersFile", "platformTrustFile", &PlatformLoginProviderFactory::readEntry,
                                                                            listSettings, outError ) == false )
                    return false;
                if ( _platformProviders.initialize( listSettings, OnlineProviderFile::getTrustText( trustFile ), clock, outError ) == false )
                    return false;
                bool bNeedsSecretVault = false;
                for ( int32 index = 0; index < _platformProviders.getProviderCount(); ++index )
                {
                    if ( _loginService.registerPlatformProvider( _platformProviders.getProvider( index ) ) == false )
                    {
                        outError = "platform login provider names must be unique and valid";
                        return false;
                    }
                    bNeedsSecretVault = bNeedsSecretVault || _platformProviders.getProvider( index )->supportsRevocation();
                }
                if ( bNeedsSecretVault == false )
                    return true;
                // 탈퇴 때 토큰을 철회하는 제공자가 있다 — 연동 비밀을 암호화할 키가 반드시 있어야 한다(개발용 키로 대신하지 않는다).
                if ( linkSecretKeyEnvironment.empty() )
                {
                    outError = "a login provider revokes link secrets on account deletion - set the option linkSecretKeySecretEnvironment";
                    return false;
                }
                uint8 arrVaultKey[LoginSecretVault::kKeySize];
                if ( LoginTicketKey::readFromEnvironment( linkSecretKeyEnvironment, arrVaultKey, outError ) == false )
                    return false;
                _secretVault.initialize( &game::getSubsystem<INetSecurityProvider>(), arrVaultKey );
                Memory::set( arrVaultKey, 0, sizeof( arrVaultKey ) );
                _loginService.setSecretVault( &_secretVault );
                return true;
            }

            NetSecurityLoginCrypto   _crypto;
            LoginSecretVault         _secretVault;       ///< 로그인 서비스보다 오래 산다(먼저 선언)
            PlatformLoginProviderSet _platformProviders; ///< 로그인 서비스보다 오래 산다(먼저 선언)
            LoginService             _loginService;
            AccountServer            _accountServer;
            AccountNameIndex         _nameIndex;
            vector<LoginCredential>  _listSeedAccount; ///< 첫 틱에 가입시킬 개발용 계정(`seedTestAccounts`)
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEFINE_DEV_ONLINE_EXTENSION( AccountDevOptions, "the Account kit development options (lightPasswordHash, seedTestAccounts, empty ticketKeySecretEnvironment)" );

    SW_REGISTER_ONLINE_SERVER_KIT( AccountOnlineServerKit, OnlineServerKitName::kAccount );
} // namespace sw
