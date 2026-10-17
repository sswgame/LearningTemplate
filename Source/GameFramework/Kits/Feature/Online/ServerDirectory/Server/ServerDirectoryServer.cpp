#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryProtocol.h"

namespace sw
{
    ServerDirectoryServer::ServerDirectoryServer()
        : _pService{ nullptr }
        , _pHost{ nullptr }
    {
    }

    void ServerDirectoryServer::initialize( ServerDirectoryService* pService )
    {
        _pService = pService;
    }

    void ServerDirectoryServer::shutdown()
    {
        if ( _pHost != nullptr )
            _pHost->unsubscribeServerBus( ServerDirectoryBus::kChangedTopic.getName(), this );
        _pHost    = nullptr;
        _pService = nullptr;
    }

    void ServerDirectoryServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _pHost = nullptr; // 구독은 호스트가 이어서 모두 해제한다
    }

    const OnlineProtocolEntry& ServerDirectoryServer::getProtocol() const
    {
        return ServerDirectoryProtocol::kOnlineEntry;
    }

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
        using Messages = ServerDirectoryProtocol::MessageTypes;
        switch ( context._method )
        {
            case ServerDirectoryMethod::kGetStatus:
            {
                (void)host.respondMessage<Messages>( context._token, _pService->makeStatus( context._nowMs ) );
                return;
            }
            case ServerDirectoryMethod::kAssignServer:
            {
                ServerAssignmentRequest request;
                if ( ServerDirectoryProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                {
                    (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                    return;
                }
                const ServerAssignment assignment = _pService->assignServer( context._accountID, request, context._nowMs );
                if ( assignment._result == ServerDirectoryResult::Invalid )
                {
                    (void)host.respondError( context._token, ServerDirectoryError::kUnknownKind );
                    return;
                }
                (void)host.respondMessage<Messages>( context._token, assignment );
                return;
            }
            case ServerDirectoryMethod::kListServers:
            {
                ServerListRequest request;
                if ( ServerDirectoryProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
                {
                    (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                    return;
                }
                ServerListReply listReply;
                if ( _pService->listServers( request._kind, context._nowMs, listReply._listEntry ) != ServerDirectoryResult::Ok )
                {
                    (void)host.respondError( context._token, ServerDirectoryError::kUnknownKind );
                    return;
                }
                (void)host.respondMessage<Messages>( context._token, listReply );
                return;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
    }

    void ServerDirectoryServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pService == nullptr )
            return;
        _pService->update( nowMs );
        if ( _pService->takeStatusChange() == false )
            return;
        BitWriter body;
        if ( ServerDirectoryProtocol::MessageTypes::write( body, _pService->makeStatus( nowMs ) ) )
            (void)host.sendPushToAll( ServerDirectoryMethod::kPushStatus, body );
    }

    void ServerDirectoryServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const ServerBus* pBus = host.getServerBus();
        if ( _pService == nullptr || ServerDirectoryBus::kChangedTopic.matches( message ) == false )
            return;
        if ( pBus != nullptr && message.getOriginServerID() == pBus->getServerID() )
            return; // 이 서버가 낸 것이다. 쓰기 완료 때 이미 다시 읽기를 걸었다
        ServerDirectoryChanged changed;
        if ( ServerDirectoryBus::kChangedTopic.read( message, changed ) == ServerBusReadResult::Read )
            _pService->notifyChanged(); // 못 읽으면(의미 버전이 다름) 주기 다시 읽기를 기다린다
    }

    void ServerDirectoryServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        host.subscribeServerBus( ServerDirectoryBus::kChangedTopic.getName(), this );
    }
} // namespace sw
