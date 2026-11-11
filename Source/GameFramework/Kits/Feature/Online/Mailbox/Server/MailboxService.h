/**
 * @file MailboxService.h
 * @brief 우편함 서비스(서버)입니다. 목록, 읽음, 수령, 모두 받기, 지우기를 저장소 작업으로 맡기고 완료에서 응답하며, 주기마다 만료 정리와 캠페인 다시 읽기를 맡깁니다.
 * @details - 프로토콜 표로 선다(`OnlineProtocolService`). 메서드마다 `handleCall` 이 저장소 작업 하나를 맡기고, 완료가 같은 표지로 응답한다. 저장소 완료는 호스트가 서비스 틱 전에 회수한다.
 *          - 수령과 모두 받기에는 멱등 키가 필수이다. 전송 계층의 응답 기억이 재시도를 받아 주고, 원장 쪽은 우편 토큰 분개 키가 중복을 막는다.
 *          - 만료 정리는 서버가 여러 대여도 된다. 우편마다 버전 조건과 분개 키를 쓰므로 하나만 이기고, 진 쪽은 실패 수에 들어간다.
 *          - 캠페인 목록은 메모리에 두고 `_campaignRefreshMs` 마다 다시 읽는다. 서버가 둘이면 그만큼 늦게 보일 수 있으며, 수령은 저장소가 막는다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresenceTracker.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Mail/ServiceMailCampaign.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServicePeriodicJob.h"
#include "GameFramework/Runtime/Online/Schedule/Server/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    struct ServiceMailNotification;

    class IAccountDirectory;
    class IAccountPresence;
    class IBalanceChangedListener;
    class ILedgerPolicy;
    class IOfflinePushSender;
    class IServiceStore;
    class MetricCounter;
    class MetricRegistry;
    class ServiceScheduler;

    /** @brief 우편함 서비스 설정입니다. 포인터는 빌려 씁니다. */
    struct MailboxServiceConfig
    {
        const ILedgerPolicy*     _pPolicy{ nullptr }; ///< 계정 상한이다. 게임이 CurrencyCatalog 를 넘기며, 기반 계층의 계약이라 키트끼리 몰라도 된다
        MetricRegistry*          _pMetricRegistry{ nullptr };
        int64                    _sweepIntervalMs{ 60000 }; ///< 만료 정리 주기이다. 예약 테이블이 있으면 그 주기의 예약 작업(`mail.sweep`)으로 서버 하나만 돌린다
        ServiceScheduler*        _pScheduler{ nullptr };    ///< 서버 조립의 예약 테이블이다. 없으면(테스트, 서버 한 대 도구) 틱 주기로 정리한다
        int64                    _campaignRefreshMs{ 60000 };
        int32                    _sweepBatchCount{ 256 };
        int64                    _noticeIntervalMs{ 1000 };  ///< 새 우편 알림을 가져가는 주기이다. 0 이면 알림을 보내지 않는다(다른 서버가 맡는다)
        int32                    _noticeBatchCount{ 32 };    ///< 한 번에 가져가는 알림 수이다(트랜잭션 상한 64 안)
        IAccountPresence*        _pPresence{ nullptr };      ///< 다른 서버에 붙은 계정을 찾고 알림을 넘긴다. 서버가 하나면 null 이다
        IBalanceChangedListener* _pBalanceNotice{ nullptr }; ///< 수령 · 만료 반환으로 바뀐 잔액을 그 계정에 알린다. 경제 키트가 없으면 null 이다
        IOfflinePushSender*      _pOfflinePush{ nullptr };   ///< 접속하지 않은 계정의 기기 푸시이다(라이브 운영 키트). 없으면 오프라인 계정에는 알리지 않는다
    };
} // namespace sw

namespace sw
{
    /** @class MailboxService @brief 우편함 요청 처리기입니다. 서비스 스레드에서 씁니다. */
    class SW_KIT_SERVER_API MailboxService final : public OnlineProtocolService<MailboxService, MailboxProtocol>
    {
    public:
        static constexpr const utf8* kSweepJobID = "mail.sweep";

        MailboxService();
        ~MailboxService() override;

        MailboxService( const MailboxService& )            = delete;
        MailboxService& operator=( const MailboxService& ) = delete;

        /** @brief 저장소가 없으면 false 를 반환합니다. 첫 `tick` 이 캠페인 읽기를 맡깁니다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const MailboxServiceConfig& settings );
        /** @brief 맡긴 작업(요청, 만료 정리, 캠페인)이 끝날 때까지 저장소를 회수합니다. 상한은 5초이며 저장소를 종료하기 전에 부릅니다. */
        void shutdown();

