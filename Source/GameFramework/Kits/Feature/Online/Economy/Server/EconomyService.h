/**
 * @file EconomyService.h
 * @brief 경제 서비스(서버)입니다. 지갑, 내역, 구매, 영수증 지급 요청을 저장소 작업으로 맡기고 완료에서 응답합니다. 서비스 스레드 하나에서 씁니다.
 * @details - 요청이 들어오는 경로는 둘이다. 서비스 프레임워크(`onServiceRequest`)는 호스트가 인증, 도배 제한, 스키마 해시 비교를 이미 마친 요청을 넘긴다. `submitCall` 은 같은 프로세스의 서버 코드와 테스트가 쓴다. 둘 다 같은 작업으로 이어진다.
 *          - 저장소 완료는 호스트(`OnlineServiceHost::update`)가 서비스 틱 전에 회수한다. 호스트 없이 쓰면 호출하는 쪽이 `IServiceStore::pollCompletions` 를 부른 뒤 `tick` 을 부른다.
 *          - 구매에는 멱등 키가 필수이다. 분개 키는 `acct.<계정>/<키>` 이다. 영수증은 레지스트리에 맡기고, `tick` 이 결과를 회수해 지급 작업을 맡긴다.
 *          - 원격 설정 `feature.shop_enabled`(기본값은 켬)가 꺼지면 구매와 지급은 `kFeatureDisabled` 로 끝나고 조회는 된다. Shipping 빌드는 개발 전용 영수증 제공자를 거절한다.
 *          - 환불 알림은 `update` 가 레지스트리에서 거두어 회수 작업(`EconomyStoreLogic::revokeRefund`)을 맡긴다. 응답이 없고 결과는 지표 `economy_refund_total{result}` 이다.
 *          - 청약철회(`WithdrawPurchase`)는 설정의 기한(`_withdrawalWindowMs`, 기본 7일) 안에 쓰지 않은 만큼 회수한다.
 *          - 원장 보존 검사(`LedgerAudit`)는 예약 작업 `ledger.audit`(매일 `_auditMinuteOfDay`)으로 서버 하나가 돌린다. 자산마다 발행, 소각, 보유 총량을
 *            지표 `economy_issued_total{asset}` · `economy_burned_total{asset}` · `economy_held_total{asset}` 로 내고, 맞지 않으면 오류 로그와 `economy_ledger_imbalance{asset}` 이다.
 *          - `shutdown` 은 맡긴 작업이 모두 끝날 때까지 저장소를 회수한다(상한 5초). 저장소를 종료하기 **전에** 부른다.
 *
 *          PlayFab Economy v2 의 `PurchaseInventoryItems` 와 `Redeem*` 에 해당한다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Ledger/IBalanceNotice.h"
#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/EconomyProtocol.h"

namespace sw
{
    struct AccountPresenceResult;
    struct EconomyBalanceChange;
    struct LedgerAuditReport;

    class CurrencyCatalog;
    class IAccountPresence;
    class IServiceStore;
    class IServiceStoreWork;
    class MetricRegistry;
    class OfferCatalog;
    class ReceiptValidatorRegistry;
    class ServiceScheduler;

    /** @brief 서비스 설정입니다. 포인터는 모두 빌려 쓰며 서비스보다 오래 살아야 합니다. */
    struct EconomyServiceConfig
    {
        const CurrencyCatalog*    _pCurrencyCatalog{ nullptr };
        const OfferCatalog*       _pOfferCatalog{ nullptr };
        ReceiptValidatorRegistry* _pReceiptRegistry{ nullptr };                                                           ///< 없으면 RedeemReceipt 는 ReceiptInvalid 로 끝난다
        MetricRegistry*           _pMetricRegistry{ nullptr };                                                            ///< 없으면 지표를 세지 않는다
        int64                     _withdrawalWindowMs{ 7 * constant::kSecondsPerDay * constant::kMillisecondsPerSecond }; ///< 청약철회 기한이다(전자상거래법의 7일)
        ServiceScheduler*         _pScheduler{ nullptr };                                                                 ///< 원장 보존 검사를 예약한다. 없으면 검사하지 않는다(테스트는 `requestLedgerAudit`)
        int32                     _auditMinuteOfDay{ 4 * 60 + 30 };                                                       ///< 보존 검사 시각(UTC 분)이다. 테이블 전체를 읽으므로 한가한 시간에 둔다
        IAccountPresence*         _pPresence{ nullptr };                                                                  ///< 다른 서버에 붙은 계정에 잔액 변경 알림을 중계한다. 서버가 하나면 null 이다
        uint8                     _bAcceptSandboxReceipts{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 전송과 무관한 요청 하나입니다. 메서드에 맞는 필드만 채웁니다. */
    struct EconomyCall
    {
        /** @param nowMs 요청의 시각(서버 루프의 유닉스 밀리초)입니다. 필수입니다 — 서비스가 다른 시각으로 메우지 않습니다. */
        explicit EconomyCall( UnixMilliseconds nowMs )
            : _nowMs{ nowMs }
        {
        }

        EconomyPurchaseRequest _purchase{};
        EconomyRedeemRequest   _redeem{};
        EconomyWithdrawRequest _withdraw{};
        EconomyHistoryRequest  _history{};
        NetIdempotencyKey      _idempotencyKey{};
        AccountID              _accountID{ kInvalidAccountID };
        UnixMilliseconds       _nowMs;
        uint16                 _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @class EconomyService @brief 경제 요청 처리기입니다. */
    class SW_KIT_SERVER_API EconomyService final : public IOnlineHostService, public IBalanceNotice
    {
    public:
        using ReplyDelegate = Delegate<void( const EconomyReply& )>;

        static constexpr const utf8* kFeatureFlag = "feature.shop_enabled";

        EconomyService();
        ~EconomyService() override;

        EconomyService( const EconomyService& )            = delete;
        EconomyService& operator=( const EconomyService& ) = delete;

        /** @brief 카탈로그나 저장소가 없거나 Shipping 에 개발 전용 영수증 제공자가 있으면 false 를 반환합니다. 호스트에 등록하는 일은 호출하는 쪽이 합니다(`host.registerHostService( &service )`). */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const EconomyServiceConfig& settings );
        void               shutdown();

        /** @brief 요청 하나를 맡깁니다. 서비스 스레드에서 부릅니다. @p onReply 는 정확히 한 번 불리며, 규칙 위반 거절은 바로, 나머지는 저장소 완료에서 불립니다. */
        void submitCall( const EconomyCall& call, ReplyDelegate onReply );
        /** @brief 영수증 결과를 회수해 지급 작업을 맡기고 지표를 갱신합니다. 호스트가 있으면 `onServiceTick` 이 부릅니다. */
        void update( UnixMilliseconds nowMs );

        // IOnlineHostService
        const OnlineProtocolEntry& getProtocol() const override { return EconomyProtocol::kOnlineEntry; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void                       onHostShutdown( OnlineServiceHost& host ) override;
        // IBalanceNotice: 이 서버에 붙은 계정이면 바로, 다른 서버에 붙었으면 접속 상태를 찾아 중계한다
        void notifyBalancesChanged( AccountID accountID, const vector<LedgerBalance>& listBalance ) override;
        // 예약 작업 — 보존 검사 회차를 차지했다
        void onScheduledRun( const ScheduledRun& run );
        /** @brief 보존 검사 한 번을 맡깁니다. 예약 회차와 테스트가 부릅니다. 이미 도는 중이면 무시합니다. */
        void requestLedgerAudit();
        /** @brief 보존 검사 작업의 `complete` 입니다. 지표와 로그를 남기고 예약 회차를 마칩니다. */
        void                         completeLedgerAudit( ServiceStoreResult result, const LedgerAuditReport& report, ServiceStoreCompletion completion );
        static constexpr const utf8* kAuditJobID = "ledger.audit";
        /** @brief 마지막 보존 검사가 맞았으면 true 입니다(검사 전에는 false). */
        bool wasLastAuditBalanced() const { return _bLastAuditBalanced == SW_TRUE; }

        /** @brief 작업(work)의 `complete` 가 호출합니다. 응답과 지표를 처리합니다. */
        void completeCall( uint64 callID, const EconomyReply& reply );
        /** @brief 환불 회수 작업의 `complete` 가 호출합니다. 지표와 로그를 남기고 회수로 바뀐 잔액을 계정에 알립니다. */
        void  completeRefund( EconomyResult result, const vector<EconomyBalanceChange>& listChange, ServiceStoreCompletion completion );
        int32 getPendingCallCount() const { return static_cast<int32>( _listPendingCall.size() ); }

    private:
        /** @brief 응답을 기다리는 요청 하나입니다. 호스트 경로이면 토큰으로, 아니면 델리게이트로 응답합니다. */
        struct PendingCall
        {
            ReplyDelegate      _onReply{};
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };
            uint64             _callID{ 0 };
            uint64             _receiptTicket{ 0 }; ///< 0이 아니면 영수증 검증을 기다리는 중이다
            int64              _receivedNanoseconds{ 0 };
            AccountID          _accountID{ kInvalidAccountID };
            int32              _methodIndex{ 0 };
        };

        void startCall( const EconomyCall& call, PendingCall pending );
        void finishCall( size_t pendingIndex, const EconomyReply& reply );
        void pollReceipts();
        /** @brief 환불 알림을 거두어 회수 작업을 맡깁니다. */
        void pollRefunds();
        void onBalancePresenceFound( const AccountPresenceResult& result );

        vector<PendingCall>                          _listPendingCall;
        ServiceMetrics                               _metrics;
        EconomyServiceConfig                         _settings;
        Subscription                                 _findOwner;                  ///< 접속 상태 찾기의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        unordered_map<uint64, EconomyBalanceChanged> _mapPresenceRequestToNotice; ///< 접속 상태를 묻는 중인 잔액 변경 알림(받는 계정은 결과에 있다)
        OnlineServiceHost*                           _pHost;                      ///< `onServiceTick` 이 붙잡는다. 알림을 이 서버에 붙은 계정에 바로 보낸다
        IServiceStore*                               _pStore;
        MetricRegistry*                              _pMetricRegistry; ///< 환불 결과 지표를 센다(설정에서 복사)
        IDAllocator<uint64>                          _callIDAllocator;
        int32                                        _pendingRefundCount;
        ScheduledRun                                 _auditRun;
        uint8                                        _bAuditPending;
        uint8                                        _bAuditRunHeld;
        uint8                                        _bLastAuditBalanced;
        UnixMilliseconds                             _nowMs;
    };
} // namespace sw
