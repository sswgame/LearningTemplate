#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBotLocalServer.h"

#include "Core/Container/StringUtil.h"
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
            static constexpr int32       kUnlimitedAttemptBurst = 1000000; ///< 봇은 한 주소에서 온다 — 주소마다 제한을 사실상 끈다
            static constexpr uint16      kGameServerPort        = 7777;
            static constexpr const utf8* kLoopbackAddress       = "127.0.0.1";
            static constexpr const utf8* kDefaultBoardID        = "kills";
            static constexpr const utf8* kArrMatchMode[]        = { "solo:2x1", "duo:2x2" };

            static void addOption( OnlineServerKitEntry& outEntry, const utf8* pKey, string value )
            {
                OnlineServerOption& option = outEntry._listOption.emplace_back();
                option._key                = pKey;
                option._value              = std::move( value );
            }

            static OnlineServerKitEntry& addKit( OnlineServerConfig& outConfig, const utf8* pName )
            {
                OnlineServerKitEntry& entry = outConfig._listKit.emplace_back();
                entry._name                 = pName;
                return entry;
            }

            /** @brief 봇이 쓰는 키트 일곱과 옵션입니다. 전용 서버라면 운영자가 `<이름>.online.json` 에 적는 내용이다. */
            static void fillConfig( const LoadBotLocalServerSettings& settings, OnlineServerConfig& outConfig )
            {
                outConfig._serverID                = static_cast<int32>( settings._serverID );
                outConfig._region                  = settings._region;
                outConfig._serverKind              = "game";
                outConfig._publicAddress           = kLoopbackAddress; // 이 서버를 게임 서버로 등록한다 — 매칭 배정이 그리로 간다
                outConfig._capacity                = settings._gameServerCapacity;
                outConfig._maxConnections          = settings._maxConnections;
                outConfig._ioThreadCount           = 0; // `tick` 이 전송을 돈다
                outConfig._requestBurstPerRemote   = kUnlimitedAttemptBurst;
                outConfig._requestBurstPerAccount  = kUnlimitedAttemptBurst;
                outConfig._maxConnectionsPerRemote = settings._maxConnections; // 봇은 모두 루프백 한 주소에서 붙는다

                OnlineServerKitEntry& account = addKit( outConfig, "account" );
                addOption( account, "attemptBurst", sw::to_string( kUnlimitedAttemptBurst ) );
                if ( settings._bLightPasswordHash == SW_TRUE )
                    addOption( account, "lightPasswordHash", "1" );
                (void)addKit( outConfig, "directory" );
                OnlineServerKitEntry& chat = addKit( outConfig, "chat" );
                addOption( chat, "bannedWords", "none" );
                (void)addKit( outConfig, "social" );
                OnlineServerKitEntry& leaderboard = addKit( outConfig, "leaderboard" );
                if ( settings._listBoardID.empty() )
                    addOption( leaderboard, "board", kDefaultBoardID );
                for ( const string& boardID : settings._listBoardID )
                {
                    addOption( leaderboard, "board", boardID );
                }
                OnlineServerKitEntry& matchmaking = addKit( outConfig, "matchmaking" );
                for ( const utf8* pMode : kArrMatchMode )
                {
                    addOption( matchmaking, "mode", pMode );
                }
                (void)addKit( outConfig, "liveops" );
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
        Internal::fillConfig( settings, runtimeSettings._config );
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
