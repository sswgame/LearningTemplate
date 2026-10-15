#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Config/Server/ServerSecret.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/NetSecurityLoginCrypto.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Rule/AccountNameIndex.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/AccountServer.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Service/LoginService.h"

// 계정 키트의 서버 조립 — 로그인 서비스 · 바인딩을 올리고 신원(디렉터리) · 접속 상태 · 세션 끊기 · 이름 찾기를 문맥에 내놓는다.
// 옵션:
//   ticketKeySecretEnvironment  게임 접속 티켓 주 키(32 B, 16 진수 64 자)를 담은 환경 변수 이름. 비우면 Dev 만 개발용 키, Shipping 은 기동 실패.
//   attemptBurst                주소마다 한꺼번에 받는 로그인 시도 수(부하 봇은 크게).
//   lightPasswordHash           1 이면 가벼운 Argon2id(테스트 · 부하 봇 — Dev 만, 실제 비용을 재려면 끈다).

namespace sw
{
    SW_LOG_CALLER( "AccountOnlineServerKit" );

    namespace
    {
        struct AccountOnlineServerKitInternal
        {
            [[maybe_unused]] static constexpr uint32 kLightHashMemoryKiB  = 256; ///< 테스트용 가벼운 Argon2id
            [[maybe_unused]] static constexpr uint32 kLightHashIterations = 1;
            [[maybe_unused]] static constexpr uint8  kDevTicketKeyByte    = 0x5A; ///< 개발용 주 키(모든 바이트 같은 값). Shipping 은 쓰지 않는다
            static constexpr int32                   kHexDigitsPerChunk   = ServiceKeyUtil::kHexWidth;
            static constexpr int32                   kBytesPerChunk       = 8;

            /** @brief 16 진수 64 자를 32 바이트로 읽습니다(앞이 높은 자리). 형식이 틀리면 false 입니다. */
            [[nodiscard]] static bool parseTicketKey( string_view text, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize] )
            {
                const string_view trimmed = StringUtil::trim( text );
                if ( trimmed.size() != static_cast<size_t>( LoginTicketAuthority::kMasterKeySize ) * 2 )
                    return false;
                const string lower = StringUtil::toLower( string( trimmed ).c_str() );
                for ( int32 chunkIndex = 0; chunkIndex < LoginTicketAuthority::kMasterKeySize / kBytesPerChunk; ++chunkIndex )
                {
                    uint64 value = 0;
                    if ( ServiceKeyUtil::parseHex64( string_view( lower ).substr( static_cast<size_t>( chunkIndex * kHexDigitsPerChunk ), kHexDigitsPerChunk ), value ) == false )
                        return false;
                    for ( int32 byteIndex = 0; byteIndex < kBytesPerChunk; ++byteIndex )
                    {
                        const int32 shift                                  = ( kBytesPerChunk - 1 - byteIndex ) * 8;
                        outArrKey[chunkIndex * kBytesPerChunk + byteIndex] = static_cast<uint8>( ( value >> shift ) & 0xFFu );
                    }
                }
                return true;
            }
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
                , _loginService{}
                , _accountServer{}
                , _nameIndex{}
                , _bLoginStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                using Internal [[maybe_unused]] = AccountOnlineServerKitInternal;
                LoginSettings loginSettings;
                bool          bLightHash = false;
                string        ticketKeyEnvironment;
                const bool    bOptionOk = entry.readOptions( {
                                                              {"ticketKeySecretEnvironment",        ticketKeyEnvironment},
                                                              {              "attemptBurst", loginSettings._attemptBurst},
                                                              {         "lightPasswordHash",                  bLightHash}
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
                if ( readTicketKey( ticketKeyEnvironment, arrTicketKey, outError ) == false )
                    return false;
                _loginService.initialize( context._pStore, &_crypto, loginSettings, arrTicketKey );
                Memory::set( arrTicketKey, 0, sizeof( arrTicketKey ) );
                _bLoginStarted = true;
                _loginService.setRemoteConfig( context._pRemoteConfig );
                _accountServer.initialize( &_loginService, AccountServerSettings{} );
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
            }

        private:
            /** @brief 옵션의 환경 변수에서 주 키를 읽습니다. 변수를 정하지 않았으면 Dev 는 개발용 키(경고), Shipping 은 실패입니다. */
            [[nodiscard]] static bool readTicketKey( const string& environment, uint8 ( &outArrKey )[LoginTicketAuthority::kMasterKeySize], string& outError )
            {
                if ( environment.empty() )
                {
#if defined( SW_SHIPPING )
                    outError = "the game ticket master key is required - set the ticketKeySecretEnvironment option";
                    return false;
#else
                    SW_LOG_WARNING( "Account kit: no ticketKeySecretEnvironment - using the development ticket key (Dev only)" );
                    Memory::set( outArrKey, AccountOnlineServerKitInternal::kDevTicketKeyByte, sizeof( outArrKey ) );
                    return true;
#endif
                }
                string secret;
                if ( ServerSecret::read( environment, secret ) == false || secret.empty() )
                {
                    outError = "the game ticket master key environment variable '" + environment + "' is not set";
                    return false;
                }
                const bool bParsed = AccountOnlineServerKitInternal::parseTicketKey( secret, outArrKey );
                Memory::set( secret.data(), 0, secret.size() );
                if ( bParsed == false )
                {
                    outError = "the game ticket master key must be 64 hexadecimal digits (32 bytes)";
                    return false;
                }
                return true;
            }

            NetSecurityLoginCrypto _crypto;
            LoginService           _loginService;
            AccountServer          _accountServer;
            AccountNameIndex       _nameIndex;
            bool                   _bLoginStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AccountOnlineServerKit, "account", OnlineServerKitOrder::kIdentity );
} // namespace sw
