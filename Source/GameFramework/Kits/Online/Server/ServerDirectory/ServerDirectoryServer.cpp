#include "pch.h"

#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryService.h"
#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryProtocol.h"

namespace sw
{
    ServerDirectoryServer::ServerDirectoryServer()
        : _pService{ nullptr }
        , _pHost{ nullptr }
    {
    }

    void ServerDirectoryServer::initialize( ServerDirectoryService* pService ) { _pService = pService; }

    void ServerDirectoryServer::shutdown()
    {
        if ( _pHost != nullptr )
            _pHost->unsubscribeServerBus( ServerDirectoryBus::kChangedTopic, this );
        _pHost    = nullptr;
        _pService = nullptr;
    }

    void ServerDirectoryServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _pHost = nullptr; // 구독은 호스트가 이어서 모두 푼다
    }

    uint32 ServerDirectoryServer::getProtocolVersion() const { return ServerDirectoryMethod::kWireVersion; }

    bool ServerDirectoryServer::isAnonymousMethod( uint16 method ) const
    {
        return method == ServerDirectoryMethod::kGetStatus || method == ServerDirectoryMethod::kListServers;
    }

    void ServerDirectoryServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pService == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        BitWriter reply;
        switch ( context._method )
        {
            case ServerDirectoryMethod::kGetStatus:
            {
                ServerDirectoryProtocol::writeStatus( reply, _pService->makeStatus( context._nowMs ) );
                break;
            }
            case ServerDirectoryMethod::kAssignServer:
            {
                ServerAssignmentRequest request;
                if ( ServerDirectoryProtocol::readAssignmentRequest( body, request ) == false )
                {
                    (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                    return;
                }
                const ServerAssignment assignment = _pService->assignServer( context._accountId, request, context._nowMs );
                if ( assignment._result == ServerDirectoryResult::Invalid )
                {
                    (void)host.respondError( context._token, ServerDirectoryError::kUnknownKind );
                    return;
                }
                ServerDirectoryProtocol::writeAssignment( reply, assignment );
                break;
            }
            case ServerDirectoryMethod::kListServers:
            {
                string kind;
                if ( ServiceKeyUtil::readString( body, ServerRecord::kMaxNameSize, kind ) == false || body.hasOverflowed() )
                {
                    (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                    return;
                }
                vector<ServerListEntry> listEntry;
                if ( _pService->listServers( kind, context._nowMs, listEntry ) != ServerDirectoryResult::Ok )
                {
                    (void)host.respondError( context._token, ServerDirectoryError::kUnknownKind );
                    return;
                }
                ServerDirectoryProtocol::writeServerList( reply, listEntry );
                break;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
        (void)host.respondOk( context._token, reply );
    }

    void ServerDirectoryServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pService == nullptr )
            return;
        _pService->tick( nowMs );
        if ( _pService->takeStatusChange() == false )
            return;
        BitWriter body;
        ServerDirectoryProtocol::writeStatus( body, _pService->makeStatus( nowMs ) );
        (void)host.sendPushToAll( ServerDirectoryMethod::kPushStatus, body );
    }

    void ServerDirectoryServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const IServerBus* pBus = host.getServerBus();
        if ( _pService == nullptr || message._topic != ServerDirectoryBus::kChangedTopic )
            return;
        if ( pBus != nullptr && message._originServerId == pBus->getServerId() )
            return; // 이 서버가 낸 것 — 쓰기 완료 때 이미 다시 읽기를 걸었다
        _pService->notifyChanged();
    }

    void ServerDirectoryServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        host.subscribeServerBus( ServerDirectoryBus::kChangedTopic, this );
    }
} // namespace sw
