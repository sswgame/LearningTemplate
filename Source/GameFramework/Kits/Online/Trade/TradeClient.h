/**
 * @file TradeClient.h
 * @brief 거래 키트의 클라이언트 — `OnlineServiceClient` 에 올리는 `IOnlineClientService`. 신청 · 수락 · 제시 · 잠금 · 확정 · 취소를 보내고 응답 · 알림(스냅숏)을 꺼냅니다.
 * @details - 모든 요청에 멱등 키 — 같은 버튼 두 번 · 다시 연결한 뒤의 재시도가 두 번 적용되지 않는다(정산은 분개 키로도 막힌다). 재시도는 응답에 실린 키를 다시 넘긴다.
 *          - 화면이 비추는 스냅숏 하나(`getSnapshot`)를 든다 — 확정은 그 스냅숏의 두 판을 싣는다(내가 본 것만 확정).
 *          - 정산 잔액(`TradeBalance`)은 게임이 지갑 · 인벤토리 거울에 넣는다(키트끼리 모른다 — 원장이 소유의 정본, 사용자 결정 5).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Online/Trade/TradeProtocol.h"

namespace sw
{
    /** @brief 거래 클라이언트 응답 하나입니다. */
    struct TradeClientReply
    {
        TradeSnapshot        _snapshot{};
        vector<TradeBalance> _listBalance{};
        NetIdempotencyKey    _idempotencyKey{}; ///< 재시도할 때 그대로 다시 넘긴다
        uint64               _requestId{ 0 };
        uint16               _method{ 0 };
        uint16               _errorCode{ 0 }; ///< 전송 · 공통 오류(`OnlineError`) — 0 이 아니면 `_result` 는 Unavailable 또는 Invalid
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
        uint16               _kind{ 0 }; ///< kPushInvited · kPushUpdate · kPushClosed
    };
} // namespace sw

namespace sw
{
    /**
     * @class TradeClient
     * @brief 거래 클라이언트입니다.
     */
    class SW_GF_API TradeClient final : public IOnlineClientService
    {
    public:
        TradeClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이. */
        void initialize( OnlineServiceClient* pClient );

        uint64 invite( string_view peerDisplayName, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        uint64 respond( uint64 tradeId, bool bAccept, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        uint64 setOffer( uint64 tradeId, const vector<TradeLeg>& listLeg, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        uint64 lock( uint64 tradeId, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        /** @brief 지금 비추는 스냅숏의 (내 판, 상대 판)으로 확정합니다. */
        uint64 confirm( uint64 tradeId, const NetIdempotencyKey& key = NetIdempotencyKey{} );
        /** @brief 판을 직접 주어 확정합니다(재시도 — 처음 본 판 그대로). */
        uint64 confirmSeen( uint64 tradeId, uint32 seenOwnRevision, uint32 seenPeerRevision, const NetIdempotencyKey& key );
        uint64 cancel( uint64 tradeId, const NetIdempotencyKey& key = NetIdempotencyKey{} );

        int32 pollReplies( vector<TradeClientReply>& outListReply );
        int32 pollUpdates( vector<TradeClientUpdate>& outListUpdate );

        /** @brief 내 계정 id — 스냅숏에서 내 쪽을 찾는다(로그인한 뒤 게임이 넣는다). */
        void                 setAccountId( AccountId accountId ) { _accountId = accountId; }
        const TradeSnapshot& getSnapshot() const { return _snapshot; }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kTrade; }
        uint32 getProtocolVersion() const override { return TradeProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            NetIdempotencyKey _key{};
            uint64            _requestId{ 0 };
            uint16            _method{ 0 };
        };

        uint64 send( uint16 method, const BitWriter& body, const NetIdempotencyKey& key );
        void   onResponse( const OnlineResponse& response );
        void   observe( const TradeSnapshot& snapshot );

        unordered_map<uint64, PendingCall> _mapClientIdToCall;
        vector<TradeClientReply>           _listReply;
        vector<TradeClientUpdate>          _listUpdate;
        TradeSnapshot                      _snapshot;
        PendingCall                        _sendingCall;
        OnlineServiceClient*               _pClient;
        AccountId                          _accountId;
        uint64                             _nextRequestId;
        uint8                              _bSending;
    };
} // namespace sw
