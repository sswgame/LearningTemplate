#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryServer.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"

namespace sw
{
    ServerDirectoryServer::ServerDirectoryServer()
        : _pService{ nullptr }
        , _pHost{ nullptr }
        , _busSubscription{}
    {
        setReady( false );
    }

    void ServerDirectoryServer::initialize( ServerDirectoryService* pService, MetricRegistry* pMetricRegistry )
    {
        _pService = pService;
        initializeCalls( pMetricRegistry, "server_directory" );
        setReady( pService != nullptr );
    }

    void ServerDirectoryServer::shutdown()
    {
        _busSubscription.reset();
        _calls.clear();
        _pHost    = nullptr;
        _pService = nullptr;
        setReady( false );
    }

    void ServerDirectoryServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _busSubscription.reset();
        _pHost = nullptr;
    }

    void ServerDirectoryServer::handleCall( OnlineServerCall<ServerDirectoryMethod::GetStatus> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        (void)respond( call, _pService->makeStatus( context._nowMs ) );
    }

    void ServerDirectoryServer::handleCall( OnlineServerCall<ServerDirectoryMethod::AssignServer> call, const OnlineCallContext& context, const ServerAssignmentRequest& request )
    {
        ServerAssignment assignment;
        (void)finish( call, _pService->assignServer( context._accountID, request, context._nowMs, assignment ), assignment );
    }

    void ServerDirectoryServer::handleCall( OnlineServerCall<ServerDirectoryMethod::ListServers> call, const OnlineCallContext& context, const ServerListRequest& request )
    {
        ServerListReply listReply;
        (void)finish( call, _pService->listServers( request._kind, context._nowMs, listReply._listEntry ), listReply );
    }

    void ServerDirectoryServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
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
            (void)host.sendPushToAll( ServerDirectoryMethod::Status::kKind, body );
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
        if ( host.getServerBus() != nullptr ) // 서버가 한 대면 들을 버스가 없다
            _busSubscription = host.subscribeServerBus( ServerDirectoryBus::kChangedTopic.getName(), this );
    }
} // namespace sw
