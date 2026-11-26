#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    namespace
    {
        /** @brief 게임 네트워크 세션의 개발 명령(`net.*`)입니다. */
        struct NetSessionCommandsInternal
        {
            /** @brief `net.traffic` 이 한 번에 보낼 수 있는 메시지 상한(신뢰 창 `NetConnection::kReliableWindow` 를 넘으면 보내기가 거절된다). */
            static constexpr int32 kMaxTrafficMessageCount = 200;

            /** @brief 명령을 받을 세션이 없을 때의 답입니다. */
            static constexpr const utf8* kNoSessionReply = "net session: none (start with -gv_netListenPort=<port> or -gv_netConnectAddress=<host:port>)";

            /** @brief 명령을 받을 세션입니다 — 없거나 꺼져 있으면 nullptr 이고 부르는 쪽이 `kNoSessionReply` 로 답한다. */
            static GameNetSession* findCommandSession()
            {
                GameNetSession* pSession = engine::findSubsystem<GameNetSession>();
                return pSession != nullptr && pSession->isActive() ? pSession : nullptr;
            }

            /** @brief `net.disconnect` — 이 프로세스의 모든 연결을 끊습니다(서버는 모든 클라이언트를, 클라이언트는 서버를). */
            static bool runDisconnect( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession();
                if ( pSession == nullptr )
                {
                    outReply = kNoSessionReply;
                    return true;
                }
                pSession->disconnectPeers();
                outReply = "net session disconnected";
                return true;
            }

            /** @brief `net.reconnect` — 끊긴 클라이언트가 다시 붙기 시작합니다. */
            static bool runReconnect( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession();
                if ( pSession == nullptr )
                {
                    outReply = kNoSessionReply;
                    return true;
                }
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
                GameNetSession* pSession = findCommandSession();
                if ( pSession == nullptr )
                {
                    outReply = kNoSessionReply;
                    return true;
                }
                messageCount = MathUtil::min( messageCount, kMaxTrafficMessageCount );
                // 종류 바이트 뒤를 0 으로 채운 덩어리다. 채널은 서술자(`GameNetSessionMessage::Traffic`)에서 온다.
                NetMessageWriter trafficWriter;
                BitWriter&       payload    = trafficWriter.beginMessage<GameNetSessionMessage::Table, GameNetSessionMessage::Traffic>();
                const int32      totalBytes = MathUtil::min( messageBytes, NetConnection::kMaxSingleMessageSize );
                for ( int32 byteIndex = 1; byteIndex < totalBytes; ++byteIndex )
                    payload.writeBits( 0u, 8 );
                vector<int32> listConnection;
                pSession->getPeers().collectConnected( listConnection );
                int32 sentCount = 0;
                for ( const int32 connectionID : listConnection )
                {
                    for ( int32 index = 0; index < messageCount; ++index )
                    {
                        const bool bSent = trafficWriter.trySendMessage( pSession->getPeers(), connectionID );
                        sentCount += bSent ? 1 : 0;
                    }
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "net traffic queued %# reliable message(s) to %# connection(s)", sentCount, listConnection.size() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `net.stats` — 이 프로세스의 연결 · 트래픽 수치 한 줄(`NetStatsReport`)입니다. 에디터 Profiler 탭이 표준 입력으로 주기적으로 묻습니다. */
            static bool runStats( const vector<string>& listArgument, string& outReply )
            {
                (void)listArgument;
                GameNetSession* pSession = findCommandSession();
                if ( pSession == nullptr )
                {
                    outReply = kNoSessionReply;
                    return true;
                }
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
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "net session: %#, connected %#", getEnumName( pSession->getRole() ), pSession->getConnectedCount() );
                if ( pSession->getRole() == GameNetRole::Server )
                    reply.appendFormat( ", port %#, shards %#", pSession->getListenPort(), pSession->getShardCount() );
                outReply.assign( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( NetStatus, "net.status", "net.status", "Show the game net session role, port and connection count", &NetSessionCommandsInternal::runStatus );
    SW_DEV_COMMAND( NetDisconnect, "net.disconnect", "net.disconnect", "Drop every connection of the net session (a client stays disconnected until net.reconnect)",
                    &NetSessionCommandsInternal::runDisconnect );
    SW_DEV_COMMAND( NetReconnect, "net.reconnect", "net.reconnect", "Connect a disconnected client to its server again", &NetSessionCommandsInternal::runReconnect );
    SW_DEV_COMMAND( NetTraffic, "net.traffic", "net.traffic <messages> [<bytes>]", "Send that many reliable messages (1 to 1024 bytes) to every peer (load to watch resends)",
                    &NetSessionCommandsInternal::runTraffic );
    SW_DEV_COMMAND( NetStats, "net.stats", "net.stats", "Show the net session's connection and traffic numbers in one line (packets, bytes per second, RTT, loss, message kinds)",
                    &NetSessionCommandsInternal::runStats );
} // namespace sw
