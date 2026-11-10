#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryKitConfig.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryServer.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 서버 디렉터리 키트의 서버 조립 — 점검 · 공지 · 접속 서버 배정 서비스와 바인딩을 만든다.
// 이 서버를 디렉터리에 등록하는 일(하트비트)은 키트가 아니라 런타임이 온라인 설정의 `_publicAddress` 로 한다.
// 설정은 `ServerDirectoryKitConfig` 이다.

namespace sw
{
    namespace
    {
        /**
         * @class ServerDirectoryOnlineServerKit
         * @brief 서버 디렉터리 키트의 서버 조립입니다.
         */
        class ServerDirectoryOnlineServerKit final : public IOnlineServerKit
        {
        public:
            ServerDirectoryOnlineServerKit()
                : _service{}
                , _server{}
                , _settings{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                ServerDirectoryKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                _settings._listServerKind = std::move( kitConfig._listServerKind );
                // 점검 · 공지 창은 GM 키트가 `start` 에서 읽는다(GM 키트의 선택 의존).
                return registerServerService( context, entry, &_server ).and_then( [&]
                { return exposeInterface<IServerDirectoryControl>( context, entry, _service ); } );
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                if ( context._pRouter == nullptr )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the server directory needs a cache" } );
                ServerDirectoryDependencies dependencies;
                dependencies._pStore  = context._pStore;
                dependencies._pRouter = context._pRouter;
                dependencies._pBus    = context._pBus;
                _service.initialize( dependencies, _settings );
                _server.initialize( &_service, context._pMetricRegistry );
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 저장소에 맡긴 서버 등록 · 배정 요청. */
            int32 getOpenWorkCount() const override { return _service.getPendingCount(); }

            void stop() override
            {
                if ( isStarted() == false )
                    return;
                _server.shutdown();
                _service.shutdown();
            }

        private:
            ServerDirectoryService       _service;
            ServerDirectoryServer        _server;
            ServerDirectoryServiceConfig _settings;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( ServerDirectoryOnlineServerKit, OnlineServerKitName::kDirectory );
} // namespace sw
