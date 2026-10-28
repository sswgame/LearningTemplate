/**
 * @file TradeClient.h
 * @brief 거래 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 신청, 수락, 제시, 잠금, 확정, 취소를 보내고 응답과 알림(스냅샷)을 꺼냅니다.
 * @details - 모든 요청에 멱등 키를 싣는다. 같은 버튼을 두 번 누르거나 다시 연결한 뒤 재시도해도 두 번 적용되지 않는다. 정산은 분개 키로도 막힌다. 재시도할 때는 응답에 실린 키를 다시 넘긴다.
 *          - 화면이 비추는 스냅샷 하나(`getSnapshot`)를 보관한다. 확정은 그 스냅샷의 두 버전을 싣는다. 내가 본 것만 확정한다는 뜻이다.
 *          - 정산 잔액(`TradeBalance`)은 게임이 지갑과 인벤토리 미러에 넣는다. 키트끼리는 서로 모르며 원장이 소유의 원본이다. 사용자가 정한 정책이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeProtocol.h"

namespace sw
{
    class Inventory;

    /** @brief 아이템 id 를 원장 자산 id 로 바꿉니다. 거래할 수 없는 아이템이면 false 를 반환합니다. 게임이 경제 키트의 카탈로그로 연결합니다(키트끼리는 서로 모른다). */
    using AssetIDFromItem = bool ( * )( const hashed_string& itemID, string& outAssetID );
    /** @brief 거래 클라이언트 응답 하나입니다. */
    struct TradeClientReply
    {
        TradeSnapshot        _snapshot{};
        vector<TradeBalance> _listBalance{};
        NetIdempotencyKey    _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘기는 키이다
        uint64               _requestID{ 0 };
        uint16               _method{ 0 };
        uint16               _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다. 0이 아니면 `_result` 는 Unavailable 이나 Invalid 이다
        TradeResult          _result{ TradeResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 알림 하나입니다. */
    struct TradeClientUpdate
    {
        TradeSnapshot        _snapshot{};
        vector<TradeBalance> _listBalance{};
        uint16               _kind{ 0 }; ///< kPushInvited, kPushUpdate, kPushClosed 중 하나이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class TradeClient
     * @brief 거래 클라이언트입니다.
     */
    class SW_KIT_API TradeClient final : public IOnlineClientService
    {
    public:
        /**
         * @brief 거래 화면이 인벤토리 슬롯 @p listSlot 에서 거래 항목을 만듭니다. 슬롯을 (원장 자산 id, 수량)으로 바꾸며 같은 아이템의 슬롯은 한 항목으로 합칩니다.
         * @details 빈 슬롯 · 범위 밖 · 같은 슬롯 두 번은 Invalid 이고, 인스턴스 상태가 있는 슬롯(꾸미기, 떨어진 부품, 외형 피해, 내구도)이나 바꿀 수 없는 아이템은 NotTradable 입니다.
         *          인벤토리 슬롯에는 서버의 인스턴스 아이템 id 가 없기 때문이다. 인스턴스 아이템(`LedgerItemInstance`)은 게임이 그 자산 id 와 양 1 로 다리를 직접 만든다.
         */
        static TradeResult makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIDFromItem pAssetIDFromItem, vector<TradeLeg>& outListLeg );

        TradeClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 부릅니다. */
        void initialize( OnlineServiceClient* pClient );

        uint64 invite( string_view peerDisplayName, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        uint64 respond( uint64 tradeID, bool bAccept, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        uint64 setOffer( uint64 tradeID, const vector<TradeLeg>& listLeg, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        uint64 lock( uint64 tradeID, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        /** @brief 지금 비추는 스냅샷의 (내 버전, 상대 버전)으로 확정합니다. */
        uint64 confirm( uint64 tradeID, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        /** @brief 버전을 직접 지정해 확정합니다. 재시도할 때 처음 본 버전 그대로 씁니다. */
        uint64 confirmSeen( uint64 tradeID, uint32 seenOwnRevision, uint32 seenPeerRevision, const NetIdempotencyKey& key );
        uint64 cancel( uint64 tradeID, const NetIdempotencyKey& key = NetIdempotencyKey{} );

        int32 pollReplies( vector<TradeClientReply>& outListReply );
        int32 pollUpdates( vector<TradeClientUpdate>& outListUpdate );

        /** @brief 내 계정 id 입니다. 스냅샷에서 내 쪽을 찾는 데 쓰며 로그인한 뒤 게임이 넣습니다. */
        void                 setAccountID( AccountID accountID ) { _accountID = accountID; }
        const TradeSnapshot& getSnapshot() const { return _snapshot; }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return TradeProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            NetIdempotencyKey _key{};
            uint64            _requestID{ 0 };
            uint16            _method{ 0 };
        };

        /** @brief @p request 를 써서 보냅니다. 범위 밖 값이 있으면 보내지 않고 `kInvalidRequest` 응답을 바로 남깁니다. 이 클래스의 `.cpp` 안에서만 부릅니다. */
        template <typename TRequest>
        uint64 send( uint16 method, const TRequest& request, const NetIdempotencyKey& key );
        void   onResponse( const OnlineResponse& response );
        void   observe( const TradeSnapshot& snapshot );

        ServiceClientCallTable<PendingCall> _callTable;
        vector<TradeClientReply>            _listReply;
        vector<TradeClientUpdate>           _listUpdate;
        TradeSnapshot                       _snapshot;
        OnlineServiceClient*                _pClient;
        AccountID                           _accountID;
        IDAllocator<uint64>                 _requestIDAllocator;
    };
} // namespace sw
