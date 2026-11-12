#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStoreLogic.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeSessionRecord.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRetentionRule.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct TradeStoreLogicInternal
        {
            static constexpr uint64 kSettleAuditUnique = 0x5E771Eull; ///< 정산 감사 기록은 거래당 하나이다. 재시도가 둘을 만들지 않게 한다
            static constexpr int32  kMaxRecoverCount   = 256;

            /** @brief 거래 레코드를 써 @p inoutTransaction 에 넣습니다. 못 쓰면 오류 로그를 남기고 false 이며 아무것도 넣지 않습니다. */
            [[nodiscard]] static bool stageSession( const TradeSnapshot& snapshot, uint64 ownerServerID, uint64 expectedVersion, ServiceTransaction& inoutTransaction )
            {
                vector<uint8> bytes;
                if ( TradeSessionRecord::encodeSession( TradeSessionRecord{ snapshot, ownerServerID }, bytes ) == false )
                    return false;
                const string sessionKey = ServiceKeyUtil::makeHex64( snapshot._tradeID );
                if ( snapshot.isClosed() ) // 닫는 모든 경로가 이 함수를 지나므로 색인 줄을 빠뜨리는 경로가 없다
                    inoutTransaction.put( TradeStoreLogic::getSessionExpiryTable(), ServiceRetentionRule::makeIndexKey( snapshot._updatedMs, sessionKey ), vector<uint8>{},
                                          ServiceRecord::kAnyVersion );
                inoutTransaction.put( TradeStoreLogic::getSessionTable(), sessionKey, std::move( bytes ), expectedVersion );
                return true;
            }

            /** @brief 닫힌 거래 레코드의 닫은 시각을 읽습니다(보존 쓸기). 열린 거래나 읽지 못하는 레코드는 false 이고 지우지 않습니다. */
            [[nodiscard]] static bool readClosedTimeMs( const ServiceRecord& record, int64& outTimeMs )
            {
                TradeSnapshot snapshot;
                uint64        ownerServerID = 0;
                if ( decodeSession( record._key, record._bytes, snapshot, ownerServerID ) == false || snapshot.isClosed() == false )
                    return false;
                outTimeMs = snapshot._updatedMs;
                return true;
            }

            [[nodiscard]] static bool decodeSession( const string_view key, const vector<uint8>& bytes, TradeSnapshot& outSnapshot, uint64& outOwnerServerID )
            {
                TradeSessionRecord record;
                if ( TradeSessionRecord::decodeSession( key, bytes, record ) == false )
                    return false;
                outSnapshot      = std::move( record._snapshot );
                outOwnerServerID = record._ownerServerID;
                return true;
            }

            /** @brief 정산 분개 키를 만듭니다. */
            [[nodiscard]] static bool makeJournalKey( uint64 tradeID, string& outKey )
            {
                return LedgerJournalKey::makeFromToken( "trade", ServiceKeyUtil::makeHex64( tradeID ), outKey );
            }

            static uint64 mixSeed( uint64 value )
            {
                return HashUtil::mix64( value + HashUtil::kGoldenRatio64 );
            }

            static string makeOwnerKey( uint64 serverID, uint64 tradeID )
            {
                string key = ServiceKeyUtil::makeHex64( serverID );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, tradeID );
                return key;
            }

            static TradeServiceResult toClosedResult( const TradeSnapshot& trade )
            {
                if ( trade._state == TradeState::Settled )
                    return TradeResult::Ok;
                if ( trade._state == TradeState::Failed )
                    return trade._closeReason == TradeCloseReason::CapExceeded ? TradeResult::CapExceeded : TradeResult::InsufficientFunds;
                return TradeResult::WrongState;
            }

            static string describeForAudit( const TradeSnapshot& trade )
            {
                string text;
                for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
                {
                    const TradeSide& side = trade._arrSide[sideIndex];
                    text += sideIndex == 0 ? "a=" : ";b=";
                    text += ServiceKeyUtil::makeHex64( side._accountID );
                    for ( const TradeLeg& leg : side._listLeg )
                    {
                        StringBuilder<constant::buffer::kMax256> textPart;
                        textPart.appendFormat( ",%#x%#", leg._assetID, leg._amount );
                        text += textPart.c_str();
                    }
                }
                return text;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TradeStoreLogic::TradeStoreLogic( IServiceStoreConnection& connection, const ITradePolicy& policy, const ILedgerPolicy* pLedgerPolicy, uint64 serverID,
                                      const TradeServiceConfig& settings )
        : _connection{ connection }
        , _policy{ policy }
        , _pLedgerPolicy{ pLedgerPolicy }
        , _settings{ settings }
        , _serverID{ serverID }
    {
    }

    const hashed_string& TradeStoreLogic::getSessionTable()
    {
        static const hashed_string s_table{ "trade_session" };
        return s_table;
    }

    const hashed_string& TradeStoreLogic::getActiveTable()
    {
        static const hashed_string s_table{ "trade_active" };
        return s_table;
    }

    const hashed_string& TradeStoreLogic::getSessionExpiryTable()
    {
        static const hashed_string s_table{ "trade_session_expiry" };
        return s_table;
    }

    ServiceRetentionRule TradeStoreLogic::makeRetentionRule( int64 retentionMs )
    {
        ServiceRetentionRule rule;
        rule._table       = getSessionTable();
        rule._indexTable  = getSessionExpiryTable();
        rule._retentionMs = retentionMs;
        rule._pfnReadTime = &TradeStoreLogicInternal::readClosedTimeMs;
        return rule;
    }

    const hashed_string& TradeStoreLogic::getOwnerTable()
    {
        static const hashed_string s_table{ "trade_owner" };
        return s_table;
    }

    bool TradeStoreLogic::isIdleExpired( const TradeSnapshot& trade, const TradeServiceConfig& settings, int64 nowMs )
    {
        if ( trade.isClosed() )
            return false;
        const int64 timeoutMs = trade._state == TradeState::Invited ? settings._inviteTimeoutMs : settings._idleTimeoutMs;
        return nowMs >= trade._updatedMs + timeoutMs;
    }

    TradeServiceResult TradeStoreLogic::invite( AccountID fromID, AccountID toID, uint64 seed, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        using Internal                       = TradeStoreLogicInternal;
        const bool bInvalidTradeParticipants = fromID == kInvalidAccountID || toID == kInvalidAccountID || fromID == toID;
        if ( bInvalidTradeParticipants )
            return TradeServiceResult::fail( OnlineError::kInvalidRequest );
        uint64 candidate = seed;
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            candidate                      = Internal::mixSeed( candidate );
            const uint64       tradeID     = candidate == 0 ? 1 : candidate;
            const AccountID    arrParty[2] = { fromID, toID };
            ServiceTransaction transaction;
            uint64             arrLinkVersion[2] = { ServiceRecord::kAbsentVersion, ServiceRecord::kAbsentVersion };
            uint64             closedTradeID     = 0;
            for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
            {
                ServiceRecord            linkRaw;
                const ServiceStoreResult linkRead = _connection.readRecord( getActiveTable(), ServiceKeyUtil::makeHex64( arrParty[sideIndex] ), linkRaw );
                if ( linkRead == ServiceStoreResult::NotFound )
                    continue;
                if ( linkRead != ServiceStoreResult::Ok )
                    return TradeServiceResult::fail( OnlineError::kUnavailable );
                // 남은 링크가 있고 그 거래가 닫혔거나 시한이 지났으면 정리하고 이어 간다. 지연 만료이다.
                SessionRead              stale;
                const TradeServiceResult staleRead = readSession( ServiceKeyUtil::decodeVarID( linkRaw._bytes ), stale );
                const bool               bOrphan   = staleRead == TradeResult::NotFound;
                const bool               bExpired  = staleRead == TradeResult::Ok && ( stale._snapshot.isClosed() || isIdleExpired( stale._snapshot, _settings, nowMs ) );
                if ( bOrphan == false && bExpired == false )
                    return staleRead == TradeResult::Ok ? ( sideIndex == 0 ? TradeResult::AlreadyTrading : TradeResult::PeerBusy ) : TradeServiceResult::fail( OnlineError::kUnavailable );
                arrLinkVersion[sideIndex]         = linkRaw._version;
                const bool bAlreadyClosedHere     = closedTradeID != 0 && closedTradeID == stale._snapshot._tradeID; // 두 사람의 이전 거래가 같은 것이다
                const bool bShouldCloseStaleTrade = bExpired && stale._snapshot.isClosed() == false && bAlreadyClosedHere == false;
                if ( bShouldCloseStaleTrade )
                {
                    closedTradeID        = stale._snapshot._tradeID;
                    TradeSnapshot closed = stale._snapshot;
                    TradeStateMachine::close( closed, TradeState::Cancelled, TradeCloseReason::Timeout, nowMs );
                    if ( Internal::stageSession( closed, stale._ownerServerID, stale._version, transaction ) == false )
                        return TradeServiceResult::fail( OnlineError::kInvalidRequest );
                    transaction.erase( getOwnerTable(), Internal::makeOwnerKey( stale._ownerServerID, closed._tradeID ) );
                }
            }
            TradeSnapshot trade;
            trade._tradeID               = tradeID;
            trade._createdMs             = nowMs;
            trade._updatedMs             = nowMs;
            trade._state                 = TradeState::Invited;
            trade._arrSide[0]._accountID = fromID;
            trade._arrSide[1]._accountID = toID;
            // 쓰기 번호 0과 1이 두 활성 링크이다. Conflict 번호로 누가 바쁜지 구분한다
            transaction.put( getActiveTable(), ServiceKeyUtil::makeHex64( fromID ), ServiceKeyUtil::encodeVarID( tradeID ), arrLinkVersion[0] );
            transaction.put( getActiveTable(), ServiceKeyUtil::makeHex64( toID ), ServiceKeyUtil::encodeVarID( tradeID ), arrLinkVersion[1] );
            if ( Internal::stageSession( trade, _serverID, ServiceRecord::kAbsentVersion, transaction ) == false )
                return TradeServiceResult::fail( OnlineError::kInvalidRequest );
            transaction.put( getOwnerTable(), Internal::makeOwnerKey( _serverID, tradeID ), vector<uint8>{}, ServiceRecord::kAbsentVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult committed = _connection.commit( transaction, &info );
            if ( committed == ServiceStoreResult::Ok )
            {
                outSnapshot = trade;
                return TradeResult::Ok;
            }
            if ( committed != ServiceStoreResult::Conflict )
                return TradeServiceResult::fail( OnlineError::kUnavailable );
            // 그사이 다른 서버가 링크를 넣었거나 거래 id 가 겹쳤다. 링크를 넣었으면 다시 읽을 때 바쁨으로 나오고, id 가 겹쳤으면 다음 id 를 쓴다. 어느 쪽이든 다시 시도한다.
        }
        return TradeServiceResult::fail( OnlineError::kUnavailable );
    }

    TradeServiceResult TradeStoreLogic::applyCommand( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& outSnapshot, LedgerTransferResult& outLedger )
    {
        using Internal = TradeStoreLogicInternal;
        outLedger      = LedgerTransferResult{};
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            SessionRead              session;
            const TradeServiceResult read = readSession( tradeID, session );
            if ( read != TradeResult::Ok )
                return read;
            // 당사자가 아니면 스냅숏을 싣지 않는다 — 권한 실패 응답에 남의 거래 내용(물건 · 상대 · 상태)이 나갔다(감사 O-4, Steam 교환 · Nakama 자리).
            if ( session._snapshot.findSideIndex( command._actorID ).has_value() == false )
            {
                outSnapshot = TradeSnapshot{};
                return TradeResult::NotParty;
            }
            outSnapshot = session._snapshot;
            if ( session._snapshot.isClosed() )
                return command._kind == TradeCommandKind::Confirm ? Internal::toClosedResult( session._snapshot ) : TradeResult::WrongState; // 재시도이거나 다른 서버가 먼저 처리한 경우이다

            if ( isIdleExpired( session._snapshot, _settings, nowMs ) )
            {
                TradeSnapshot     closed;
                const CloseResult serviceResult = commitClose( session, TradeCloseReason::Timeout, nowMs, closed );
                if ( serviceResult._bRetry )
                    continue;
                if ( serviceResult._result != TradeResult::Ok )
                    return serviceResult._result;
                outSnapshot = closed;
                return TradeResult::WrongState;
            }

            TradeSnapshot            next   = session._snapshot;
            const TradeServiceResult result = TradeStateMachine::apply( next, command, _policy, nowMs );
            if ( result != TradeResult::Ok )
                return result;
            if ( command._kind == TradeCommandKind::Lock )
            {
                // 위에서 당사자를 확인했다 — 그래도 클라이언트 명령 경로라 멈추지 않고 거절한다.
                const optional<int32> sideIndex = next.findSideIndex( command._actorID );
                if ( sideIndex.has_value() == false )
                    return TradeResult::NotParty;
                const TradeServiceResult funds = evaluateFunds( next._arrSide[*sideIndex] );
                if ( funds != TradeResult::Ok )
                    return funds;
            }

            ServiceTransaction transaction;
            SettlementStage    settlement;
            if ( next._state == TradeState::Open && next.isBothConfirmed() )
            {
                const TradeServiceResult settled = stageSettlement( tradeID, command, nowMs, next, transaction, settlement, outLedger );
                if ( settled != TradeResult::Ok )
                    return settled;
            }

            if ( next.isClosed() )
            {
                if ( stageClosedTrade( session, next, tradeID, nowMs, transaction ) == false )
                    return TradeServiceResult::fail( OnlineError::kInvalidRequest );
            }
            else if ( Internal::stageSession( next, session._ownerServerID, session._version, transaction ) == false )
            {
                return TradeServiceResult::fail( OnlineError::kInvalidRequest );
            }

            const ServiceStoreResult committed = _connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                outSnapshot = next;
                return next._state == TradeState::Failed ? Internal::toClosedResult( next ) : TradeResult::Ok;
            }
            if ( committed == ServiceStoreResult::Conflict || committed == ServiceStoreResult::Unavailable )
            {
                // 다른 서버나 앞선 시도가 같은 분개로 이미 정산했는지 원장이 가려 준다. 응답이 유실된 경우도 포함한다. 분개가 있으면 다음 반복의 읽기가 Settled 이다.
                if ( settlement._bStaged && Ledger::resolveConflict( _connection, settlement._request, outLedger ) == LedgerResult::Ok )
                    continue;
                if ( committed == ServiceStoreResult::Unavailable )
                    return TradeServiceResult::fail( OnlineError::kUnavailable );
                continue; // 상대가 그사이 바꿨으므로 다시 읽고 다시 시도한다
            }
            return TradeServiceResult::fail( OnlineError::kInvalidRequest );
        }
        return TradeServiceResult::fail( OnlineError::kUnavailable );
    }

    TradeServiceResult TradeStoreLogic::closeIfIdle( uint64 tradeID, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            SessionRead              session;
            const TradeServiceResult read = readSession( tradeID, session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( isIdleExpired( session._snapshot, _settings, nowMs ) == false )
                return TradeResult::WrongState;
            TradeSnapshot     closed;
            const CloseResult serviceResult = commitClose( session, TradeCloseReason::Timeout, nowMs, closed );
            if ( serviceResult._bRetry )
                continue;
            if ( serviceResult._result != TradeResult::Ok )
                return serviceResult._result;
            outSnapshot = closed;
            return TradeResult::Ok;
        }
        return TradeServiceResult::fail( OnlineError::kUnavailable );
    }

    TradeServiceResult TradeStoreLogic::closeForAccount( AccountID accountID, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            linkRaw;
            const ServiceStoreResult linkRead = _connection.readRecord( getActiveTable(), ServiceKeyUtil::makeHex64( accountID ), linkRaw );
            if ( linkRead == ServiceStoreResult::NotFound )
                return TradeResult::NotFound;
            if ( linkRead != ServiceStoreResult::Ok )
                return TradeServiceResult::fail( OnlineError::kUnavailable );
            SessionRead              session;
            const TradeServiceResult read = readSession( ServiceKeyUtil::decodeVarID( linkRaw._bytes ), session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( session._snapshot.isClosed() )
                return TradeResult::WrongState;
            TradeSnapshot     closed;
            const CloseResult serviceResult = commitClose( session, reason, nowMs, closed );
            if ( serviceResult._bRetry )
                continue;
            if ( serviceResult._result != TradeResult::Ok )
                return serviceResult._result;
            outSnapshot = closed;
            return TradeResult::Ok;
        }
        return TradeServiceResult::fail( OnlineError::kUnavailable );
    }

    TradeServiceResult TradeStoreLogic::recoverOwned( int64 nowMs, vector<TradeSnapshot>& outListClosed )
    {
        using Internal = TradeStoreLogicInternal;
        string prefix  = ServiceKeyUtil::makeHex64( _serverID );
        prefix.push_back( '/' );
        vector<ServiceRecord> listOwned;
        if ( _connection.listRecords( getOwnerTable(), prefix, "", Internal::kMaxRecoverCount, false, listOwned ) != ServiceStoreResult::Ok )
            return TradeServiceResult::fail( OnlineError::kUnavailable );
        for ( const ServiceRecord& owned : listOwned )
        {
            uint64 tradeID = 0;
            if ( ServiceKeyUtil::parseHex64( string_view( owned._key ).substr( prefix.size() ), tradeID ) == false )
                continue;
            for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
            {
                SessionRead              session;
                const TradeServiceResult read = readSession( tradeID, session );
                if ( read == TradeResult::NotFound || ( read == TradeResult::Ok && session._snapshot.isClosed() ) )
                {
                    ServiceTransaction transaction;
                    transaction.erase( getOwnerTable(), owned._key );
                    (void)_connection.commit( transaction );
                    break;
                }
                if ( read != TradeResult::Ok )
                    return read;
                TradeSnapshot     closed;
                const CloseResult serviceResult = commitClose( session, TradeCloseReason::ServerRestart, nowMs, closed );
                if ( serviceResult._bRetry )
                    continue;
                if ( serviceResult._result != TradeResult::Ok )
                    return serviceResult._result;
                outListClosed.push_back( closed );
                break;
            }
        }
        return TradeResult::Ok;
    }

    TradeServiceResult TradeStoreLogic::readSession( uint64 tradeID, SessionRead& outSession )
    {
        ServiceRecord            raw;
        const ServiceStoreResult read = _connection.readRecord( getSessionTable(), ServiceKeyUtil::makeHex64( tradeID ), raw );
        if ( read == ServiceStoreResult::NotFound )
            return TradeResult::NotFound;
        if ( read != ServiceStoreResult::Ok )
            return TradeServiceResult::fail( OnlineError::kUnavailable );
        if ( TradeStoreLogicInternal::decodeSession( ServiceKeyUtil::makeHex64( tradeID ), raw._bytes, outSession._snapshot, outSession._ownerServerID ) == false )
            return TradeServiceResult::fail( OnlineError::kUnavailable ); // 손상은 읽기가 오류 로그로 남겼다
        outSession._version = raw._version;
        return TradeResult::Ok;
    }

    TradeStoreLogic::CloseResult TradeStoreLogic::commitClose( const SessionRead& session, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outClosed )
    {
        outClosed = session._snapshot;
        TradeStateMachine::close( outClosed, TradeState::Cancelled, reason, nowMs );
        ServiceTransaction transaction;
        if ( stageClose( session, outClosed, transaction ) == false )
            return CloseResult{ TradeServiceResult::fail( OnlineError::kInvalidRequest ), false };
        const ServiceStoreResult committed = _connection.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return CloseResult{ TradeResult::Ok, true };
        return CloseResult{ committed == ServiceStoreResult::Ok ? TradeResult::Ok : TradeServiceResult::fail( OnlineError::kUnavailable ), false };
    }

    TradeServiceResult TradeStoreLogic::stageSettlement( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& inoutNext, ServiceTransaction& inoutTransaction,
                                                         SettlementStage& outStage, LedgerTransferResult& outLedger )
    {
        LedgerTransferRequest& request = outStage._request;
        if ( TradeStoreLogicInternal::makeJournalKey( tradeID, request._journalKey ) == false )
            return TradeServiceResult::fail( OnlineError::kInvalidRequest );
        request._reason    = "trade.settle";
        request._pPolicy   = _pLedgerPolicy;
        request._timeMs    = nowMs;
        request._actorKind = LedgerActorKind::Player;
        request._actorID   = command._actorID; // 마지막 확정이다. 분개는 두 계정 모두의 내역에 들어간다
        for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
        {
            const TradeSide& giver    = inoutNext._arrSide[sideIndex];
            const TradeSide& receiver = inoutNext._arrSide[1 - sideIndex];
            for ( const TradeLeg& leg : giver._listLeg )
            {
                request._listPosting.push_back(
                    LedgerPosting{ LedgerHolder::makeAccount( giver._accountID ), LedgerHolder::makeAccount( receiver._accountID ), leg._assetID, leg._amount } );
            }
        }
        if ( request._listPosting.empty() )
        {
            TradeStateMachine::close( inoutNext, TradeState::Settled, TradeCloseReason::None, nowMs ); // 빈 거래도 닫는다. 원장은 건드리지 않는다
            return TradeResult::Ok;
        }
        const LedgerResult staged = Ledger::stageTransfer( _connection, request, inoutTransaction, outLedger );
        if ( staged == LedgerResult::InsufficientFunds || staged == LedgerResult::CapExceeded )
        {
            inoutTransaction.clear(); // 원장 부분은 붙지 않았으므로 거래만 실패로 닫는다
            TradeStateMachine::close( inoutNext, TradeState::Failed, staged == LedgerResult::InsufficientFunds ? TradeCloseReason::InsufficientFunds : TradeCloseReason::CapExceeded,
                                      nowMs );
            return TradeResult::Ok;
        }
        if ( staged == LedgerResult::AccountDeleted )
        {
            inoutTransaction.clear(); // 상대가 탈퇴로 지워졌다 — 원장 부분은 붙지 않았으므로 거래만 실패로 닫는다(다시 해도 갈 곳이 없다)
            TradeStateMachine::close( inoutNext, TradeState::Failed, TradeCloseReason::PartyLeft, nowMs );
            return TradeResult::Ok;
        }
        if ( staged != LedgerResult::Ok )
            return staged == LedgerResult::Unavailable ? TradeServiceResult::fail( OnlineError::kUnavailable ) : TradeServiceResult::fail( OnlineError::kInvalidRequest );
        outStage._bStaged = outLedger._bReplayed == SW_FALSE;
        TradeStateMachine::close( inoutNext, TradeState::Settled, TradeCloseReason::None, nowMs ); // 분개가 이미 있으면 정산은 끝난 것이다
        return TradeResult::Ok;
    }

    bool TradeStoreLogic::stageClosedTrade( const SessionRead& session, const TradeSnapshot& closed, uint64 tradeID, int64 nowMs, ServiceTransaction& inoutTransaction )
    {
        if ( stageClose( session, closed, inoutTransaction ) == false )
            return false;
        if ( closed._state == TradeState::Cancelled )
            return true;
        ServiceAuditEntry audit;
        audit._actor   = ServiceAuditLog::kSystemActor;
        audit._action  = closed._state == TradeState::Settled ? "trade.settle" : "trade.fail";
        audit._subject = "trade." + ServiceKeyUtil::makeHex64( tradeID );
        audit._after   = TradeStoreLogicInternal::describeForAudit( closed );
        audit._timeMs  = nowMs < 0 ? 0 : nowMs;
        ServiceAuditLog::stageEntry( inoutTransaction, audit, tradeID, TradeStoreLogicInternal::kSettleAuditUnique );
        return true;
    }

    bool TradeStoreLogic::stageClose( const SessionRead& session, const TradeSnapshot& closed, ServiceTransaction& inoutTransaction )
    {
        if ( TradeStoreLogicInternal::stageSession( closed, session._ownerServerID, session._version, inoutTransaction ) == false )
            return false;
        for ( const TradeSide& side : closed._arrSide )
        {
            inoutTransaction.erase( getActiveTable(), ServiceKeyUtil::makeHex64( side._accountID ) );
        }
        inoutTransaction.erase( getOwnerTable(), TradeStoreLogicInternal::makeOwnerKey( session._ownerServerID, closed._tradeID ) );
        return true;
    }

    TradeServiceResult TradeStoreLogic::evaluateFunds( const TradeSide& side )
    {
        for ( const TradeLeg& leg : side._listLeg )
        {
            LedgerBalance balance;
            if ( Ledger::readBalance( _connection, LedgerHolder::makeAccount( side._accountID ), leg._assetID, balance ) != ServiceStoreResult::Ok )
                return TradeServiceResult::fail( OnlineError::kUnavailable );
            if ( balance._amount < leg._amount )
                return TradeResult::InsufficientFunds;
        }
        return TradeResult::Ok;
    }
} // namespace sw
