/**
 * @file TradeClient.h
 * @brief 거래 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 신청, 수락, 제시, 잠금, 확정, 취소를 보내고(`OnlineProtocolClient`) 응답과 알림(스냅샷)을 꺼냅니다.
 * @details - 모든 요청에 멱등 키를 싣는다. 같은 버튼을 두 번 누르거나 다시 연결한 뒤 재시도해도 두 번 적용되지 않는다. 정산은 분개 키로도 막힌다. 재시도할 때는 응답에 실린 키를 다시 넘긴다.
 *          - 화면이 비추는 스냅샷 하나(`getSnapshot`)를 보관한다. 확정은 그 스냅샷의 두 버전을 싣는다. 내가 본 것만 확정한다는 뜻이다.
 *          - 정산 잔액(`TradeBalance`)은 서버가 경제 키트의 잔액 변경 알림으로도 보내 지갑이 스스로 맞는다. 인벤토리 미러는 게임이 넣는다. 원장이 소유의 원본이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeSessionRecord.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

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
        uint16               _errorCode{ 0 }; ///< 전송 오류나 공통 범주(`OnlineError`)이다. 0이 아니면 `_result` 는 뜻이 없다
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
        uint16               _kind{ 0 }; ///< `TradeMethod` 의 `Invited`, `Update`, `Closed` 번호(`kKind`) 중 하나이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class TradeClient
     * @brief 거래 클라이언트입니다.
     */
    class SW_KIT_API TradeClient final : public OnlineProtocolClient<TradeClient, TradeSessionRecord>
    {
    public:
        /**
         * @brief 거래 화면이 인벤토리 슬롯 @p listSlot 에서 거래 항목을 만듭니다. 슬롯을 (원장 자산 id, 수량)으로 바꾸며 같은 아이템의 슬롯은 한 항목으로 합칩니다.
         * @details 빈 슬롯 · 범위 밖 · 같은 슬롯 두 번은 공통 범주 `kInvalidRequest` 이고, 인스턴스 상태가 있는 슬롯(꾸미기, 떨어진 부품, 외형 피해, 내구도)이나 바꿀 수 없는 아이템은 NotTradable 입니다.
         *          인벤토리 슬롯에는 서버의 인스턴스 아이템 id 가 없기 때문이다. 인스턴스 아이템(`LedgerItemInstance`)은 게임이 그 자산 id 와 양 1 로 다리를 직접 만든다.
         */
        static TradeServiceResult makeLegs( const Inventory& inventory, const vector<int32>& listSlot, AssetIDFromItem pAssetIDFromItem, vector<TradeLeg>& outListLeg );

        TradeClient();

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

        // OnlineProtocolClient 의 훅
        void onPush( TradeMethod::Invited, const TradeNotification& body );
        void onPush( TradeMethod::Update, const TradeNotification& body );
        void onPush( TradeMethod::Closed, const TradeNotification& body );

    private:
        /** @brief 메서드 @p TMethod 를 보냅니다. 응답은 `pollReplies` 로 꺼냅니다. 이 클래스의 `.cpp` 안에서만 부릅니다. */
        template <typename TMethod>
        uint64 send( const typename TMethod::Request& request, const NetIdempotencyKey& key );
        void   onReply( const OnlineResult<TradeReply>& result );
        void   recordUpdate( uint16 kind, const TradeNotification& body );
        void   observe( const TradeSnapshot& snapshot );

    private:
        vector<TradeClientReply>  _listReply;
        vector<TradeClientUpdate> _listUpdate;
        TradeSnapshot             _snapshot;
        AccountID                 _accountID;
    };
} // namespace sw
