/**
 * @file EconomyClient.h
 * @brief 경제 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 지갑, 내역, 구매, 영수증 지급 요청을 보내고 잔액 캐시를 보관합니다.
 * @details - 성공 응답의 잔액을 캐시에 넣는다. 지갑 전체 응답은 스냅샷이고 구매와 지급 응답은 바뀐 것만 담는다. 다른 키트(거래, 우편, GM)의 응답에 든 잔액도 게임이 `applyBalances` 로 넘긴다.
 *          - **로컬 지갑은 원장의 읽기 사본이다.** 온라인 게임은 `Wallet::add`, `trySpend`, `charge` 를 부르지 않고 `applyLedgerBalances( wallet )` 로만 맞춘다. 서버가 원본이기 때문이다.
 *          - 구매와 지급을 재시도할 때는 **같은** 멱등 키를 쓴다. 응답의 `_idempotencyKey` 를 다시 넘기며, 새 키는 새 구매로 취급한다.
 *          - 콜백은 `OnlineServiceClient::tick` 스레드에서 정확히 한 번 불린다. 보내지 못하면 그 자리에서 Unavailable 로 끝난다.
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
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Protocol/EconomyProtocol.h"

namespace sw
{
    class Wallet;

    /** @brief 경제 클라이언트 응답 하나입니다. */
    struct EconomyClientReply
    {
        EconomyReply      _reply{};
        NetIdempotencyKey _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘기는 키이다
        uint64            _requestID{ 0 };
        uint16            _method{ 0 };
        uint16            _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다. 0이 아니면 `_reply._result` 는 그 코드에 대응하는 결과이다
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

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 부릅니다. */
        void initialize( OnlineServiceClient* pClient );

        /** @brief 이 클라이언트가 붙인 요청 번호를 반환합니다. 응답의 `_requestID` 와 같습니다. */
        uint64 requestWallet( ReplyDelegate onReply );
        uint64 requestHistory( const EconomyHistoryRequest& request, ReplyDelegate onReply );
        /** @brief @p key 가 비어 있으면 새로 만듭니다. 재시도할 때는 응답의 키를 다시 넘깁니다. */
        uint64 requestPurchase( const EconomyPurchaseRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );
        uint64 requestRedeem( const EconomyRedeemRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );

        /** @brief 잔액을 캐시에 넣습니다. 다른 키트 응답의 잔액도 게임이 이 함수로 넘깁니다. @p bSnapshot 이 true 이면 캐시를 통째로 바꿉니다. */
        void applyBalances( const vector<LedgerBalance>& listBalance, bool bSnapshot );
        /** @brief 캐시(원장 사본)로 @p inoutWallet 을 덮어씁니다. 캐시에 없는 통화는 0이 되고 빚은 음수 그대로 둡니다. 화면 이벤트는 차이만큼 발생합니다. */
        void applyLedgerBalances( Wallet& inoutWallet ) const;

        const vector<LedgerBalance>& getBalances() const { return _listBalance; }
        int64                        getBalance( string_view assetID ) const;
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
            uint64            _requestID{ 0 };
            uint16            _method{ 0 };
        };

        uint64 send( uint16 method, const BitWriter& body, const NetIdempotencyKey& key, ReplyDelegate onReply );
        void   onResponse( const OnlineResponse& response );

        ServiceClientCallTable<PendingCall> _callTable;
        vector<LedgerBalance>               _listBalance;
        OnlineServiceClient*                _pClient;
        uint64                              _nextRequestID;
        uint32                              _revision;
    };
} // namespace sw
