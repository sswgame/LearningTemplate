#include "pch.h"

#include "GameFramework/Runtime/Online/Service/Tests/OnlineHostTestUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/Server/ServerConfig.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"

namespace sw::test
{
    namespace
    {
        // 이름 공간 범위 객체다 — DLL 을 올릴 때 레지스트리에 오르고 내릴 때 빠진다(키트 항목과 같은 수명). 함수 안 정적 객체면 처음 쓸 때 올라 실행 중에 온라인 스키마 해시가 바뀐다.
        const OnlineProtocolEntry kTestLoginProtocol{ "OnlineHostTestUtil.Login", TestEmptyMethodTable<OnlineMethodRange::kGame>{}, OnlineMessageTypes<>{} };

        /** @brief 설정으로 서는 조립(`OnlineRuntimeFixture`)에 테스트 로그인을 올리는 키트입니다. 설정의 `testLogin` 이 고릅니다. */
        class TestLoginServerKit final : public IOnlineServerKit
        {
        public:
            [[nodiscard]] bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                return entry.readOptions( {}, outError ) && registerServerService( context, entry, &_service, outError );
            }

            [[nodiscard]] bool start( OnlineServerKitContext& context, string& outError ) override
            {
                (void)context;
                (void)outError;
                return true;
            }

            void stop() override {}

        private:
            TestLoginService _service{};
        };
    } // namespace

    const OnlineProtocolEntry& getTestLoginProtocol()
    {
        return kTestLoginProtocol;
    }

    bool OnlineRuntimeFixture::fromConfig( string_view serverJSON, string_view onlineJSON, int64 nowMs, string& outError )
    {
        ConfigManager manager;
        if ( manager.loadConfigFromJSON<ServerConfig>( string( serverJSON ), "OnlineRuntimeFixture.server" ) == false || manager.loadConfigFromJSON<OnlineServerConfig>( string( onlineJSON ), "OnlineRuntimeFixture.online" ) == false )
        {
            outError = "the server or online config text could not be read";
            return false;
        }
        OnlineServerRuntimeConfig settings;
        settings._config = *manager.getConfig<OnlineServerConfig>();
        if ( OnlineServerRuntime::resolveStores( *manager.getConfig<ServerConfig>(), settings._config, settings, outError ) == false )
            return false;
        settings._config._ioThreadCount = 0;
        settings._bTLS                  = SW_FALSE;
        settings._pClock                = &_clock;
        settings._pTransport            = _serverTransport.get();
        settings._listenAddress         = NetAddress::makeLoopback( kPort );
        return _runtime.initialize( std::move( settings ), UnixMilliseconds{ nowMs }, outError );
    }

    SW_REGISTER_ONLINE_SERVER_KIT( TestLoginServerKit, "testLogin" );
} // namespace sw::test
