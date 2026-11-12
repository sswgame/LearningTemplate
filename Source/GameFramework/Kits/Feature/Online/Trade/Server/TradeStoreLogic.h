/**
 * @file TradeStoreLogic.h
 * @brief 거래의 저장 부분입니다. 저장소 스레드에서 거래 레코드를 버전과 함께 읽고, 상태 기계로 새 레코드를 만들어 버전 조건으로 씁니다. 정산일 때만 원장 이동을 같은 트랜잭션에 붙입니다.
 * @details 저장소 테이블은 다음과 같다.
 *          - `trade_session`: 거래 id(16진)에서 형식 버전, 주인 서버, 스냅샷으로 가는 테이블이다.
 *          - `trade_active`: 계정(16진)에서 거래 id 로 가는 테이블이다. 계정마다 열린 거래는 하나이며 "없어야 함" 조건으로 넣으므로 서버가 여러 대여도 하나뿐이다.
 *          - `trade_owner`: `<주인 서버 16진>/<거래 16진>` 키이며 재시작 복구에 쓰는 인덱스이다.
 *          - `trade_session_expiry`: 닫힌 거래의 만료 색인 `<닫은 시각 16진>/<거래 16진>` 이다. 거래를 닫아 쓰는 트랜잭션(`stageSession`)이 함께 넣고,
 *            보존 규칙(`makeRetentionRule`, 기간은 `TradeKitConfig::_closedRetentionDays`)이 닫은 뒤 기간이 지난 거래 레코드를 지운다. 열린 거래는 지우지 않는다.
 *            지워도 다른 합 · 멱등이 깨지지 않는다: 정산 멱등은 원장 분개 `trade/<id>` 가 맡고(분개는 원장 보존 기간을 따른다), 남은 활성 링크는 다음 신청이
 *            "거래 없음" 으로 정리한다. 닫힌 거래의 확정 재시도는 기간 뒤에 NotFound 가 된다.
 *
 *          정산은 두 방향의 항목을 **한 분개**(키는 `trade/<거래 id 16진의 16진>`)로 `Ledger::stageTransfer` 에 넣고, 거래 레코드(Settled), 활성 링크 지우기, 감사 기록을 한 커밋으로 쓴다.
 *          잔액이 모자라면 원장 부분을 붙이지 않고 Failed 로 닫으며 아무것도 움직이지 않는다. 커밋이 Conflict 나 Unavailable 이면 분개로 확인한다. 응답이 유실됐거나 다른 서버가 먼저 처리한 경우이다.
 *          시한이 지난 거래와 닫혔는데 남은 활성 링크는 다음 신청이 정리한다(지연 만료). 서비스 틱도 아는 거래의 시한을 확인한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeTypes.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"

namespace sw
{
    struct ServiceRetentionRule;

    class IServiceStoreConnection;
    class ServiceTransaction;

    /** @brief 거래 서비스 설정입니다. 시간은 밀리초입니다. */
    struct TradeServiceConfig
    {
        int64 _idleTimeoutMs{ TradeConstant::kIdleTimeoutMs };
        int64 _inviteTimeoutMs{ TradeConstant::kInviteTimeoutMs };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TradeStoreLogic
     * @brief 거래 흐름의 저장 부분입니다. 작업 하나가 도는 동안만 사는 객체입니다.
     */
    class SW_KIT_SERVER_API TradeStoreLogic
    {
    public:
        TradeStoreLogic( IServiceStoreConnection& connection, const ITradePolicy& policy, const ILedgerPolicy* pLedgerPolicy, uint64 serverID, const TradeServiceConfig& settings );

        static const hashed_string& getSessionTable();
        static const hashed_string& getActiveTable();
        static const hashed_string& getOwnerTable();
        static const hashed_string& getSessionExpiryTable(); ///< "trade_session_expiry"

        /** @brief 닫힌 거래 레코드의 보존 규칙입니다(만료 색인 규칙). 키트 조립이 `IOnlineServerKit::addRetentionRule` 로 올립니다. */
        static ServiceRetentionRule makeRetentionRule( int64 retentionMs );

        /** @brief 거래를 엽니다(Invited). @p seed 로 거래 id 를 만들며 겹치면 다음 값을 씁니다. 양쪽 활성 링크는 "없어야 함" 조건이며, 닫혔거나 시한이 지난 이전 거래는 이때 정리합니다. */
        TradeServiceResult invite( AccountID fromID, AccountID toID, uint64 seed, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 명령 하나를 처리합니다. 둘 다 확정하면 같은 트랜잭션에서 정산합니다. 닫힌 거래의 확정을 재시도하면 그 결과를 돌려주며 Settled 이면 Ok 입니다. */
        TradeServiceResult applyCommand( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& outSnapshot, LedgerTransferResult& outLedger );
        /** @brief 시한이 지났으면 닫습니다(Timeout). 닫았으면 Ok 이고 아직이면 WrongState 입니다. */
        TradeServiceResult closeIfIdle( uint64 tradeID, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 계정의 열린 거래를 @p reason 이유로 닫습니다. 떠남과 제재가 해당하며 없으면 NotFound 입니다. */
        TradeServiceResult closeForAccount( AccountID accountID, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 이 서버가 주인인 열린 거래를 모두 닫습니다(ServerRestart). 아무것도 움직이지 않았으므로 안전합니다. 닫은 거래는 @p outListClosed 에 담습니다. */
        TradeServiceResult recoverOwned( int64 nowMs, vector<TradeSnapshot>& outListClosed );

        static bool isIdleExpired( const TradeSnapshot& trade, const TradeServiceConfig& settings, int64 nowMs );

    private:
        struct SessionRead
        {
            TradeSnapshot _snapshot{};
            uint64        _ownerServerID{ 0 };
            uint64        _version{ 0 };
        };

        /** @brief 거래를 닫아 커밋한 결과입니다. `_bRetry` 이면 상대가 그사이 바꿨으므로 다시 읽고 다시 시도합니다. 아니면 `_result` 가 Ok 일 때 닫은 것입니다. */
        struct CloseResult
        {
            TradeServiceResult _result{ TradeResult::Ok };
            bool               _bRetry{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        /** @brief 정산을 붙인 트랜잭션이 커밋에서 충돌했을 때 분개로 확인하려고 남기는 값입니다. */
        struct SettlementStage
        {
            LedgerTransferRequest _request{};
            bool                  _bStaged{ false }; ///< 이 시도가 분개를 새로 붙였으면 true 이다(이미 있던 분개를 되풀이한 것이면 false) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        TradeServiceResult readSession( uint64 tradeID, SessionRead& outSession );
        /** @brief 거래를 @p reason 이유로 Cancelled 로 닫아 커밋합니다. 닫은 스냅숏은 @p outClosed 에 담습니다. */
        CloseResult commitClose( const SessionRead& session, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outClosed );
        /**
         * @brief 둘 다 확정한 거래의 정산 분개를 @p inoutTransaction 에 붙이고 @p inoutNext 를 닫습니다(Settled 또는 Failed).
         * @details 정산할 항목이 없으면 원장을 건드리지 않고 Settled 로 닫습니다. 잔액이나 상한이 모자라면 원장 부분을 붙이지 않고 Failed 로 닫습니다. Ok 가 아니면 호출부가 그 결과로 끝냅니다.
         */
        TradeServiceResult stageSettlement( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& inoutNext, ServiceTransaction& inoutTransaction, SettlementStage& outStage,
                                            LedgerTransferResult& outLedger );
        /** @brief 닫힌 거래를 트랜잭션에 붙이고 정산 감사 기록을 남깁니다(취소는 남기지 않습니다). 레코드를 못 쓰면 false 입니다. */
        [[nodiscard]] bool stageClosedTrade( const SessionRead& session, const TradeSnapshot& closed, uint64 tradeID, int64 nowMs, ServiceTransaction& inoutTransaction );
        /** @brief 닫힌 레코드, 활성 링크 지우기, 주인 인덱스 지우기를 트랜잭션에 붙입니다. 레코드를 못 쓰면 false 이며 호출부는 요청을 Invalid 로 끝냅니다. */
        [[nodiscard]] bool stageClose( const SessionRead& session, const TradeSnapshot& closed, ServiceTransaction& inoutTransaction );
        /** @brief 잠그는 쪽의 항목을 원장 잔액과 비교합니다. 미리 확인하는 것이며 정산할 때 다시 확인합니다. */
        TradeServiceResult evaluateFunds( const TradeSide& side );

    private:
        IServiceStoreConnection&  _connection;
        const ITradePolicy&       _policy;
        const ILedgerPolicy*      _pLedgerPolicy;
        const TradeServiceConfig& _settings;
        uint64                    _serverID;
    };
} // namespace sw
