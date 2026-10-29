/**
 * @file GameNetSessionDevCommands.cpp
 * @brief 게임 네트워크 세션의 개발 명령 `net.status` · `net.disconnect` · `net.reconnect` · `net.traffic` · `net.stats` 입니다(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        struct GameNetSessionDevCommandsInternal
        {
            /** @brief `net.traffic` 이 한 번에 보낼 수 있는 메시지 상한(신뢰 창 `NetConnection::kReliableWindow` 를 넘으면 보내기가 거절된다). */
            static constexpr int32 kMaxTrafficMessageCount = 200;

            /** @brief 명령을 받을 세션 — 없으면 답을 채우고 nullptr 입니다. */
            static GameNetSession* findCommandSession( string& outReply )
            {
                GameNetSession* pSession = engine::findSubsystem<GameNetSession>();
                if ( pSession == nullptr || pSession->isActive() == false )
                {
                    outReply = "net session: none (start with -gv_netListenPort=<port> or -gv_netConnectAddress=<host:port>)";
                    return nullptr;
                }
                return pSession;
            }

            /** @brief `net.disconnect` — 이 프로세스의 모든 연결을 끊습니다(서버는 모든 클라이언트를, 클라이언트는 서버를). */
            static bool runDisconnect( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                pSession->disconnectPeers();
                outReply = "net session disconnected";
                return true;
            }

            /** @brief `net.reconnect` — 끊긴 클라이언트가 다시 붙기 시작합니다. */
            static bool runReconnect( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                outReply = pSession->reconnect() ? "net session reconnecting" : "net session: only a disconnected client reconnects";
                return true;
            }

            /**
             * @brief `net.traffic <메시지 수> [<바이트>]` — 붙은 상대마다 그 크기의 신뢰 메시지를 그만큼 보냅니다(재전송 · 손실 수치를 보는 부하).
             * @details 작은 메시지는 패킷 하나에 여럿 실려 패킷 수가 적다 — 손실을 확실히 맞히려면 메시지를 패킷 하나 크기(1024 바이트까지)로 키운다.
             */
            static bool runTraffic( const vector<string>& listArgument, string& outReply )
            {
                int32      messageCount = 0;
                int32      messageBytes = 1;
                const bool bValid       = ( listArgument.size() == 1 || listArgument.size() == 2 ) && StringUtil::parseInt( listArgument[0], messageCount ) && messageCount >= 1 &&
                                    ( listArgument.size() == 1 || StringUtil::parseInt( listArgument[1], messageBytes ) );
                if ( bValid == false || messageBytes < 1 )
                    return false;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                messageCount = MathUtil::min( messageCount, kMaxTrafficMessageCount );
                vector<uint8> payloadBytes( static_cast<size_t>( MathUtil::min( messageBytes, NetConnection::kMaxSingleMessageSize ) ), uint8{ 0 } );
                payloadBytes[0] = GameNetSessionMessage::kTraffic;
                vector<int32> listConnection;
                pSession->getPeers().collectConnected( listConnection );
                int32 sentCount = 0;
                for ( const int32 connectionID : listConnection )
                {
                    for ( int32 index = 0; index < messageCount; ++index )
                    {
                        const bool bSent = pSession->getPeers().sendMessage( connectionID, NetChannelType::ReliableOrdered, payloadBytes.data(), static_cast<int32>( payloadBytes.size() ) );
                        sentCount += bSent ? 1 : 0;
                    }
                }
                outReply = formatToString( "net traffic queued %# reliable message(s) to %# connection(s)", sentCount, listConnection.size() );
                return true;
            }

            /** @brief `net.stats` — 이 프로세스의 연결 · 트래픽 수치 한 줄(`NetStatsReport`)입니다. 에디터 Profiler 탭이 표준 입력으로 주기적으로 묻습니다. */
            static bool runStats( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession( outReply );
                if ( pSession == nullptr )
                    return true;
                outReply = pSession->makeStatsReport().makeLine();
                return true;
            }

            /** @brief `net.status` — 이 프로세스의 게임 네트워크 역할 · 포트 · 연결 수입니다. */
            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                const GameNetSession* pSession = engine::findSubsystem<GameNetSession>();
                if ( pSession == nullptr || pSession->isActive() == false )
                {
                    outReply = "net session: none (start with -gv_netListenPort=<port> or -gv_netConnectAddress=<host:port>)";
                    return true;
                }
                outReply = string( "net session: " ) + getEnumName( pSession->getRole() ) + ", connected " + to_string( pSession->getConnectedCount() );
                if ( pSession->getRole() == GameNetRole::Server )
                    outReply += formatToString( ", port %#, shards %#", pSession->getListenPort(), pSession->getShardCount() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( NetStatus, "net.status", "net.status", "Show the game net session role, port and connection count", &GameNetSessionDevCommandsInternal::runStatus );
    SW_DEV_COMMAND( NetDisconnect, "net.disconnect", "net.disconnect", "Drop every connection of the net session (a client stays disconnected until net.reconnect)",
                    &GameNetSessionDevCommandsInternal::runDisconnect );
    SW_DEV_COMMAND( NetReconnect, "net.reconnect", "net.reconnect", "Connect a disconnected client to its server again", &GameNetSessionDevCommandsInternal::runReconnect );
    SW_DEV_COMMAND( NetTraffic, "net.traffic", "net.traffic <messages> [<bytes>]", "Send that many reliable messages (1 to 1024 bytes) to every peer (load to watch resends)",
                    &GameNetSessionDevCommandsInternal::runTraffic );
    SW_DEV_COMMAND( NetStats, "net.stats", "net.stats", "Show the net session's connection and traffic numbers in one line (packets, bytes per second, RTT, loss, message kinds)",
                    &GameNetSessionDevCommandsInternal::runStats );
} // namespace sw
