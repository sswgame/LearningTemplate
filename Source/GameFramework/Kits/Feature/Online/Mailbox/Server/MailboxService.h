/**
 * @file MailboxService.h
 * @brief 우편함 서비스(서버)입니다. 목록, 읽음, 수령, 모두 받기, 지우기를 저장소 작업으로 맡기고 완료에서 응답하며, 주기마다 만료 정리와 캠페인 다시 읽기를 맡깁니다.
 * @details - 경제 서비스와 같은 구조이다. 요청 경로는 둘이며 서비스 프레임워크의 `onServiceRequest` 와 전송과 무관한 `submitCall` 이다. 작업 하나가 요청 하나이고, 저장소 완료는 호스트가 서비스 틱 전에 회수한다.
 *          - 수령과 모두 받기에는 멱등 키가 필수이다. 전송 계층의 응답 기억이 재시도를 받아 주고, 원장 쪽은 우편 토큰 분개 키가 중복을 막는다.
 *          - 만료 정리는 서버가 여러 대여도 된다. 우편마다 버전 조건과 분개 키를 쓰므로 하나만 이기고, 진 쪽은 실패 수에 들어간다.
 *          - 캠페인 목록은 메모리에 두고 `_campaignRefreshMs` 마다 다시 읽는다. 서버가 둘이면 그만큼 늦게 보일 수 있으며, 수령은 저장소가 막는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

namespace sw
{
    struct AccountPresenceResult;
    struct ServiceMailNotice;

    class IAccountDirectory;
    class IAccountPresence;
    class IBalanceNotice;
    class ILedgerPolicy;
    class IOfflinePushSender;
    class IServiceStore;
    class MetricCounter;
    class MetricRegistry;
    class ServiceScheduler;

    /** @brief 우편함 서비스 설정입니다. 포인터는 빌려 씁니다. */
    struct MailboxServiceConfig
    {
        const ILedgerPolicy* _pPolicy{ nullptr }; ///< 계정 상한이다. 게임이 CurrencyCatalog 를 넘기며, 기반 계층의 계약이라 키트끼리 몰라도 된다
        MetricRegistry*      _pMetricRegistry{ nullptr };
        int64                _sweepIntervalMs{ 60000 }; ///< 만료 정리 주기이다. 예약 테이블이 있으면 그 주기의 예약 작업(`mail.sweep`)으로 서버 하나만 돌린다
        ServiceScheduler*    _pScheduler{ nullptr };    ///< 서버 조립의 예약 테이블이다. 없으면(테스트, 서버 한 대 도구) 틱 주기로 정리한다
        int64                _campaignRefreshMs{ 60000 };
        int32                _sweepBatchCount{ 256 };
        int64                _noticeIntervalMs{ 1000 };  ///< 새 우편 알림을 가져가는 주기이다. 0 이면 알림을 보내지 않는다(다른 서버가 맡는다)
        int32                _noticeBatchCount{ 32 };    ///< 한 번에 가져가는 알림 수이다(트랜잭션 상한 64 안)
        IAccountPresence*    _pPresence{ nullptr };      ///< 다른 서버에 붙은 계정을 찾고 알림을 넘긴다. 서버가 하나면 null 이다
        IBalanceNotice*      _pBalanceNotice{ nullptr }; ///< 만료로 보낸 쪽에 돌려준 재화의 잔액을 그 계정에 알린다. 경제 키트가 없으면 null 이다
        IOfflinePushSender*  _pOfflinePush{ nullptr };   ///< 접속하지 않은 계정의 기기 푸시이다(라이브 운영 키트). 없으면 오프라인 계정에는 알리지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 전송과 무관한 요청 하나입니다. */
    struct MailboxCall
    {
        /** @param nowMs 요청의 시각(서버 루프의 유닉스 밀리초)입니다. 필수입니다 — 서비스가 다른 시각으로 메우지 않습니다. */
        explicit MailboxCall( UnixMilliseconds nowMs )
            : _nowMs{ nowMs }
        {
        }

        MailboxRequest    _request{};
        NetIdempotencyKey _idempotencyKey{};
        AccountID         _accountID{ kInvalidAccountID };
        UnixMilliseconds  _nowMs;
        uint16            _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @class MailboxService @brief 우편함 요청 처리기입니다. 서비스 스레드에서 씁니다. */
    class SW_KIT_SERVER_API MailboxService final : public IOnlineHostService
    {
    public:
        using ReplyDelegate = Delegate<void( const MailboxReply& )>;

        MailboxService();
        ~MailboxService() override;

        MailboxService( const MailboxService& )            = delete;
        MailboxService& operator=( const MailboxService& ) = delete;

        /** @brief 저장소가 없으면 false 를 반환합니다. 첫 `tick` 이 캠페인 읽기를 맡깁니다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const MailboxServiceConfig& settings );
        /** @brief 맡긴 작업(요청, 만료 정리, 캠페인)이 끝날 때까지 저장소를 회수합니다. 상한은 5초이며 저장소를 종료하기 전에 부릅니다. */
        void shutdown();

        void submitCall( const MailboxCall& call, ReplyDelegate onReply );
        /** @brief 주기가 되면 만료 정리와 캠페인 다시 읽기 작업을 맡깁니다. 앞의 작업이 끝나지 않았으면 건너뜁니다. */
        void update( UnixMilliseconds nowMs );
        /** @brief 예약 작업이 부르는 만료 정리 한 번입니다. 주기와 별개이며 다음 `tick` 에 맡기고, 이미 돌고 있으면 무시합니다. */
        void requestSweep();

        // IOnlineHostService
        const OnlineProtocolEntry& getProtocol() const override { return MailboxProtocol::kOnlineEntry; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        // 예약 작업 — 만료 정리 회차를 차지했다
        void                         onScheduledRun( const ScheduledRun& run );
        static constexpr const utf8* kSweepJobID = "mail.sweep";

        /** @brief 작업의 `complete` 가 호출합니다. */
        void completeCall( uint64 callID, const MailboxReply& reply );
        /** @brief 만료 정리 작업의 `complete` 입니다. 지표 `mailbox_expired_total{action}` 을 갱신하며 `_bMore` 이면 다음 틱에 바로 다시 정리합니다. */
        void completeSweep( const MailboxSweepStats& stats, ServiceStoreCompletion completion );
        /** @brief 새 우편 알림 작업의 `complete` 입니다. 이 서버의 계정에는 알림을, 다른 서버의 계정에는 중계를, 접속하지 않은 계정에는 기기 푸시를 보냅니다. */
        void completeNotices( ServiceStoreResult result, vector<ServiceMailNotice>& inoutListNotice );
        /** @brief 캠페인 작업의 `complete` 입니다. 활성 캠페인과 곧 열릴 캠페인만 남겨 캐시를 바꿉니다. */
        void completeCampaignRefresh( ServiceStoreResult result, vector<ServiceMailCampaign>& inoutListCampaign, ServiceStoreCompletion completion );

        const vector<ServiceMailCampaign>& getCampaigns() const { return _listCampaign; }
        int32                              getPendingCallCount() const { return static_cast<int32>( _listPendingCall.size() ); }

    private:
        struct PendingCall
        {
            ReplyDelegate      _onReply{};
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };
            uint64             _callID{ 0 };
            int64              _receivedNanoseconds{ 0 };
            int32              _methodIndex{ 0 };
        };

        void startCall( const MailboxCall& call, PendingCall pending );
        void onNoticePresenceFound( const AccountPresenceResult& result );
        void sendOfflinePush( AccountID accountID );
        void finishCall( size_t pendingIndex, const MailboxReply& reply );
        bool isIdle() const;

        vector<PendingCall>                   _listPendingCall;
        vector<ServiceMailCampaign>           _listCampaign;
        ServiceMetrics                        _metrics;
        MailboxServiceConfig                  _settings;
        Subscription                          _findOwner;                  ///< 접속 상태 찾기의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        unordered_map<uint64, MailboxNewMail> _mapPresenceRequestToNotice; ///< 접속 상태를 묻는 중인 알림(받는 계정은 결과에 있다)
        IServiceStore*                        _pStore;
        OnlineServiceHost*                    _pHost;                ///< 알림을 보낼 호스트이다. 첫 틱에 받는다
        MetricCounter*                        _arrExpiredCounter[3]; ///< discarded · returned · removed
        IDAllocator<uint64>                   _callIDAllocator;
        UnixMilliseconds                      _nowMs;
        UnixMilliseconds                      _nextSweepMs;
        UnixMilliseconds                      _nextCampaignRefreshMs;
        UnixMilliseconds                      _nextNoticeMs;
        uint8                                 _bSweepPending;
        uint8                                 _bSweepRequested;
        uint8                                 _bCampaignRefreshPending;
        uint8                                 _bNoticePending;
        uint8                                 _bSweepRunHeld; ///< 예약 회차를 차지해 정리하는 중이다. 끝나면 `completeRun`
        ScheduledRun                          _sweepRun;      ///< 차지한 회차이다
    };
} // namespace sw
