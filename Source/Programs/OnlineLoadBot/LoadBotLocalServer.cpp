#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/WallClock.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Runtime/Online/Server/IOnlineStoreDriver.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerRuntime.h"

namespace sw
{
    namespace
    {
        struct LoadBotLocalServerInternal
        {
            static constexpr uint16      kGameServerPort = 7777;
            static constexpr const utf8* kAccountKit     = "account";

            /**
             * @brief 파일에서 읽은 설정 위에 부하에 딸린 값을 얹습니다. 키트 목록 · 옵션은 파일이 정본이다(코드에 다시 적지 않는다).
             * @return 가벼운 해시를 켜야 하는데 파일에 계정 키트가 없으면 그 이유입니다.
             */
            [[nodiscard]] static expected<void> applyLoad( const LoadBotLocalServerSettings& settings, OnlineServerConfig& inoutConfig )
            {
                inoutConfig._serverID                = static_cast<int32>( settings._serverID );
                inoutConfig._region                  = settings._region;
                inoutConfig._maxConnections          = settings._maxConnections;
                inoutConfig._maxConnectionsPerRemote = settings._maxConnections; // 봇은 모두 루프백 한 주소에서 붙는다
                if ( settings._bLightPasswordHash == SW_FALSE )
                    return {};
                for ( OnlineServerKitEntry& kit : inoutConfig._listKit )
                {
                    if ( kit._name != kAccountKit )
                        continue;
                    // 설정 글은 키트 모듈의 형식이라 봇이 모른다. 부하 값 하나만 글 위에 얹는다.
                    JSONDocument document;
                    if ( document.parse( kit._settings.empty() ? string_view( "{}" ) : string_view( kit._settings ) ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the account kit settings are not JSON", document.getLastError() } );
                    document.getRoot().set( "_bLightPasswordHash" ).setBool( true );
                    const expected<string> settingsText = document.dump();
                    if ( settingsText.has_value() == false )
                        return unexpected( settingsText.error() );
                    kit._settings = *settingsText;
                    return {};
                }
                return unexpected( Error{ ErrorKind::NotFound, "--light-hash needs the account kit in the online config", kAccountKit } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoadBotLocalServer::LoadBotLocalServer()
        : _runtime{ make_unique<OnlineServerRuntime>() }
    {
    }

    LoadBotLocalServer::~LoadBotLocalServer()
    {
        shutdown();
    }

    expected<void> LoadBotLocalServer::initialize( IStreamTransport* pTransport, const LoadBotLocalServerSettings& settings )
    {
        using Internal = LoadBotLocalServerInternal;
        OnlineServerRuntimeConfig runtimeSettings;
        runtimeSettings._config     = settings._config;
        const expected<void> loaded = Internal::applyLoad( settings, runtimeSettings._config );
        if ( loaded.has_value() == false )
            return loaded;
        runtimeSettings._listenAddress = NetAddress::makeLoopback( settings._port );
        runtimeSettings._pTransport    = pTransport;
        runtimeSettings._gamePort      = Internal::kGameServerPort;
        runtimeSettings._bTLS          = SW_FALSE;               // 봇과 테스트는 평문 루프백이다
        runtimeSettings._store._driver = kMemoryStoreDriverName; // 봇 서버는 부하를 재고 사라진다 - 저장을 남기지 않는다
        runtimeSettings._cache._driver = kMemoryStoreDriverName;
        runtimeSettings._pClock        = settings._pClock;
        const UnixMilliseconds startMs = settings._startMs != UnixMilliseconds{ 0 } ? settings._startMs : WallClock::nowUnixMilliseconds();
        const expected<void>   started = _runtime->initialize( std::move( runtimeSettings ), startMs );
        if ( started.has_value() == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "the online server runtime did not start", started.error().message() }.appendDetail( started.error().detail() ) );
        return {};
    }

    void LoadBotLocalServer::shutdown()
    {
        _runtime->shutdown();
    }

    void LoadBotLocalServer::update( UnixMilliseconds nowMs )
    {
        _runtime->update( nowMs );
    }

    OnlineServiceHost& LoadBotLocalServer::getHost()
    {
        return _runtime->getHost();
    }

    uint16 LoadBotLocalServer::getListenPort() const
    {
        return _runtime->getListenPort();
    }
} // namespace sw
