/**
 * @file EconomyClient.h
 * @brief 경제 키트의 클라이언트 — `OnlineServiceClient` 에 올리는 `IOnlineClientService`. 지갑 · 내역 · 구매 · 영수증 지급을 보내고 잔액 캐시를 듭니다.
 * @details - 성공 응답의 잔액을 캐시에 넣는다(지갑 전체는 스냅숏, 구매 · 지급은 바뀐 것만). 다른 키트(거래 · 우편 · GM) 응답의 잔액도 게임이 `applyBalances` 로 넘긴다.
 *          - **로컬 지갑은 원장의 읽기 사본이다**: 온라인 게임은 `Wallet::add` · `trySpend` · `charge` 를 부르지 않고 `applyLedgerBalances( wallet )` 로만 맞춘다(서버가 정본).
 *          - 구매 · 지급 재시도는 **같은** 멱등 키로(응답의 `_idempotencyKey` 를 다시 넘긴다 — 새 키는 새 구매다).
 *          - 콜백은 `OnlineServiceClient::tick` 스레드에서 정확히 한 번 온다(보내지 못하면 그 자리에서 Unavailable).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Economy/EconomyProtocol.h"

namespace sw
{
    class Wallet;

    /** @brief 경제 클라이언트 응답 하나입니다. */
    struct EconomyClientReply
    {
        EconomyReply      _reply{};
        NetIdempotencyKey _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘긴다
        uint64            _requestId{ 0 };
        uint16            _method{ 0 };
        uint16            _errorCode{ 0 }; ///< 전송 · 공통 오류(`OnlineError`) — 0 이 아니면 `_reply._result` 는 그 코드의 결과
    };
} // namespace sw

namespace sw
{
    /**
     * @class EconomyClient
     * @brief 경제 클라이언트입니다.
     */
    class SW_GF_API EconomyClient final : public IOnlineClientService
    {
    public:
        using ReplyDelegate = Delegate<void( const EconomyClientReply& )>;

        EconomyClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이. */
        void initialize( OnlineServiceClient* pClient );

        /** @brief 우리 요청 번호입니다(응답의 `_requestId`). */
        uint64 requestWallet( ReplyDelegate onReply );
        uint64 requestHistory( const EconomyHistoryRequest& request, ReplyDelegate onReply );
        /** @brief @p key 가 비면 새로 만든다 — 재시도는 응답의 키를 다시 넘긴다. */
        uint64 requestPurchase( const EconomyPurchaseRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );
        uint64 requestRedeem( const EconomyRedeemRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );

        /** @brief 잔액을 캐시에 넣습니다(다른 키트 응답의 잔액도 게임이 이리로). @p bSnapshot 이면 캐시를 통째로 바꾼다. */
        void applyBalances( const vector<LedgerBalance>& listBalance, bool bSnapshot );
        /** @brief 캐시(원장 사본)로 @p inoutWallet 을 덮어씁니다 — 캐시에 없는 통화는 0, 빚은 음수 그대로. 화면 사건은 차이만큼 난다. */
        void applyLedgerBalances( Wallet& inoutWallet ) const;

        const vector<LedgerBalance>& getBalances() const { return _listBalance; }
        int64                        getBalance( string_view assetId ) const;
        uint32                       getRevision() const { return _revision; }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kEconomy; }
        uint32 getProtocolVersion() const override { return EconomyProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            ReplyDelegate     _onReply{};
            NetIdempotencyKey _key{};
            uint64            _requestId{ 0 };
            uint16            _method{ 0 };
        };

        uint64 send( uint16 method, const BitWriter& body, const NetIdempotencyKey& key, ReplyDelegate onReply );
        void   onResponse( const OnlineResponse& response );

        unordered_map<uint64, PendingCall> _mapClientIdToCall;
        vector<LedgerBalance>              _listBalance;
        PendingCall                        _sendingCall;
        OnlineServiceClient*               _pClient;
        uint64                             _nextRequestId;
        uint32                             _revision;
        uint8                              _bSending;
    };
} // namespace sw
