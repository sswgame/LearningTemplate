/**
 * @file EconomyService.h
 * @brief 경제 서비스(서버) — 지갑 · 내역 · 구매 · 영수증 지급 요청을 저장소 일로 맡기고, 완료에서 답합니다. 서비스 스레드 하나에서 씁니다.
 * @details - 입구 둘: 서비스 틀(`onServiceRequest` — 호스트가 인증 · 도배 제한 · 판 협상을 이미 했다)과 `submitCall`(같은 프로세스의 서버 코드 · 시험). 둘 다 같은 일로 간다.
 *          - 저장소 완료는 호스트(`OnlineServiceHost::tick`)가 서비스 틱 전에 거둔다 — 호스트 없이 쓰면 부르는 쪽이 `IServiceStore::pollCompletions` 뒤 `tick`.
 *          - 구매는 멱등 키 필수(분개 키 = `acct.<계정>/<키>`). 영수증은 등록부에 맡기고 `tick` 이 결과를 거둬 지급 일을 맡긴다.
 *          - 원격 설정 `feature.shop_enabled`(기본 켬)가 꺼지면 구매 · 지급이 `kFeatureDisabled`(조회는 된다). Shipping 은 개발 전용 영수증 제공자를 거절한다.
 *          - 내리기(`shutdown`)는 맡긴 일이 다 끝날 때까지 저장소를 거둔다(상한 5 초) — 저장소를 내리기 **전에** 부른다.
 *          PlayFab Economy v2 의 `PurchaseInventoryItems` · `Redeem*` 과 같은 자리다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Economy/Protocol/EconomyProtocol.h"

namespace sw
{
    class CurrencyCatalog;
    class IServiceStore;
    class IServiceStoreWork;
    class MetricRegistry;
    class OfferCatalog;
    class ReceiptValidatorRegistry;

    /** @brief 서비스 설정 — 포인터는 모두 빌려 쓴다(서비스보다 오래 산다). */
    struct EconomyServiceSettings
    {
        const CurrencyCatalog*    _pCurrencyCatalog{ nullptr };
        const OfferCatalog*       _pOfferCatalog{ nullptr };
        ReceiptValidatorRegistry* _pReceiptRegistry{ nullptr }; ///< 없으면 RedeemReceipt 는 ReceiptInvalid
        MetricRegistry*           _pMetricRegistry{ nullptr };  ///< 없으면 세지 않는다
        uint8                     _bAcceptSandboxReceipts{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 전송과 무관한 요청 하나입니다(메서드에 맞는 칸만 채운다). */
    struct EconomyCall
    {
        EconomyPurchaseRequest _purchase{};
        EconomyRedeemRequest   _redeem{};
        EconomyHistoryRequest  _history{};
        NetIdempotencyKey      _idempotencyKey{};
        AccountId              _accountId{ kInvalidAccountId };
        int64                  _nowMs{ 0 }; ///< 0 이면 마지막 `tick` 의 시각
        uint16                 _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @class EconomyService @brief 경제 요청 처리기입니다. */
    class SW_GF_API EconomyService final : public IOnlineService
    {
    public:
        using ReplyDelegate = Delegate<void( const EconomyReply& )>;

        static constexpr const utf8* kFeatureFlag = "feature.shop_enabled";

        EconomyService();
        ~EconomyService() override;

        EconomyService( const EconomyService& )            = delete;
        EconomyService& operator=( const EconomyService& ) = delete;

        /** @brief 카탈로그 · 저장소가 없거나 Shipping 에 개발 전용 영수증 제공자가 있으면 false 입니다. 호스트에 올리는 것은 부르는 쪽(`host.registerService( &service )`). */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const EconomyServiceSettings& settings );
        void               shutdown();

        /** @brief 요청 하나를 맡깁니다(서비스 스레드). @p onReply 는 정확히 한 번 — 규칙 위반 거절은 바로, 나머지는 저장소 완료에서. */
        void submitCall( const EconomyCall& call, ReplyDelegate onReply );
        /** @brief 영수증 결과를 거둬 지급 일을 맡기고 지표를 갱신합니다(호스트가 있으면 `onServiceTick` 이 부른다). */
        void tick( int64 nowMs );

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kEconomy; }
        uint32 getProtocolVersion() const override { return EconomyProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;

        /** @brief 일(work)의 `complete` 가 부릅니다 — 응답 · 지표. */
        void  completeCall( uint64 callId, const EconomyReply& reply );
        int32 getPendingCallCount() const { return static_cast<int32>( _listPendingCall.size() ); }

    private:
        /** @brief 답을 기다리는 요청 하나 — 호스트 경로면 토큰으로, 아니면 델리게이트로 답한다. */
        struct PendingCall
        {
            ReplyDelegate      _onReply{};
            NetRequestToken    _token{};
            OnlineServiceHost* _pHost{ nullptr };
            uint64             _callId{ 0 };
            uint64             _receiptTicket{ 0 }; ///< 0 이 아니면 영수증 검증을 기다리는 중
            int64              _receivedNanoseconds{ 0 };
            AccountId          _accountId{ kInvalidAccountId };
            int32              _methodIndex{ 0 };
        };

        void startCall( const EconomyCall& call, PendingCall pending );
        void finishCall( size_t pendingIndex, const EconomyReply& reply );
        void pollReceipts();

        vector<PendingCall>    _listPendingCall;
        ServiceMetrics         _metrics;
        EconomyServiceSettings _settings;
        IServiceStore*         _pStore;
        uint64                 _nextCallId;
        int64                  _nowMs;
    };
} // namespace sw
