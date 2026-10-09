/**
 * @file TradeService.h
 * @brief 거래 서비스 — 상태 기계는 저장소 위(거래 레코드), 요청마다 저장 일 하나(읽기 → 상태 기계 → 판 조건 쓰기, 정산이면 원장 분개를 같은 커밋에). 서버 프로세스마다 하나.
 * @details - 서비스 스레드 하나에서 부른다. 결과는 꼬리표와 함께 `drainCompletions`, 바뀐 거래는 `drainUpdates`(바인딩이 두 당사자에게 알린다 — 다른 서버의 상대는 버스로).
 *          - 상태가 저장소에 있으므로 상대가 다른 서버에 붙어 있어도 상대 서버의 서비스가 같은 레코드를 판 조건으로 바꾼다(주인 서버를 거치지 않는다).
 *          - 기동 때 `recoverOwnedTrades` 가 이 서버가 연 열린 거래를 ServerRestart 로 닫는다(아무것도 안 움직였으니 안전).
 *          - 시한은 이 서비스가 본 거래의 마지막 바뀜 + 시한이 지나면 `tick` 이 닫기를 맡긴다(다른 서버가 연 것도 다음 신청 때 게으르게 닫힌다).
 *          자체 MMO 서버가 거래 확정을 DB 트랜잭션 하나(두 인벤토리 + 거래 기록)로 하는 것과 같은 모양 — 언리얼에는 거래 시스템이 없다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStoreLogic.h"

namespace sw
{
    class IServiceStore;

    /** @brief 거래 서비스의 작업 종류입니다. */
    enum class TradeOperation : uint8
    {
        Invite = 0,
        Respond,
        SetOffer,
        Lock,
        Confirm,
        Cancel,
        Expire,
        Leave,
        Recover
    };

    /** @brief 끝난 요청 하나입니다. */
    struct TradeCompletion
    {
        TradeSnapshot         _snapshot{};
        LedgerTransferOutcome _ledger{}; ///< 정산 — 이동 뒤 잔액(게임이 거울에 넣는다)
        uint64                _requestTag{ 0 };
        TradeResult           _result{ TradeResult::Ok };
        TradeOperation        _operation{ TradeOperation::Invite };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TradeService
     * @brief 거래 서비스입니다.
     */
    class SW_GF_API TradeService
    {
    public:
        TradeService();

        /** @brief 넘긴 것은 모두 빌려 쓴다. @p serverId 는 이 서버 프로세스의 id(0 아님 — 재시작 복구 색인). */
        void initialize( IServiceStore* pStore, const ITradePolicy* pPolicy, const ILedgerPolicy* pLedgerPolicy, uint64 serverId, const TradeSettings& settings );
        void shutdown();
        /** @brief 이 서버가 연 열린 거래를 모두 닫습니다(기동 때 한 번). 닫힌 거래는 `drainUpdates` 로. */
        void recoverOwnedTrades( int64 nowMs );

        void invite( AccountId fromId, AccountId toId, int64 nowMs, uint64 requestTag );
        void respondInvite( AccountId responderId, uint64 tradeId, bool bAccept, int64 nowMs, uint64 requestTag );
        void setOffer( AccountId actorId, uint64 tradeId, const vector<TradeLeg>& listLeg, int64 nowMs, uint64 requestTag );
        void lock( AccountId actorId, uint64 tradeId, int64 nowMs, uint64 requestTag );
        void confirm( AccountId actorId, uint64 tradeId, uint32 seenOwnRevision, uint32 seenPeerRevision, int64 nowMs, uint64 requestTag );
        void cancel( AccountId actorId, uint64 tradeId, int64 nowMs, uint64 requestTag );
        /** @brief 계정이 떠났다 · 제재됐다 — 그 계정의 열린 거래를 닫는다(완료 없음, 바뀜만). */
        void closeForAccount( AccountId accountId, TradeCloseReason reason, int64 nowMs );
        /** @brief 아는 거래의 시한을 봅니다. */
        void tick( int64 nowMs );

        void drainCompletions( vector<TradeCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainUpdates( vector<TradeSnapshot>& outListUpdate ) { _updateBuffer.drainTo( outListUpdate ); }

        int32                getPendingCount() const { return _pendingCount; }
        const TradeSettings& getSettings() const { return _settings; }
        /** @brief 일의 `complete` 가 부른다(키트 안). */
        void applyCompletion( TradeCompletion&& completion, const vector<TradeSnapshot>& listChanged );

    private:
        void submitCommand( TradeOperation operation, uint64 tradeId, const TradeCommand& command, int64 nowMs, uint64 requestTag );
        void trackTrade( const TradeSnapshot& snapshot );

        EventBuffer<TradeCompletion> _completionBuffer;
        EventBuffer<TradeSnapshot>   _updateBuffer;
        unordered_map<uint64, int64> _mapTradeToDeadline; ///< 이 서비스가 본 열린 거래 → 시한
        TradeSettings                _settings;
        IServiceStore*               _pStore;
        const ITradePolicy*          _pPolicy;
        const ILedgerPolicy*         _pLedgerPolicy;
        uint64                       _serverId;
        uint64                       _nextSeed;
        int32                        _pendingCount;
    };
} // namespace sw
