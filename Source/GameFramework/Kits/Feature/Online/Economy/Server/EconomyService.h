/**
 * @file EconomyService.h
 * @brief 경제 서비스(서버)입니다. 지갑, 내역, 구매, 영수증 지급 요청을 저장소 작업으로 맡기고 완료에서 응답합니다. 서비스 스레드 하나에서 씁니다.
 * @details - 요청이 들어오는 경로는 둘이다. 서비스 프레임워크(`onServiceRequest`)는 호스트가 인증, 도배 제한, 버전 협상을 이미 마친 요청을 넘긴다. `submitCall` 은 같은 프로세스의 서버 코드와 테스트가 쓴다. 둘 다 같은 작업으로 이어진다.
 *          - 저장소 완료는 호스트(`OnlineServiceHost::tick`)가 서비스 틱 전에 회수한다. 호스트 없이 쓰면 호출하는 쪽이 `IServiceStore::pollCompletions` 를 부른 뒤 `tick` 을 부른다.
 *          - 구매에는 멱등 키가 필수이다. 분개 키는 `acct.<계정>/<키>` 이다. 영수증은 레지스트리에 맡기고, `tick` 이 결과를 회수해 지급 작업을 맡긴다.
 *          - 원격 설정 `feature.shop_enabled`(기본값은 켬)가 꺼지면 구매와 지급은 `kFeatureDisabled` 로 끝나고 조회는 된다. Shipping 빌드는 개발 전용 영수증 제공자를 거절한다.
 *          - `shutdown` 은 맡긴 작업이 모두 끝날 때까지 저장소를 회수한다(상한 5초). 저장소를 종료하기 **전에** 부른다.
 *
 *          PlayFab Economy v2 의 `PurchaseInventoryItems` 와 `Redeem*` 에 해당한다.
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
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Protocol/EconomyProtocol.h"

namespace sw
{
    class CurrencyCatalog;
    class IServiceStore;
    class IServiceStoreWork;
    class MetricRegistry;
    class OfferCatalog;
    class ReceiptValidatorRegistry;

    /** @brief 서비스 설정입니다. 포인터는 모두 빌려 쓰며 서비스보다 오래 살아야 합니다. */
    struct EconomyServiceSettings
    {
        const CurrencyCatalog*    _pCurrencyCatalog{ nullptr };
        const OfferCatalog*       _pOfferCatalog{ nullptr };
        ReceiptValidatorRegistry* _pReceiptRegistry{ nullptr }; ///< 없으면 RedeemReceipt 는 ReceiptInvalid 로 끝난다
        MetricRegistry*           _pMetricRegistry{ nullptr };  ///< 없으면 지표를 세지 않는다
        uint8                     _bAcceptSandboxReceipts{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 전송과 무관한 요청 하나입니다. 메서드에 맞는 필드만 채웁니다. */
    struct EconomyCall
    {
        EconomyPurchaseRequest _purchase{};
        EconomyRedeemRequest   _redeem{};
        EconomyHistoryRequest  _history{};
        NetIdempotencyKey      _idempotencyKey{};
        AccountID              _accountID{ kInvalidAccountID };
        int64                  _nowMs{ 0 }; ///< 0이면 마지막 `tick` 의 시각을 쓴다
        uint16                 _method{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @class EconomyService @brief 경제 요청 처리기입니다. */
    class SW_KIT_API EconomyService final : public IOnlineHostService
    {
    public:
        using ReplyDelegate = Delegate<void( const EconomyReply& )>;

        static constexpr const utf8* kFeatureFlag = "feature.shop_enabled";

        EconomyService();
        ~EconomyService() override;

        EconomyService( const EconomyService& )            = delete;
        EconomyService& operator=( const EconomyService& ) = delete;

        /** @brief 카탈로그나 저장소가 없거나 Shipping 에 개발 전용 영수증 제공자가 있으면 false 를 반환합니다. 호스트에 등록하는 일은 호출하는 쪽이 합니다(`host.registerHostService( &service )`). */
        [[nodiscard]] bool initialize( IServiceStore* pStore, const EconomyServiceSettings& settings );
        void               shutdown();

        /** @brief 요청 하나를 맡깁니다. 서비스 스레드에서 부릅니다. @p onReply 는 정확히 한 번 불리며, 규칙 위반 거절은 바로, 나머지는 저장소 완료에서 불립니다. */
        void submitCall( const EconomyCall& call, ReplyDelegate onReply );
        /** @brief 영수증 결과를 회수해 지급 작업을 맡기고 지표를 갱신합니다. 호스트가 있으면 `onServiceTick` 이 부릅니다. */
        void tick( int64 nowMs );

        // IOnlineHostService
        uint16 getMethodRange() const override { return OnlineMethodRange::kEconomy; }
        uint32 getProtocolVersion() const override { return EconomyProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;

        /** @brief 작업(work)의 `complete` 가 호출합니다. 응답과 지표를 처리합니다. */
        void  completeCall( uint64 callID, const EconomyReply& reply );
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

        vector<PendingCall>    _listPendingCall;
        ServiceMetrics         _metrics;
        EconomyServiceSettings _settings;
        IServiceStore*         _pStore;
        uint64                 _nextCallID;
        int64                  _nowMs;
    };
} // namespace sw
