/**
 * @file EconomyService.h
 * @brief 경제 서비스(서버)입니다. 지갑, 내역, 구매, 영수증 지급 요청을 저장소 작업으로 맡기고 완료에서 응답합니다. 서비스 스레드 하나에서 씁니다.
 * @details - 요청이 들어오는 경로는 둘이다. 서비스 프레임워크(`onServiceRequest`)는 호스트가 인증, 도배 제한, 스키마 해시 비교를 이미 마친 요청을 넘긴다. `submitCall` 은 같은 프로세스의 서버 코드와 테스트가 쓴다. 둘 다 같은 작업으로 이어진다.
 *          - 저장소 완료는 호스트(`OnlineServiceHost::update`)가 서비스 틱 전에 회수한다. 호스트 없이 쓰면 호출하는 쪽이 `IServiceStore::pollCompletions` 를 부른 뒤 `tick` 을 부른다.
 *          - 구매에는 멱등 키가 필수이다. 분개 키는 `acct.<계정>/<키>` 이다. 영수증은 레지스트리에 맡기고, `tick` 이 결과를 회수해 지급 작업을 맡긴다.
 *          - 원격 설정 `feature.shop_enabled`(기본값은 켬)가 꺼지면 구매와 지급은 `kFeatureDisabled` 로 끝나고 조회는 된다. Shipping 빌드는 개발 전용 영수증 제공자를 거절한다.
 *          - 환불 알림은 `update` 가 레지스트리에서 거두어 회수 작업(`EconomyStoreLogic::revokeRefund`)을 맡긴다. 응답이 없고 결과는 지표 `economy_refund_total{result}` 이다.
 *          - 환불 조회의 기준 시각은 저장소(`econ_refund_cursor`)에 남는다. 기동 때 읽어 제공자에 넘기고(`restoreRefundCursor`, 그 전에 제공자는 조회하지 않는다), 끝까지 읽은 조회마다 쓴다. 못 읽으면 조회를 시작하지 않고 다시 읽는다.
 *          - 영수증 구매자를 외부 로그인으로 묶는 제공자가 있는데 계정 연동 포트(`IAccountExternalLinks`)가 없으면 `initialize` 가 실패한다 — 묶지 않은 채로 뜨지 않는다.
 *          - 청약철회(`WithdrawPurchase`)는 설정의 기한(`_withdrawalWindowMs`, 기본 7일) 안에 쓰지 않은 만큼 회수한다.
 *          - 원장 보존 검사(`LedgerAudit`)는 예약 작업 `ledger.audit`(매일 `_auditMinuteOfDay`)으로 서버 하나가 돌린다. 자산마다 발행, 소각, 보유 총량을
 *            지표 `economy_issued_total{asset}` · `economy_burned_total{asset}` · `economy_held_total{asset}` 로 내고, 맞지 않으면 오류 로그와 `economy_ledger_imbalance{asset}` 이다.
 *          - `shutdown` 은 맡긴 작업이 모두 끝날 때까지 저장소를 회수한다(상한 5초). 저장소를 종료하기 **전에** 부른다.
 *
 *          PlayFab Economy v2 의 `PurchaseInventoryItems` 와 `Redeem*` 에 해당한다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/EconomyProtocol.h"
#include "GameFramework/Runtime/Online/Ledger/IBalanceNotice.h"
#include "GameFramework/Runtime/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Runtime/Online/Schedule/ServiceScheduler.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

namespace sw
{
    struct AccountPresenceResult;
    struct EconomyBalanceChange;
    struct LedgerAuditReport;
    struct ReceiptRefundNotice;

    class CurrencyCatalog;
    class IAccountExternalLinks;
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
        const CurrencyCatalog*       _pCurrencyCatalog{ nullptr };
        const OfferCatalog*          _pOfferCatalog{ nullptr };
        ReceiptValidatorRegistry*    _pReceiptRegistry{ nullptr };                                                                       ///< 없으면 RedeemReceipt 는 ReceiptInvalid 로 끝난다
        MetricRegistry*              _pMetricRegistry{ nullptr };                                                                        ///< 없으면 지표를 세지 않는다
        int64                        _withdrawalWindowMs{ 7 * constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond }; ///< 청약철회 기한이다(전자상거래법의 7일)
        ServiceScheduler*            _pScheduler{ nullptr };                                                                             ///< 원장 보존 검사를 예약한다. 없으면 검사하지 않는다(테스트는 `requestLedgerAudit`)
        int64                        _refundCursorRetryMs{ 30000 };                                                                      ///< 환불 조회 기준 시각을 읽지 못했을 때 다시 읽는 간격이다
        int32                        _auditMinuteOfDay{ 4 * 60 + 30 };                                                                   ///< 보존 검사 시각(UTC 분)이다. 테이블 전체를 읽으므로 한가한 시간에 둔다
        IAccountPresence*            _pPresence{ nullptr };                                                                              ///< 다른 서버에 붙은 계정에 잔액 변경 알림을 중계한다. 서버가 하나면 null 이다
        const IAccountExternalLinks* _pExternalLinks{ nullptr };                                                                         ///< 영수증 구매자를 외부 로그인으로 묶는 제공자(`ReceiptBuyerBinding::LinkedLogin`)가 있으면 필수다. 없으면 기동 실패다
        uint8                        _bAcceptSandboxReceipts{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 스토어 하나의 환불 조회 기준 시각입니다. */
    struct EconomyRefundCursor
    {
        string _storeName{};
        int64  _lastReadMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 전송과 무관한 요청 하나입니다. 메서드에 맞는 필드만 채웁니다. */
    struct EconomyCall
    {

        EconomyPurchaseRequest _purchase{};
        EconomyRedeemRequest   _redeem{};
        EconomyWithdrawRequest _withdraw{};
        EconomyHistoryRequest  _history{};
        NetIdempotencyKey      _idempotencyKey{};
        AccountID              _accountID{ kInvalidAccountID };
        UnixMilliseconds       _nowMs;
        uint16                 _method{ 0 };
        /** @param nowMs 요청의 시각(서버 루프의 유닉스 밀리초)입니다. 필수입니다 — 서비스가 다른 시각으로 메우지 않습니다. */
        explicit EconomyCall( UnixMilliseconds nowMs )
            : _nowMs{ nowMs }
        {
        }
    };
} // namespace sw

