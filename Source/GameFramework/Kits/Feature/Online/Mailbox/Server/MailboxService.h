/**
 * @file MailboxService.h
 * @brief 우편함 서비스(서버) — 목록 · 읽음 · 수령 · 모두 받기 · 지우기를 저장소 일로 맡기고 완료에서 답하며, 주기마다 만료 쓸기 · 캠페인 다시 읽기를 맡깁니다.
 * @details - 경제 서비스와 같은 모양: 입구 둘(서비스 틀 `onServiceRequest` · 전송과 무관한 `submitCall`), 일 하나 = 요청 하나, 저장소 완료는 호스트가 서비스 틱 전에 거둔다.
 *          - 수령 · 모두 받기는 멱등 키 필수(전송 층의 응답 기억이 재시도를 받고, 원장 쪽은 우편 토큰 분개 키가 막는다).
 *          - 만료 쓸기는 서버가 여럿이어도 된다 — 우편마다 판 조건 + 분개 키라 하나만 이기고 진 쪽은 실패 수에 든다.
 *          - 캠페인 목록은 메모리에 두고 `_campaignRefreshMs` 마다 다시 읽는다(서버 둘이면 그만큼 늦게 보인다 — 수령은 저장소가 막는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

namespace sw
{
    class ILedgerPolicy;
    class IServiceStore;
    class MetricCounter;
    class MetricRegistry;

    /** @brief 우편함 서비스 설정 — 포인터는 빌려 쓴다. */
    struct MailboxServiceSettings
    {
        const ILedgerPolicy* _pPolicy{ nullptr }; ///< 계정 상한(게임이 CurrencyCatalog 를 — 기반 계약이라 키트끼리 몰라도 된다)
        MetricRegistry*      _pMetricRegistry{ nullptr };
        int64                _sweepIntervalMs{ 60000 }; ///< 만료 쓸기 주기(0 = 끈다 — 예약 작업이 `requestSweep` 을 부른다)
        int64                _campaignRefreshMs{ 60000 };
        int32                _sweepBatchCount{ 256 };
    };
} // namespace sw

namespace sw
{
    /** @brief 전송과 무관한 요청 하나입니다. */
    struct MailboxCall
    {
        MailboxRequest    _request{};
        NetIdempotencyKey _idempotencyKey{};
        AccountID         _accountID{ kInvalidAccountID };
        int64             _nowMs{ 0 }; ///< 0 이면 마지막 `tick` 의 시각
        uint16            _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @class MailboxService @brief 우편함 요청 처리기입니다(서비스 스레드). */
    class SW_GF_API MailboxService final : public IOnlineService
    {
    public:
        using ReplyDelegate = Delegate<void( const MailboxReply& )>;

        MailboxService();
        ~MailboxService() override;

        MailboxService( const MailboxService& )            = delete;
        MailboxService& operator=( const MailboxService& ) = delete;

        /** @brief 저장소가 없으면 false 입니다. 첫 `tick` 이 캠페인 읽기를 맡긴다. */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const MailboxServiceSettings& settings );
        /** @brief 맡긴 일(요청 · 쓸기 · 캠페인)이 끝날 때까지 저장소를 거둡니다(상한 5 초) — 저장소를 내리기 전에. */
        void shutdown();

        void submitCall( const MailboxCall& call, ReplyDelegate onReply );
        /** @brief 주기가 되면 만료 쓸기 · 캠페인 다시 읽기 일을 맡긴다(앞의 것이 끝나지 않았으면 건너뛴다). */
        void tick( int64 nowMs );
        /** @brief 예약 작업이 부르는 쓸기 한 번(주기와 별개) — 다음 `tick` 에 맡긴다. 이미 돌고 있으면 무시한다. */
        void requestSweep();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kMailbox; }
        uint32 getProtocolVersion() const override { return MailboxProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;

        /** @brief 일의 `complete` 가 부릅니다. */
        void completeCall( uint64 callID, const MailboxReply& reply );
        /** @brief 쓸기 일의 `complete` — 지표 `mailbox_expired_total{action}`, `_bMore` 면 다음 틱에 바로 또. */
        void completeSweep( const MailboxSweepStats& stats );
        /** @brief 캠페인 일의 `complete` — 활성 · 곧 열릴 캠페인만 남겨 캐시를 바꾼다. */
        void completeCampaignRefresh( ServiceStoreResult result, vector<ServiceMailCampaign>& inoutListCampaign );

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
        void finishCall( size_t pendingIndex, const MailboxReply& reply );
        bool isIdle() const;

        vector<PendingCall>         _listPendingCall;
        vector<ServiceMailCampaign> _listCampaign;
        ServiceMetrics              _metrics;
        MailboxServiceSettings      _settings;
        IServiceStore*              _pStore;
        MetricCounter*              _arrExpiredCounter[3]; ///< discarded · returned · removed
        uint64                      _nextCallID;
        int64                       _nowMs;
        int64                       _nextSweepMs;
        int64                       _nextCampaignRefreshMs;
        uint8                       _bSweepPending;
        uint8                       _bSweepRequested;
        uint8                       _bCampaignRefreshPending;
    };
} // namespace sw
