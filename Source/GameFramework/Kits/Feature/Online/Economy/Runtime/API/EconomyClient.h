/**
 * @file EconomyClient.h
 * @brief 경제 키트의 클라이언트입니다. 잔액 캐시를 보관합니다.
 * @details - 성공 응답의 잔액을 캐시에 넣는다. 지갑 전체 응답은 스냅샷이고 구매와 지급 응답은 바뀐 것만 담는다. 다른 키트(거래, 우편, GM)의 응답에 포함된 잔액도 게임이 `applyBalances` 로 넘긴다.
 *          - **로컬 지갑은 원장의 읽기 사본이다.** 온라인 게임은 `Wallet::add`, `trySpend`, `charge` 를 부르지 않고 `applyLedgerBalances( wallet )` 로만 맞춘다. 서버가 원본이기 때문이다.
 *          - 요청 없이 바뀐 잔액(GM 지급 · 회수, 환불 회수)은 서버가 알림(`EconomyMethod::BalanceChanged`)으로 보내며, 받으면 캐시를 그 값으로 덮는다.
 *          - 구매와 지급을 재시도할 때는 **같은** 멱등 키를 쓴다. 응답의 `_idempotencyKey` 를 다시 넘기며, 새 키는 새 구매로 취급한다.
 *          - 요청은 `call<EconomyMethod::Purchase>( request, onReply, key )` 이고 규칙은 `OnlineProtocolClient` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/EconomyProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    class Wallet;

    /**
     * @class EconomyClient
     * @brief 경제 클라이언트입니다.
     */
    class SW_KIT_API EconomyClient final : public OnlineProtocolClient<EconomyClient, EconomyProtocol>
    {
    public:
        using Reply         = OnlineResult<EconomyReply>;
        using ReplyDelegate = Delegate<void( const Reply& )>;

        EconomyClient();

        /** @brief 잔액을 캐시에 넣습니다. 다른 키트 응답의 잔액도 게임이 이 함수로 넘깁니다. @p bSnapshot 이 true 이면 캐시를 통째로 바꿉니다. */
        void applyBalances( const vector<LedgerBalance>& listBalance, bool bSnapshot );
        /** @brief 캐시(원장 사본)로 @p inoutWallet 을 덮어씁니다. 캐시에 없는 통화는 0이 되고 빚은 음수 그대로 둡니다. 화면 이벤트는 차이만큼 발생합니다. */
        void applyLedgerBalances( Wallet& inoutWallet ) const;

        const vector<LedgerBalance>& getBalances() const { return _listBalance; }
        int64                        getBalance( string_view assetID ) const;
        uint32                       getRevision() const { return _revision; }

        // OnlineProtocolClient 의 훅 - 잔액을 싣는 메서드만 캐시를 고친다
        using OnlineProtocolClient::onCallSucceeded;
        void onCallSucceeded( EconomyMethod::GetWallet, const OnlineNoBody& request, const EconomyReply& reply );
        void onCallSucceeded( EconomyMethod::Purchase, const EconomyPurchaseRequest& request, const EconomyReply& reply );
        void onCallSucceeded( EconomyMethod::RedeemReceipt, const EconomyRedeemRequest& request, const EconomyReply& reply );
        void onCallSucceeded( EconomyMethod::WithdrawPurchase, const EconomyWithdrawRequest& request, const EconomyReply& reply );
        /** @brief 잔액 변경 알림을 받으면 바뀐 잔액을 캐시에 넣습니다. */
        void onPush( EconomyMethod::BalanceChanged, const EconomyBalanceChanged& body );

    private:
        vector<LedgerBalance> _listBalance;
        uint32                _revision;
    };
} // namespace sw
