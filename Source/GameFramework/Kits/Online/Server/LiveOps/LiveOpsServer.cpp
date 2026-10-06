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
        : _pService{ nullptr }
        , _pHost{ nullptr }
    {
    }

    void LiveOpsServer::initialize( LiveOpsService* pService ) { _pService = pService; }

    void LiveOpsServer::shutdown()
    {
        if ( _pHost != nullptr )
            _pHost->unsubscribeServerBus( LiveOpsBus::kChangedTopic, this );
        _pHost    = nullptr;
        _pService = nullptr;
    }

    uint32 LiveOpsServer::getProtocolVersion() const { return LiveOpsProtocol::kVersion; }

    void LiveOpsServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
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

    void LiveOpsServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
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
