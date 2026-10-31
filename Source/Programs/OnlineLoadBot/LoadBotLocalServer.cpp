#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/Time/WallClock.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerRuntime.h"

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
             * @return 가벼운 해시를 켜야 하는데 파일에 계정 키트가 없으면 false 와 @p outError 입니다.
             */
            [[nodiscard]] static bool applyLoad( const LoadBotLocalServerSettings& settings, OnlineServerConfig& inoutConfig, string& outError )
            {
                inoutConfig._serverID                = static_cast<int32>( settings._serverID );
                inoutConfig._region                  = settings._region;
                inoutConfig._maxConnections          = settings._maxConnections;
                inoutConfig._maxConnectionsPerRemote = settings._maxConnections; // 봇은 모두 루프백 한 주소에서 붙는다
                if ( settings._bLightPasswordHash == SW_FALSE )
                    return true;
                for ( OnlineServerKitEntry& kit : inoutConfig._listKit )
                {
                    if ( kit._name != kAccountKit )
                        continue;
                    OnlineServerOption& option = kit._listOption.emplace_back();
                    option._key                = "lightPasswordHash";
                    option._value              = "1";
                    return true;
                }
                outError = "--light-hash needs the account kit in the online config";
                return false;
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

    bool LoadBotLocalServer::initialize( IStreamTransport* pTransport, const LoadBotLocalServerSettings& settings, string& outError )
    {
        using Internal = LoadBotLocalServerInternal;
        OnlineServerRuntimeConfig runtimeSettings;
        runtimeSettings._config = settings._config;
        if ( Internal::applyLoad( settings, runtimeSettings._config, outError ) == false )
            return false;
        runtimeSettings._listenAddress = NetAddress::makeLoopback( settings._port );
        runtimeSettings._pTransport    = pTransport;
        runtimeSettings._gamePort      = Internal::kGameServerPort;
        runtimeSettings._bTLS          = SW_FALSE; // 봇과 테스트는 평문 루프백이다
        runtimeSettings._pClock        = settings._pClock;
        const UnixMilliseconds startMs = settings._startMs != UnixMilliseconds{ 0 } ? settings._startMs : WallClock::nowUnixMilliseconds();
        return _runtime->initialize( std::move( runtimeSettings ), startMs, outError );
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
