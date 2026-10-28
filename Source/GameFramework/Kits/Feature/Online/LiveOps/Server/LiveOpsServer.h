/**
 * @file LiveOpsServer.h
 * @brief 라이브 운영의 스트림 바인딩입니다. `IOnlineServerService`(영역 `kLiveOps`)를 구현합니다.
 * @details 열린 이벤트 요청에는 바로 응답하고, 열린 이벤트 목록이 바뀌면 이 프로세스의 모든 클라이언트에 알리며, 버스 `liveops.changed` 를 로직에 전달한다.
 *          푸시 기기 등록과 해지는 발송기에 요청 태그와 함께 맡기고 완료를 응답으로 보낸다.
 *          - 모든 메서드는 로그인한 뒤에만 쓸 수 있다. 출시 비율과 기기가 계정을 보기 때문이다. 알림에는 본문이 없으며 클라이언트가 자기 대상에 맞게 다시 받는다.
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerServerService( &server )`, 호스트 `initialize` 전에). 버스 구독은 첫 서비스 틱에서 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"

namespace sw
{
    struct LiveOpsReply;

    /**
     * @class LiveOpsServer
     * @brief 라이브 운영 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API LiveOpsServer final : public IOnlineServerService
    {
    public:
        LiveOpsServer();

        /** @brief 넘긴 객체는 빌려 씁니다. 첫 서비스 틱 전에 지정합니다. @p pDispatcher 가 없으면 기기 메서드는 kUnavailable 입니다. 발송기의 틱도 바인딩이 돌립니다. */
        void initialize( LiveOpsService* pService, PushNotificationDispatcher* pDispatcher = nullptr );
        /** @brief 버스 구독을 해제합니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // IOnlineServerService
        const OnlineProtocolEntry& getProtocol() const override;
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void                       onHostShutdown( OnlineServiceHost& host ) override;
        void                       onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        /** @brief 새로 열린 이벤트 창의 시작 푸시를 지역 토픽으로 한 번 보냅니다(`LiveOpsPush`). 발송기가 없으면 버립니다. */
        void announceStarts( UnixMilliseconds nowMs );
        void handleDeviceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body );
        /** @brief 응답을 씁니다. 범위 밖 값이 있어 쓰지 못하면(로그가 남는다) `kInternal` 로 답합니다. */
        void respond( OnlineServiceHost& host, const NetRequestToken& token, const LiveOpsReply& reply );

        ServicePendingTable          _pendingTable;
        vector<PushDeviceCompletion> _listDeviceCompletionScratch;
        vector<LiveEventStart>       _listStartScratch;
        LiveOpsService*              _pService;
        PushNotificationDispatcher*  _pDispatcher;
        OnlineServiceHost*           _pHost;           ///< 붙은 호스트이다(첫 서비스 틱에 붙는다). 붙었는지만 본다
        Subscription                 _busSubscription; ///< 버스 주제의 구독. 서버가 한 대면 비어 있다
    };
} // namespace sw
