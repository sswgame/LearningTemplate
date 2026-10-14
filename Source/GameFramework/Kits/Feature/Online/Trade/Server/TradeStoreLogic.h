/**
 * @file TradeStoreLogic.h
 * @brief 거래의 저장 부분입니다. 저장소 스레드에서 거래 레코드를 버전과 함께 읽고, 상태 기계로 새 레코드를 만들어 버전 조건으로 씁니다. 정산일 때만 원장 이동을 같은 트랜잭션에 붙입니다.
 * @details 저장소 테이블은 다음과 같다.
 *          - `trade_session`: 거래 id(16진)에서 형식 버전, 주인 서버, 스냅샷으로 가는 테이블이다.
 *          - `trade_active`: 계정(16진)에서 거래 id 로 가는 테이블이다. 계정마다 열린 거래는 하나이며 "없어야 함" 조건으로 넣으므로 서버가 여러 대여도 하나뿐이다.
 *          - `trade_owner`: `<주인 서버 16진>/<거래 16진>` 키이며 재시작 복구에 쓰는 인덱스이다.
 *
 *          정산은 두 방향의 항목을 **한 분개**(키는 `trade/<거래 id 16진의 16진>`)로 `Ledger::stageTransfer` 에 넣고, 거래 레코드(Settled), 활성 링크 지우기, 감사 기록을 한 커밋으로 쓴다.
 *          잔액이 모자라면 원장 부분을 붙이지 않고 Failed 로 닫으며 아무것도 움직이지 않는다. 커밋이 Conflict 나 Unavailable 이면 분개로 확인한다. 응답이 유실됐거나 다른 서버가 먼저 처리한 경우이다.
 *          시한이 지난 거래와 닫혔는데 남은 활성 링크는 다음 신청이 정리한다(지연 만료). 서비스 틱도 아는 거래의 시한을 확인한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

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
     * @brief 거래 흐름의 저장 부분입니다. 작업 하나가 도는 동안만 사는 객체입니다.
     */
    class SW_KIT_SERVER_API TradeStoreLogic
    {
    public:
        TradeStoreLogic( IServiceStoreConnection& connection, const ITradePolicy& policy, const ILedgerPolicy* pLedgerPolicy, uint64 serverID, const TradeSettings& settings );

        static const hashed_string& getSessionTable();
        static const hashed_string& getActiveTable();
        static const hashed_string& getOwnerTable();

        /** @brief 거래를 엽니다(Invited). @p seed 로 거래 id 를 만들며 겹치면 다음 값을 씁니다. 양쪽 활성 링크는 "없어야 함" 조건이며, 닫혔거나 시한이 지난 이전 거래는 이때 정리합니다. */
        TradeResult invite( AccountID fromID, AccountID toID, uint64 seed, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 명령 하나를 처리합니다. 둘 다 확정하면 같은 트랜잭션에서 정산합니다. 닫힌 거래의 확정을 재시도하면 그 결과를 돌려주며 Settled 이면 Ok 입니다. */
        TradeResult applyCommand( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& outSnapshot, LedgerTransferOutcome& outLedger );
        /** @brief 시한이 지났으면 닫습니다(Timeout). 닫았으면 Ok 이고 아직이면 WrongState 입니다. */
        TradeResult closeIfIdle( uint64 tradeID, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 계정의 열린 거래를 @p reason 이유로 닫습니다. 떠남과 제재가 해당하며 없으면 NotFound 입니다. */
        TradeResult closeForAccount( AccountID accountID, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outSnapshot );
        /** @brief 이 서버가 주인인 열린 거래를 모두 닫습니다(ServerRestart). 아무것도 움직이지 않았으므로 안전합니다. 닫은 거래는 @p outListClosed 에 담습니다. */
        TradeResult recoverOwned( int64 nowMs, vector<TradeSnapshot>& outListClosed );

        static bool isIdleExpired( const TradeSnapshot& trade, const TradeSettings& settings, int64 nowMs );

    private:
        struct SessionRead
        {
            TradeSnapshot _snapshot{};
            uint64        _ownerServerID{ 0 };
            uint64        _version{ 0 };
        };

        TradeResult readSession( uint64 tradeID, SessionRead& outSession );
        /** @brief 닫힌 레코드, 활성 링크 지우기, 주인 인덱스 지우기를 트랜잭션에 붙입니다. */
        void stageClose( const SessionRead& session, const TradeSnapshot& closed, ServiceTransaction& inoutTransaction );
        /** @brief 잠그는 쪽의 항목을 원장 잔액과 비교합니다. 미리 확인하는 것이며 정산할 때 다시 확인합니다. */
        TradeResult evaluateFunds( const TradeSide& side );

        IServiceStoreConnection& _connection;
        const ITradePolicy&      _policy;
        const ILedgerPolicy*     _pLedgerPolicy;
        const TradeSettings&     _settings;
        uint64                   _serverID;
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 레코드 코덱입니다. */
    struct SW_KIT_SERVER_API TradeRecordUtil
    {
        static vector<uint8>      encodeSession( const TradeSnapshot& snapshot, uint64 ownerServerID );
        [[nodiscard]] static bool decodeSession( const vector<uint8>& bytes, TradeSnapshot& outSnapshot, uint64& outOwnerServerID );
        /** @brief 정산 분개 키를 만듭니다. */
        [[nodiscard]] static bool makeJournalKey( uint64 tradeID, string& outKey );
    };
} // namespace sw
