#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsServer.h"

#include "Core/Common/HashUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

namespace sw
{
    LiveOpsServer::LiveOpsServer()
        : _pendingTable{}
        , _listDeviceCompletionScratch{}
        , _listStartScratch{}
        , _pService{ nullptr }
        , _pDispatcher{ nullptr }
        , _pHost{ nullptr }
        , _busSubscription{}
    {
    }

    void LiveOpsServer::initialize( LiveOpsService* pService, PushNotificationDispatcher* pDispatcher )
    {
        _pService    = pService;
        _pDispatcher = pDispatcher;
    }

    void LiveOpsServer::shutdown()
    {
        _busSubscription.reset();
        _pendingTable.clear();
        _pHost       = nullptr;
        _pService    = nullptr;
        _pDispatcher = nullptr;
    }

    void LiveOpsServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _busSubscription.reset();
        _pHost = nullptr;
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
            registration._registeredMs = context._nowMs.count(); // 등록 시각은 서버 시계로 정한다. 한도를 넘으면 가장 오래된 것을 밀어낸다
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

    void LiveOpsServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
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
        announceStarts( nowMs );
        if ( _pService->takeStateChange() )
            (void)host.sendPushToAll( LiveOpsMethod::kPushLiveState, BitWriter{} );
    }

    void LiveOpsServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const ServerBus* pBus = host.getServerBus();
        if ( _pService == nullptr || LiveOpsBus::kChangedTopic.matches( message ) == false )
            return;
        if ( pBus != nullptr && message.getOriginServerID() == pBus->getServerID() )
            return; // 이 서버가 낸 것이다. 쓰기 완료 때 이미 다시 읽기를 걸었다
        LiveOpsChanged changed;
        if ( LiveOpsBus::kChangedTopic.read( message, changed ) == ServerBusReadResult::Read )
            _pService->notifyChanged(); // 못 읽으면(의미 버전이 다름) 주기 다시 읽기를 기다린다
    }

    void LiveOpsServer::announceStarts( UnixMilliseconds nowMs )
    {
        _listStartScratch.clear();
        _pService->takeStartedEvents( _listStartScratch );
        if ( _pDispatcher == nullptr )
            return;
        for ( const LiveEventStart& start : _listStartScratch )
        {
            // 지역마다 토픽 하나, 지역 제한이 없으면 전체 토픽 하나. 같은 창은 표(`liveops_push`)로 서버 여럿 중 하나만 보낸다.
            vector<string> listTopic;
            for ( const string& region : start._listRegion )
            {
                listTopic.push_back( LiveOpsPush::makeRegionTopic( region ) );
            }
            if ( listTopic.empty() )
                listTopic.push_back( LiveOpsPush::kAllTopic );
            for ( const string& topic : listTopic )
            {
                // 토픽 이름은 키 규칙 밖의 글자(대문자)를 가질 수 있어 키에는 해시를 쓴다.
                const string claimKey = start._eventID + "/" + ServiceKeyUtil::makeHex64( static_cast<uint64>( start._windowEndMs ) ) + "/" +
                                        ServiceKeyUtil::makeHex64( HashUtil::computeStringHash64( topic.data(), topic.size(), false ) );
                _pDispatcher->notifyTopicOnce( claimKey, topic, start._message, nowMs );
            }
        }
    }

    void LiveOpsServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        if ( host.getServerBus() != nullptr ) // 서버가 한 대면 들을 버스가 없다
            _busSubscription = host.subscribeServerBus( LiveOpsBus::kChangedTopic.getName(), this );
    }
} // namespace sw