        // 프로토콜 표의 메서드 - 저장소 작업 하나를 맡기고 완료에서 응답한다. 로그인 · 본문 · 멱등 키는 기반이 먼저 걸렀다.
        void handleCall( OnlineServerCall<MailboxMethod::List> call, const OnlineCallContext& context, const MailboxListRequest& request );
        void handleCall( OnlineServerCall<MailboxMethod::MarkRead> call, const OnlineCallContext& context, const MailboxMailRequest& request );
        void handleCall( OnlineServerCall<MailboxMethod::Claim> call, const OnlineCallContext& context, const MailboxMailRequest& request );
        void handleCall( OnlineServerCall<MailboxMethod::ClaimAll> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<MailboxMethod::Delete> call, const OnlineCallContext& context, const MailboxMailRequest& request );

        /** @brief 주기가 되면 만료 정리와 캠페인 다시 읽기 작업을 맡깁니다. 앞의 작업이 끝나지 않았으면 건너뜁니다. */
        void update( UnixMilliseconds nowMs );
        /** @brief 예약 작업이 부르는 만료 정리 한 번입니다. 주기와 별개이며 다음 `tick` 에 맡기고, 이미 돌고 있으면 무시합니다. */
        void requestSweep();

        // IOnlineServerService
        void onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        // 예약 작업 — 만료 정리 회차를 차지했다
        void onScheduledRun( const ScheduledRun& run );

        const vector<ServiceMailCampaign>& getCampaigns() const { return _listCampaign; }

    private:
        /** @brief 만료 정리의 완료입니다. 지표 `mailbox_expired_total{action}` 을 갱신하며 `_bMore` 이면 다음 틱에 바로 다시 정리합니다. */
        void completeSweep( const MailboxSweepStats& stats, ServiceStoreCompletion completion );
        /** @brief 새 우편 알림의 완료입니다. 이 서버의 계정에는 알림을, 다른 서버의 계정에는 중계를, 접속하지 않은 계정에는 기기 푸시를 보냅니다. */
        void completeNotices( ServiceStoreResult result, vector<ServiceMailNotification>& inoutListNotice );
        /** @brief 캠페인 다시 읽기의 완료입니다. 활성 캠페인과 곧 열릴 캠페인만 남겨 캐시를 바꿉니다. */
        void completeCampaignRefresh( ServiceStoreResult result, vector<ServiceMailCampaign>& inoutListCampaign, ServiceStoreCompletion completion );
        /** @brief 요청 하나의 저장소 작업을 맡깁니다. 내려가는 중이면 일시 장애로 답합니다. */
        template <typename TMethod>
        void submitCall( OnlineServerCall<TMethod> call, const OnlineCallContext& context, const typename TMethod::Request& request, bool bWithCampaigns );
        /** @brief 다른 서버에 있을 수 있는 계정을 찾은 답입니다. 붙어 있으면 중계하고, 아니면 기기 푸시를 보낸다. */
        void handleMailPresenceResult( const AccountPresenceResult& result, const MailboxNewMail& newMail );
        void sendOfflinePush( AccountID accountID );
        /** @brief 수령으로 바뀐 잔액을 그 계정에 알립니다(경제 키트의 `BalanceChanged`). 경제 키트가 없으면 하지 않는다. */
        void notifyClaimedBalances( AccountID accountID, const MailboxServiceResult& serviceResult, const MailboxClaimReply& reply );

    private:
        vector<ServiceMailCampaign> _listCampaign;
        MailboxServiceConfig        _settings;
        AccountPresenceTracker      _presenceFinds;        ///< 다른 서버에 붙은 계정을 찾는 새 우편 알림(`initialize` 가 묶고 `shutdown` 이 푼다)
        OnlineServiceHost*          _pHost;                ///< 알림을 보낼 호스트이다. 첫 틱에 받는다
        MetricCounter*              _arrExpiredCounter[3]; ///< discarded · returned · removed
        UnixMilliseconds            _nowMs;
        ServicePeriodicJob          _sweepJob;    ///< 만료 정리이다. 예약 테이블이 있으면 주기 없이 회차로만 돈다
        ServicePeriodicJob          _campaignJob; ///< 캠페인 다시 읽기이다
        ServicePeriodicJob          _noticeJob;   ///< 새 우편 알림 가져가기이다
    };
} // namespace sw