namespace sw
{
    /** @class EconomyService @brief 경제 요청 처리기입니다. */
    class SW_KIT_SERVER_API EconomyService final : public IOnlineServerService
    {
    public:
        using ReplyDelegate = Delegate<void( const EconomyReply& )>;

        static constexpr const utf8* kFeatureFlag = "feature.shop_enabled";
        static constexpr const utf8* kAuditJobID  = "ledger.audit";

        EconomyService();
        ~EconomyService() override;

        EconomyService( const EconomyService& )            = delete;
        EconomyService& operator=( const EconomyService& ) = delete;

        /** @brief 카탈로그나 저장소가 없거나 Shipping 에 개발 전용 영수증 제공자가 있으면 false 를 반환합니다. 호스트에 등록하는 일은 호출하는 쪽이 합니다(`host.registerServerService( &service )`). */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const EconomyServiceConfig& settings );
        void               shutdown();

        /** @brief 요청 하나를 맡깁니다. 서비스 스레드에서 부릅니다. @p onReply 는 정확히 한 번 불리며, 규칙 위반 거절은 바로, 나머지는 저장소 완료에서 불립니다. */
        void submitCall( const EconomyCall& call, ReplyDelegate onReply );
        /** @brief 영수증 결과를 회수해 지급 작업을 맡기고 지표를 갱신합니다. 호스트가 있으면 `onServiceTick` 이 부릅니다. */
        void update( UnixMilliseconds nowMs );

