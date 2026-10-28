#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryServer.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"

// 서버 디렉터리 키트의 서버 조립 — 점검 · 공지 · 접속 서버 배정 서비스와 바인딩을 만든다.
// 이 서버를 디렉터리에 등록하는 일(하트비트)은 키트가 아니라 런타임이 온라인 설정의 `_publicAddress` 로 한다.
// 옵션:
//   serverKind  배정과 목록을 받는 서버 종류(여러 번 적는다). 비우면 "game".

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

            [[nodiscard]] bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                if ( entry.readOptions( {
                                            { "serverKind", _settings._listServerKind }
                },
                                        outError ) == false )
                    return false;
                if ( _settings._listServerKind.empty() )
                    _settings._listServerKind.push_back( "game" );
                // 점검 · 공지 창은 GM 키트가 `start` 에서 읽는다(GM 키트의 선택 의존).
                return registerServerService( context, entry, &_server, outError ) && exposeInterface<IServerDirectoryControl>( context, entry, _service, outError );
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                if ( context._pRouter == nullptr )
                {
                    outError = "the server directory needs a cache";
                    return false;
                }
                ServerDirectoryDependencies dependencies;
                dependencies._pStore  = context._pStore;
                dependencies._pRouter = context._pRouter;
                dependencies._pBus    = context._pBus;
                _service.initialize( dependencies, _settings );
                _server.initialize( &_service );
                return true;
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
