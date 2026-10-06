/**
 * @file LiveOpsServer.h
 * @brief 라이브 운영의 스트림 바인딩 — `IOnlineService`(영역 `kLiveOps`). 열린 이벤트 요청에 바로 답하고, 열린 묶음이 바뀌면 이 프로세스의 모두에게 알리고,
 *        버스 `liveops.changed` 를 로직에 전합니다. 푸시 기기 등록 · 해지는 발송기에 꼬리표로 맡기고 완료를 응답으로 보냅니다.
 * @details - 모든 메서드는 로그인 뒤(출시 비율 · 기기가 계정을 본다). 알림은 몸이 없다 — 클라이언트가 자기 대상으로 다시 받는다.
 *          - 호스트에 올리는 것은 부르는 쪽이(`host.registerService( &server )` — 호스트 `initialize` 전에). 버스 구독은 첫 서비스 틱에.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Server/LiveOps/Push/PushNotificationDispatcher.h"

namespace sw
{
    class LiveOpsService;

    /**
     * @class LiveOpsServer
     * @brief 라이브 운영 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API LiveOpsServer final : public IOnlineService
    {
    public:
        LiveOpsServer();

        /** @brief 넘긴 것은 빌려 쓴다(첫 서비스 틱 전에). @p pDispatcher 가 없으면 기기 메서드는 kUnavailable. 발송기의 틱도 바인딩이 돈다. */
        void initialize( LiveOpsService* pService, PushNotificationDispatcher* pDispatcher = nullptr );
        /** @brief 버스 구독을 풉니다. 로직은 부르는 쪽이 내린다. */
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kLiveOps; }
        uint32 getProtocolVersion() const override;
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        void handleDeviceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body );

        ServicePendingTable          _pendingTable;
        vector<PushDeviceCompletion> _listDeviceCompletionScratch;
        LiveOpsService*              _pService;
        PushNotificationDispatcher*  _pDispatcher;
        OnlineServiceHost*           _pHost; ///< 버스를 구독한 호스트(풀 때)
    };
} // namespace sw
