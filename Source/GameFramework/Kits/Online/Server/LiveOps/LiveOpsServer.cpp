#include "pch.h"

#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Online/LiveOps/LiveOpsProtocol.h"
#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsService.h"

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

    uint32 LiveOpsServer::getProtocolVersion() const { return LiveOpsProtocol::kVersion; }

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
        string       region;
        const bool   bRegionOk    = ServiceKeyUtil::readString( body, ServerRecord::kMaxNameSize, region );
        const uint32 buildVersion = static_cast<uint32>( body.readVarUint() );
        if ( bRegionOk == false || body.hasOverflowed() )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        LiveOpsReply reply;
        _pService->computeActiveEvents( context._accountId, region, buildVersion, context._nowMs, true, reply._listEvent );
        BitWriter replyBody;
        LiveOpsProtocol::writeReply( replyBody, reply );
        (void)host.respondOk( context._token, replyBody );
    }

    void LiveOpsServer::handleDeviceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        if ( _pDispatcher == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        PushDeviceRegistration registration;
        bool                   bBodyOk = false;
        if ( context._method == LiveOpsMethod::kRegisterDevice )
        {
            bBodyOk = LiveOpsProtocol::readDevice( body, registration );
        }
        else
        {
            bBodyOk = ServiceKeyUtil::readString( body, PushLimit::kMaxProviderIdSize, registration._providerId ) &&
                      ServiceKeyUtil::readString( body, PushLimit::kMaxTokenSize, registration._token );
        }
        if ( bBodyOk == false || body.hasOverflowed() )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        const uint64 tag = _pendingTable.add( context._token );
        if ( context._method == LiveOpsMethod::kRegisterDevice )
        {
            registration._registeredMs = context._nowMs; // 등록 시각은 서버 시계(한도를 넘으면 가장 오래된 것을 밀어낸다)
            _pDispatcher->registerDevice( context._accountId, registration, tag );
        }
        else
        {
            _pDispatcher->unregisterDevice( context._accountId, registration._providerId, registration._token, tag );
        }
    }

    void LiveOpsServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pDispatcher != nullptr )
        {
            _pDispatcher->tick( nowMs );
            _listDeviceCompletionScratch.clear();
            _pDispatcher->drainCompletions( _listDeviceCompletionScratch );
            for ( const PushDeviceCompletion& completion : _listDeviceCompletionScratch )
            {
                NetRequestToken token;
                if ( _pendingTable.take( completion._requestTag, token ) == false )
                    continue;
                LiveOpsReply reply;
                reply._result = completion._result;
                BitWriter replyBody;
                LiveOpsProtocol::writeReply( replyBody, reply );
                (void)host.respondOk( token, replyBody );
            }
        }
        if ( _pService == nullptr )
            return;
        _pService->tick( nowMs );
        if ( _pService->takeStateChange() )
            (void)host.sendPushToAll( LiveOpsMethod::kPushLiveState, BitWriter{} );
    }

    void LiveOpsServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const IServerBus* pBus = host.getServerBus();
        if ( _pService == nullptr || message._topic != LiveOpsBus::kChangedTopic )
            return;
        if ( pBus != nullptr && message._originServerId == pBus->getServerId() )
            return; // 이 서버가 낸 것 — 쓰기 완료 때 이미 다시 읽기를 걸었다
        _pService->notifyChanged();
    }

    void LiveOpsServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        host.subscribeServerBus( LiveOpsBus::kChangedTopic, this );
    }
} // namespace sw
