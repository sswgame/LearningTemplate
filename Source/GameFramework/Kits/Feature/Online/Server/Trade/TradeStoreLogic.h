/**
 * @file TradeStoreLogic.h
 * @brief 거래의 저장 부분 — 저장소 스레드에서 거래 레코드를 판과 함께 읽고, 상태 기계로 새 레코드를 만들어 판 조건으로 씁니다. 정산만 원장 이동을 같은 트랜잭션에 붙인다.
 * @details 저장소 표: `trade_session`(거래 id 16 진 → 형식 판 · 주인 서버 · 스냅숏) · `trade_active`(계정 16 진 → 거래 id — 계정마다 열린 거래 하나, "없어야 함" 으로
 *          넣어 서버 여럿이어도 하나) · `trade_owner`(`<주인 서버 16 진>/<거래 16 진>` — 재시작 복구 색인).
 *          - 정산 = 두 방향 다리를 **한 분개**(키 = `trade/<거래 id 16 진의 16 진>`)로 `Ledger::stageTransfer` + 거래 레코드(Settled) · 활성 링크 지움 · 감사 줄 — 한 커밋.
 *            모자라면 원장 몫을 붙이지 않고 Failed 로 닫는다(아무것도 안 움직인다). 커밋이 Conflict · Unavailable 이면 분개로 가린다(응답 유실 · 다른 서버가 먼저).
 *          - 시한이 지난 거래 · 닫혔는데 남은 활성 링크는 다음 신청이 정리한다(게으른 만료) — 서비스 틱도 아는 거래의 시한을 본다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Server/Trade/TradeStateMachine.h"
#include "GameFramework/Kits/Feature/Online/Trade/TradeTypes.h"

namespace sw
{
    class IServiceStoreConnection;
    class ServiceTransaction;

    /** @brief 거래 서비스 설정입니다. 시간은 밀리초입니다. */
    struct TradeSettings
    {
        int64 _idleTimeoutMs{ TradeConstant::kIdleTimeoutMs };
        int64 _inviteTimeoutMs{ TradeConstant::kInviteTimeoutMs };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TradeStoreLogic
     * @brief 거래 흐름의 저장 부분입니다(일 하나 동안 사는 객체).
     */
    class SW_GF_API TradeStoreLogic
    {
    public:
        TradeStoreLogic( IServiceStoreConnection& connection, const ITradePolicy& policy, const ILedgerPolicy* pLedgerPolicy, uint64 serverId, const TradeSettings& settings );

        static const hashed_string& getSessionTable();
        static const hashed_string& getActiveTable();
        static const hashed_string& getOwnerTable();

        /** @brief 거래를 엽니다(Invited). @p seed 로 거래 id 를 만든다(겹치면 다음 값). 양쪽 활성 링크가 "없어야 함" — 닫혔거나 시한이 지난 옛 거래는 이 자리에서 정리한다. */
        TradeResult invite( AccountId fromId, AccountId toId, uint64 seed, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 명령 하나 — 둘 다 확정이 되면 같은 트랜잭션에서 정산합니다. 닫힌 거래의 확정 재시도는 그 결과(Settled → Ok)를 돌려준다. */
        TradeResult applyCommand( uint64 tradeId, const TradeCommand& command, int64 nowMs, TradeSnapshot& outSnapshot, LedgerTransferOutcome& outLedger );
        /** @brief 시한이 지났으면 닫습니다(Timeout). 닫았으면 Ok, 아직이면 WrongState. */
        TradeResult closeIfIdle( uint64 tradeId, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 계정의 열린 거래를 @p reason 으로 닫습니다(떠남 · 제재). 없으면 NotFound. */
        TradeResult closeForAccount( AccountId accountId, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 이 서버가 주인인 열린 거래를 모두 닫습니다(ServerRestart — 아무것도 안 움직였으니 안전). 닫은 것을 @p outListClosed 에. */
        TradeResult recoverOwned( int64 nowMs, vector<TradeSnapshot>& outListClosed );

        static bool isIdleExpired( const TradeSnapshot& trade, const TradeSettings& settings, int64 nowMs );

    private:
        struct SessionRead
        {
            TradeSnapshot _snapshot{};
            uint64        _ownerServerId{ 0 };
            uint64        _version{ 0 };
        };

        TradeResult readSession( uint64 tradeId, SessionRead& outSession );
        /** @brief 닫힌 레코드 · 활성 링크 지움 · 주인 색인 지움을 붙입니다. */
        void stageClose( const SessionRead& session, const TradeSnapshot& closed, ServiceTransaction& inoutTransaction );
        /** @brief 잠그는 쪽의 다리를 원장 잔액과 견줍니다(미리 보기 — 정산 때 다시 본다). */
        TradeResult evaluateFunds( const TradeSide& side );

        IServiceStoreConnection& _connection;
        const ITradePolicy&      _policy;
        const ILedgerPolicy*     _pLedgerPolicy;
        const TradeSettings&     _settings;
        uint64                   _serverId;
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 레코드 코덱입니다. */
    struct SW_GF_API TradeRecordUtil
    {
        static vector<uint8>      encodeSession( const TradeSnapshot& snapshot, uint64 ownerServerId );
        [[nodiscard]] static bool decodeSession( const vector<uint8>& bytes, TradeSnapshot& outSnapshot, uint64& outOwnerServerId );
        /** @brief 정산 분개 키입니다. */
        [[nodiscard]] static bool makeJournalKey( uint64 tradeId, string& outKey );
    };
} // namespace sw
