#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsServer.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsProtocol.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    LiveOpsServer::LiveOpsServer()
        : _listDeviceCompletionScratch{}
        , _listStartScratch{}
        , _pService{ nullptr }
        , _pDispatcher{ nullptr }
        , _pHost{ nullptr }
        , _busSubscription{}
    {
        setReady( false );
    }

    void LiveOpsServer::initialize( LiveOpsService* pService, PushNotificationDispatcher* pDispatcher, MetricRegistry* pMetricRegistry )
    {
        _pService    = pService;
        _pDispatcher = pDispatcher;
        initializeCalls( pMetricRegistry, "liveops" );
        setReady( pService != nullptr );
    }

    void LiveOpsServer::shutdown()
    {
        _busSubscription.reset();
        _calls.clear();
        _pHost       = nullptr;
        _pService    = nullptr;
        _pDispatcher = nullptr;
        setReady( false );
    }

    void LiveOpsServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _busSubscription.reset();
        _pHost = nullptr;
    }

    void LiveOpsServer::handleCall( OnlineServerCall<LiveOpsMethod::GetLiveState> call, const OnlineCallContext& context, const LiveOpsStateRequest& request )
    {
        LiveOpsReply reply;
        _pService->computeActiveEvents( context._accountID, request._region, request._buildVersion, context._nowMs, true, reply._listEvent );
        (void)respond( call, reply );
    }

    void LiveOpsServer::handleCall( OnlineServerCall<LiveOpsMethod::RegisterDevice> call, const OnlineCallContext& context, const PushDeviceRegistration& request )
    {
        if ( _pDispatcher == nullptr )
        {
            (void)fail( call, OnlineError::kUnavailable );
            return;
        }
        PushDeviceRegistration registration = request;
        registration._registeredMs          = context._nowMs.count(); // 등록 시각은 서버 시계로 정한다. 한도를 넘으면 가장 오래된 것을 밀어낸다
        _pDispatcher->registerDevice( context._accountID, registration, call._callID );
    }

    void LiveOpsServer::handleCall( OnlineServerCall<LiveOpsMethod::UnregisterDevice> call, const OnlineCallContext& context, const LiveOpsDeviceKey& request )
    {
        if ( _pDispatcher == nullptr )
        {
            (void)fail( call, OnlineError::kUnavailable );
            return;
        }
        _pDispatcher->unregisterDevice( context._accountID, request._providerID, request._token, call._callID );
    }

    void LiveOpsServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        _calls.publishPendingCount();
        if ( _pDispatcher != nullptr )
        {
            _pDispatcher->update( nowMs );
            _listDeviceCompletionScratch.clear();
            _pDispatcher->takeCompletions( _listDeviceCompletionScratch );
            for ( const PushDeviceCompletion& completion : _listDeviceCompletionScratch )
            {
                LiveOpsReply reply;
                reply._result = completion._result;
                (void)completeCall( completion._requestTag, reply );
            }
        }
        if ( _pService == nullptr )
            return;
        _pService->update( nowMs );
        announceStarts( nowMs );
        if ( _pService->takeStateChange() )
            (void)host.sendPushToAll( LiveOpsMethod::LiveState::kKind, BitWriter{} );
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
