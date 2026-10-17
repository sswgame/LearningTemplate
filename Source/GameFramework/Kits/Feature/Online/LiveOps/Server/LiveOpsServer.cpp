#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

namespace sw
{
    LiveOpsServer::LiveOpsServer()
        : _pendingTable{}
        , _listDeviceCompletionScratch{}
        , _pService{ nullptr }
        , _pDispatcher{ nullptr }
        , _pHost{ nullptr }
    {
    }

    void LiveOpsServer::initialize( LiveOpsService* pService, PushNotificationDispatcher* pDispatcher )
    {
        _pService    = pService;
        _pDispatcher = pDispatcher;
    }

    void LiveOpsServer::shutdown()
    {
        if ( _pHost != nullptr )
            _pHost->unsubscribeServerBus( LiveOpsBus::kChangedTopic, this );
        _pendingTable.clear();
        _pHost       = nullptr;
        _pService    = nullptr;
        _pDispatcher = nullptr;
    }

    void LiveOpsServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _pHost = nullptr; // 구독은 호스트가 이어서 모두 해제한다
    }

    const OnlineProtocolEntry& LiveOpsServer::getProtocol() const
    {
        return LiveOpsProtocol::kOnlineEntry;
    }

    void LiveOpsServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( context._method == LiveOpsMethod::kRegisterDevice || context._method == LiveOpsMethod::kUnregisterDevice )
        {
            handleDeviceRequest( host, context, body );
            return;
        }
        if ( _pService == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        if ( context._method != LiveOpsMethod::kGetLiveState )
        {
            (void)host.respondError( context._token, OnlineError::kNotFound );
            return;
        }
        LiveOpsStateRequest request;
        if ( LiveOpsProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        LiveOpsReply reply;
        _pService->computeActiveEvents( context._accountID, request._region, request._buildVersion, context._nowMs, true, reply._listEvent );
        respond( host, context._token, reply );
    }

    void LiveOpsServer::respond( OnlineServiceHost& host, const NetRequestToken& token, const LiveOpsReply& reply )
    {
        (void)host.respondMessage<LiveOpsProtocol::MessageTypes>( token, reply );
    }

    void LiveOpsServer::handleDeviceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pDispatcher == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        if ( context._method == LiveOpsMethod::kRegisterDevice )
        {
            PushDeviceRegistration registration;
            if ( LiveOpsProtocol::MessageTypes::read( body, registration ) != FixedReadResult::Ok )
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            registration._registeredMs = context._nowMs; // 등록 시각은 서버 시계로 정한다. 한도를 넘으면 가장 오래된 것을 밀어낸다
            _pDispatcher->registerDevice( context._accountID, registration, _pendingTable.add( context._token ) );
            return;
        }
        LiveOpsDeviceKey key;
        if ( LiveOpsProtocol::MessageTypes::read( body, key ) != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        _pDispatcher->unregisterDevice( context._accountID, key._providerID, key._token, _pendingTable.add( context._token ) );
    }

    void LiveOpsServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pDispatcher != nullptr )
        {
            _pDispatcher->update( nowMs );
            _listDeviceCompletionScratch.clear();
            _pDispatcher->drainCompletions( _listDeviceCompletionScratch );
            for ( const PushDeviceCompletion& completion : _listDeviceCompletionScratch )
            {
                NetRequestToken token;
                if ( _pendingTable.take( completion._requestTag, token ) == false )
                    continue;
                LiveOpsReply reply;
                reply._result = completion._result;
                respond( host, token, reply );
            }
        }
        if ( _pService == nullptr )
            return;
        _pService->update( nowMs );
        if ( _pService->takeStateChange() )
            (void)host.sendPushToAll( LiveOpsMethod::kPushLiveState, BitWriter{} );
    }

    void LiveOpsServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const IServerBus* pBus = host.getServerBus();
        if ( _pService == nullptr || message._topic != LiveOpsBus::kChangedTopic )
            return;
        if ( pBus != nullptr && message._originServerID == pBus->getServerID() )
            return; // 이 서버가 낸 것이다. 쓰기 완료 때 이미 다시 읽기를 걸었다
        _pService->notifyChanged();
    }

    void LiveOpsServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        host.subscribeServerBus( LiveOpsBus::kChangedTopic, this );
    }
} // namespace sw
