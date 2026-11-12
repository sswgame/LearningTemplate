#include "pch.h"

#include "GameFramework/Runtime/Online/Server/Tests/OnlineRuntimeTestUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/ServerConfig.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"

namespace sw::test
{
    namespace
    {
        /** @brief 설정으로 서는 조립(`OnlineRuntimeFixture`)에 테스트 로그인을 올리는 키트입니다. 설정의 `testLogin` 이 고릅니다. */
        class TestLoginServerKit final : public IOnlineServerKit
        {
        public:
            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                return registerServerService( context, entry, &_service );
            }

            [[nodiscard]] expected<void> start( OnlineServerKitContext& context ) override
            {
                (void)context;
                return {};
            }

            /** @brief 테스트 키트는 표가 없다. */
            const IAfterDeleteAccount* getAfterDeleteAccount() const override { return nullptr; }

            void stop() override {}

        private:
            TestLoginService _service{};
        };
    } // namespace

    expected<void> OnlineRuntimeFixture::fromConfig( string_view serverJSON, string_view onlineJSON, int64 nowMs )
    {
        ConfigManager manager;
        if ( manager.loadConfigFromJSON<ServerConfig>( string( serverJSON ), "OnlineRuntimeFixture.server" ) == false || manager.loadConfigFromJSON<OnlineServerConfig>( string( onlineJSON ), "OnlineRuntimeFixture.online" ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the server or online config text could not be read" } );
        OnlineServerRuntimeConfig settings;
        settings._config = *manager.getConfig<OnlineServerConfig>();
        if ( expected<void> stores = OnlineServerRuntime::resolveStores( *manager.getConfig<ServerConfig>(), settings._config, settings ); stores.has_value() == false )
            return stores;
        settings._config._ioThreadCount = 0;
        settings._bTLS                  = SW_FALSE;
        settings._pClock                = &_clock;
        settings._pTransport            = _serverTransport.get();
        settings._listenAddress         = NetAddress::makeLoopback( kPort );
        return _runtime.initialize( std::move( settings ), UnixMilliseconds{ nowMs } );
    }

    SW_REGISTER_ONLINE_SERVER_KIT( TestLoginServerKit, "testLogin" );
} // namespace sw::test
