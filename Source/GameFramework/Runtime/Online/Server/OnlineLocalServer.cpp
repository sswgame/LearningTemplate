/**
 * @file OnlineLocalServer.cpp
 * @brief 개발 구성의 프로세스 안 온라인 서버입니다. 전용 서버와 같은 설정 파일로 서버 조립(`OnlineServerRuntime`)을 세우고 루프백에서 평문으로 받습니다.
 * @details 세션은 창구(`IOnlineLocalServerFactory`)로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않습니다. 이 폴더(`Server/`)는 클라이언트 구성이 짓지 않으므로
 *          클라이언트 구성에는 공장이 없고 세션이 그 까닭을 돌려줍니다. 언리얼 PIE 의 전용 서버, UGS 의 에디터 안 서비스 자리다(기억).
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/ServerConfig.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"
#include "GameFramework/Runtime/Online/Service/IOnlineLocalServer.h"

namespace sw
{
    /** @class OnlineLocalServer @brief 세션이 드는 서버 조립 하나입니다. 지우면 만든 역순으로 내립니다. */
    class OnlineLocalServer final : public IOnlineLocalServer
    {
    public:
        OnlineLocalServer()
            : _runtime{}
        {
        }
        ~OnlineLocalServer() override { _runtime.shutdown(); }

        OnlineLocalServer( const OnlineLocalServer& )            = delete;
        OnlineLocalServer& operator=( const OnlineLocalServer& ) = delete;

        [[nodiscard]] expected<void> initialize( OnlineServerRuntimeConfig settings, UnixMilliseconds nowMs ) { return _runtime.initialize( std::move( settings ), nowMs ); }

        uint16         getListenPort() const override { return _runtime.getListenPort(); }
        void           update( UnixMilliseconds nowMs ) override { _runtime.update( nowMs ); }
        IServiceStore* findStore() const override { return _runtime.getContext()._pStore; }

    private:
        OnlineServerRuntime _runtime;
    };

    /** @class OnlineLocalServerFactory @brief 프로세스 안 서버를 만드는 모듈 창구입니다(`SW_MODULE_PART`). 이름 해시가 함수 서명 글이라 `sw` 에 둔다. */
    class OnlineLocalServerFactory final : public IOnlineLocalServerFactory
    {
    public:
        static constexpr const utf8* kModulePartName = "OnlineLocalServerFactory";

        expected<unique_ptr<IOnlineLocalServer>> createLocalServer( string_view serverConfigPath, const IClock& clock, UnixMilliseconds nowMs ) override
        {
            const string              serverPath{ serverConfigPath };
            const string              onlinePath = OnlineServerConfig::makeConfigPath( serverConfigPath );
            ServerConfig              serverConfig;
            OnlineServerRuntimeConfig settings;
            if ( ConfigManager::readConfigFileInto( serverPath, serverConfig ) != ConfigReadResult::Loaded || ConfigManager::readConfigFileInto( onlinePath, settings._config ) != ConfigReadResult::Loaded )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the server config or its online config could not be read", serverPath } );
            if ( expected<void> stores = OnlineServerRuntime::resolveStores( serverConfig, settings._config, settings ); stores.has_value() == false )
                return unexpected( stores.error() );
            settings._config._ioThreadCount      = 0; // 세션의 `update` 가 전송을 돌린다
            settings._bTLS                       = SW_FALSE;
            settings._pClock                     = &clock;
            settings._listenAddress              = NetAddress::makeLoopback( 0 );
            unique_ptr<OnlineLocalServer> server = make_unique<OnlineLocalServer>();
            if ( expected<void> started = server->initialize( std::move( settings ), nowMs ); started.has_value() == false )
                return unexpected( started.error() ); // 만든 것은 `server` 의 소멸이 내린다
            return unique_ptr<IOnlineLocalServer>( std::move( server ) );
        }
    };

    SW_MODULE_PART( Engine, OnlineLocalServerFactory, IOnlineLocalServerFactory );
} // namespace sw
