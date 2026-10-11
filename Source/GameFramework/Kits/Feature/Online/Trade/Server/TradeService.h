/**
 * @file TradeService.h
 * @brief 거래 서비스입니다. 상태 기계는 저장소 위의 거래 레코드에서 동작하고, 요청마다 저장 작업 하나를 만듭니다. 서버 프로세스마다 하나입니다.
 * @details 저장 작업은 읽기, 상태 기계, 버전 조건 쓰기 순서로 진행하며 정산이면 원장 분개를 같은 커밋에 넣는다.
 *          - 서비스 스레드 하나에서 부른다. 결과는 요청 태그와 함께 `drainCompletions` 로, 바뀐 거래는 `drainUpdates` 로 회수한다. 바인딩이 두 당사자에게 알리며 다른 서버의 상대에게는 버스로 알린다.
 *          - 상태가 저장소에 있으므로 상대가 다른 서버에 접속해 있어도 그 서버의 서비스가 같은 레코드를 버전 조건으로 바꾼다. 주인 서버를 거치지 않는다.
 *          - 기동할 때 `recoverOwnedTrades` 가 이 서버가 연 열린 거래를 ServerRestart 로 닫는다. 아무것도 움직이지 않았으므로 안전하다.
 *          - 시한은 이 서비스가 본 거래의 마지막 변경 시각에 시한을 더한 값이다. 지나면 `tick` 이 닫기를 맡긴다. 다른 서버가 연 거래도 다음 신청 때 지연해서 닫힌다.
 *
 *          자체 MMO 서버가 거래 확정을 DB 트랜잭션 하나(두 인벤토리와 거래 기록)로 처리하는 것과 같은 구조이다. 언리얼에는 거래 시스템이 없다.
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
        LedgerTransferOutcome _ledger{}; ///< 정산 결과이다. 이동 뒤 잔액이며 게임이 미러에 넣는다
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

        /** @brief 넘긴 객체는 모두 빌려 씁니다. @p serverID 는 이 서버 프로세스의 id 이며 0이 아니어야 합니다. 재시작 복구 인덱스에 씁니다. */
        void initialize( IServiceStore* pStore, const ITradePolicy* pPolicy, const ILedgerPolicy* pLedgerPolicy, uint64 serverID, const TradeSettings& settings );
        void shutdown();
        /** @brief 이 서버가 연 열린 거래를 모두 닫습니다. 기동할 때 한 번 부르며 닫힌 거래는 `drainUpdates` 로 회수합니다. */
        void recoverOwnedTrades( int64 nowMs );

        void invite( AccountID fromID, AccountID toID, int64 nowMs, uint64 requestTag );
        void respondInvite( AccountID responderID, uint64 tradeID, bool bAccept, int64 nowMs, uint64 requestTag );
        void setOffer( AccountID actorID, uint64 tradeID, const vector<TradeLeg>& listLeg, int64 nowMs, uint64 requestTag );
        void lock( AccountID actorID, uint64 tradeID, int64 nowMs, uint64 requestTag );
        void confirm( AccountID actorID, uint64 tradeID, uint32 seenOwnRevision, uint32 seenPeerRevision, int64 nowMs, uint64 requestTag );
        void cancel( AccountID actorID, uint64 tradeID, int64 nowMs, uint64 requestTag );
        /** @brief 계정이 떠나거나 제재됐을 때 부릅니다. 그 계정의 열린 거래를 닫으며 완료는 없고 변경만 발생합니다. */
        void closeForAccount( AccountID accountID, TradeCloseReason reason, int64 nowMs );
        /** @brief 아는 거래의 시한을 확인합니다. */
        void tick( int64 nowMs );

        void drainCompletions( vector<TradeCompletion>& outListCompletion ) { _completionBuffer.drainTo( outListCompletion ); }
        void drainUpdates( vector<TradeSnapshot>& outListUpdate ) { _updateBuffer.drainTo( outListUpdate ); }

        int32                getPendingCount() const { return _pendingCount; }
        const TradeSettings& getSettings() const { return _settings; }
        /** @brief 작업의 `complete` 가 호출합니다. 키트 안에서만 씁니다. */
        void applyCompletion( TradeCompletion&& completion, const vector<TradeSnapshot>& listChanged );

    private:
        void submitCommand( TradeOperation operation, uint64 tradeID, const TradeCommand& command, int64 nowMs, uint64 requestTag );
        void trackTrade( const TradeSnapshot& snapshot );

        EventBuffer<TradeCompletion> _completionBuffer;
        EventBuffer<TradeSnapshot>   _updateBuffer;
        unordered_map<uint64, int64> _mapTradeToDeadline; ///< 이 서비스가 본 열린 거래에서 시한으로 가는 맵이다
        TradeSettings                _settings;
        IServiceStore*               _pStore;
        const ITradePolicy*          _pPolicy;
        const ILedgerPolicy*         _pLedgerPolicy;
        uint64                       _serverID;
        uint64                       _nextSeed;
        int32                        _pendingCount;
    };
} // namespace sw