        // IOnlineServerService
        const OnlineProtocolEntry& getProtocol() const override { return EconomyProtocol::kOnlineEntry; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void                       onHostShutdown( OnlineServiceHost& host ) override;
        /** @brief 잔액 변경을 계정에 알립니다. 이 서버에 붙은 계정이면 바로, 다른 서버에 붙었으면 접속 상태를 찾아 중계한다. `getBalanceNotice` 가 이 함수로 넘깁니다. */
        void notifyBalancesChanged( AccountID accountID, const vector<LedgerBalance>& listBalance );
        /** @brief 다른 키트(GM · 우편함)에 거는 잔액 변경 알림 창구입니다. 이 서비스와 수명이 같습니다. */
        IBalanceNotice& getBalanceNotice() { return _balanceNotice; }
        // 예약 작업 — 보존 검사 회차를 차지했다
        void onScheduledRun( const ScheduledRun& run );
        /** @brief 보존 검사 한 번을 맡깁니다. 예약 회차와 테스트가 부릅니다. 이미 도는 중이면 무시합니다. */
        void requestLedgerAudit();
        /** @brief 보존 검사 작업의 `complete` 입니다. 지표와 로그를 남기고 예약 회차를 마칩니다. */
        void completeLedgerAudit( ServiceStoreResult result, const LedgerAuditReport& report, ServiceStoreCompletion completion );
        /** @brief 마지막 보존 검사가 맞았으면 true 입니다(검사 전에는 false). */
        bool wasLastAuditBalanced() const { return _bLastAuditBalanced == SW_TRUE; }

        /** @brief 작업(work)의 `complete` 가 호출합니다. 응답과 지표를 처리합니다. */
        void completeCall( uint64 callID, const EconomyReply& reply );
        /** @brief 환불 회수 작업의 `complete` 가 호출합니다. 지표와 로그를 남기고 회수로 바뀐 잔액을 계정에 알립니다. 회수를 끝내지 못했으면 제공자가 그 환불을 잊어 겹친 조회가 다시 알립니다. */
        void  completeRefund( const ReceiptRefundNotice& notice, EconomyResult result, const vector<EconomyBalanceChange>& listChange, ServiceStoreCompletion completion );
        int32 getPendingCallCount() const { return static_cast<int32>( _listPendingCall.size() ); }
        /** @brief 기준 시각 읽기 작업의 `complete` 가 호출합니다. 읽었으면 제공자에 넘기고, 못 읽었으면 다시 읽게 합니다. */
        void completeRefundCursorRead( const vector<EconomyRefundCursor>& listCursor, ServiceStoreResult result, ServiceStoreCompletion completion );
        /** @brief 기준 시각 쓰기 작업의 `complete` 가 호출합니다. */
        void completeRefundCursorWrite( const EconomyRefundCursor& cursor, ServiceStoreResult result, ServiceStoreCompletion completion );

    private:
        /** @brief 잔액 변경 알림 창구입니다. 서비스의 `notifyBalancesChanged` 로 넘깁니다. 서비스가 멤버로 들어 수명이 같다. */
        class BalanceNotice final : public IBalanceNotice
        {
        public:
            explicit BalanceNotice( EconomyService& service )
                : _service{ service }
            {
            }

            void notifyBalancesChanged( AccountID accountID, const vector<LedgerBalance>& listBalance ) override { _service.notifyBalancesChanged( accountID, listBalance ); }

        private:
            EconomyService& _service;
        };

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
        /** @brief 기동 때 환불 조회 기준 시각을 읽어 넘기고, 그 뒤로는 옮겨진 기준을 저장소에 씁니다. */
        void updateRefundCursors( UnixMilliseconds nowMs );
        void onBalancePresenceFound( const AccountPresenceResult& result );

    private:
        vector<PendingCall>                          _listPendingCall;
        ServiceMetrics                               _metrics;
        EconomyServiceConfig                         _settings;
        Subscription                                 _findOwner;                  ///< 접속 상태 찾기의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        unordered_map<uint64, EconomyBalanceChanged> _mapPresenceRequestToNotice; ///< 접속 상태를 묻는 중인 잔액 변경 알림(받는 계정은 결과에 있다)
        BalanceNotice                                _balanceNotice;              ///< 잔액 변경 알림 창구(`getBalanceNotice`)
        OnlineServiceHost*                           _pHost;                      ///< `onServiceTick` 이 붙잡는다. 알림을 이 서버에 붙은 계정에 바로 보낸다
        IServiceStore*                               _pStore;
        MetricRegistry*                              _pMetricRegistry; ///< 환불 결과 지표를 센다(설정에서 복사)
        IDAllocator<uint64>                          _callIDAllocator;
        int32                                        _pendingRefundCount;
        int32                                        _pendingUpkeepCount;     ///< 응답 없는 저장소 작업(기준 시각 · 지급 확인) 수이다. 종료가 기다린다
        int64                                        _nextRefundCursorReadMs; ///< 읽지 못한 기준 시각을 다시 읽는 시각
        ScheduledRun                                 _auditRun;
        uint8                                        _bAuditPending;
        uint8                                        _bAuditRunHeld;
        uint8                                        _bLastAuditBalanced;
        uint8                                        _bRefundCursorsRestored;   ///< 기준 시각을 제공자에 넘겼다(넘길 제공자가 없어도 선다)
        uint8                                        _bRefundCursorReadPending; ///< 기준 시각을 읽는 중이다
        UnixMilliseconds                             _nowMs;
    };
} // namespace sw
