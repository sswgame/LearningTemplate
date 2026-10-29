#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStoreLogic.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeProtocol.h"

namespace sw
{
    SW_LOG_CALLER( "TradeStoreLogic" );

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
                if ( TradeProtocol::encodeSession( TradeSessionRecord{ snapshot, ownerServerID }, bytes ) == false )
                    return false;
                inoutTransaction.put( TradeStoreLogic::getSessionTable(), ServiceKeyUtil::makeHex64( snapshot._tradeID ), std::move( bytes ), expectedVersion );
                return true;
            }

            [[nodiscard]] static bool decodeSession( const string_view key, const vector<uint8>& bytes, TradeSnapshot& outSnapshot, uint64& outOwnerServerID )
            {
                TradeSessionRecord record;
                if ( TradeProtocol::decodeSession( key, bytes, record ) == false )
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

            static vector<uint8> encodeID( uint64 id )
            {
                BitWriter writer;
                writer.writeVarUint( id );
                return writer.releaseBytes();
            }

            static uint64 decodeID( const vector<uint8>& bytes )
            {
                BitReader    reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
                const uint64 id = reader.readVarUint();
                return reader.hasOverflowed() ? 0 : id;
            }

            static TradeResult toClosedResult( const TradeSnapshot& trade )
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
                        text += formatToString( ",%#x%#", leg._assetID, leg._amount );
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

    TradeResult TradeStoreLogic::invite( AccountID fromID, AccountID toID, uint64 seed, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        using Internal                       = TradeStoreLogicInternal;
        const bool bInvalidTradeParticipants = fromID == kInvalidAccountID || toID == kInvalidAccountID || fromID == toID;
        if ( bInvalidTradeParticipants )
            return TradeResult::Invalid;
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
                    return TradeResult::Unavailable;
                // 남은 링크가 있고 그 거래가 닫혔거나 시한이 지났으면 정리하고 이어 간다. 지연 만료이다.
                SessionRead       stale;
                const TradeResult staleRead = readSession( Internal::decodeID( linkRaw._bytes ), stale );
                const bool        bOrphan   = staleRead == TradeResult::NotFound;
                const bool        bExpired  = staleRead == TradeResult::Ok && ( stale._snapshot.isClosed() || isIdleExpired( stale._snapshot, _settings, nowMs ) );
                if ( bOrphan == false && bExpired == false )
                    return staleRead == TradeResult::Ok ? ( sideIndex == 0 ? TradeResult::AlreadyTrading : TradeResult::PeerBusy ) : TradeResult::Unavailable;
                arrLinkVersion[sideIndex]         = linkRaw._version;
                const bool bAlreadyClosedHere     = closedTradeID != 0 && closedTradeID == stale._snapshot._tradeID; // 두 사람의 이전 거래가 같은 것이다
                const bool bShouldCloseStaleTrade = bExpired && stale._snapshot.isClosed() == false && bAlreadyClosedHere == false;
                if ( bShouldCloseStaleTrade )
                {
                    closedTradeID        = stale._snapshot._tradeID;
                    TradeSnapshot closed = stale._snapshot;
                    TradeStateMachine::close( closed, TradeState::Cancelled, TradeCloseReason::Timeout, nowMs );
                    if ( Internal::stageSession( closed, stale._ownerServerID, stale._version, transaction ) == false )
                        return TradeResult::Invalid;
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
            transaction.put( getActiveTable(), ServiceKeyUtil::makeHex64( fromID ), Internal::encodeID( tradeID ), arrLinkVersion[0] );
            transaction.put( getActiveTable(), ServiceKeyUtil::makeHex64( toID ), Internal::encodeID( tradeID ), arrLinkVersion[1] );
            if ( Internal::stageSession( trade, _serverID, ServiceRecord::kAbsentVersion, transaction ) == false )
                return TradeResult::Invalid;
            transaction.put( getOwnerTable(), Internal::makeOwnerKey( _serverID, tradeID ), vector<uint8>{}, ServiceRecord::kAbsentVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult committed = _connection.commit( transaction, &info );
            if ( committed == ServiceStoreResult::Ok )
            {
                outSnapshot = trade;
                return TradeResult::Ok;
            }
            if ( committed != ServiceStoreResult::Conflict )
                return TradeResult::Unavailable;
            // 그사이 다른 서버가 링크를 넣었거나 거래 id 가 겹쳤다. 링크를 넣었으면 다시 읽을 때 바쁨으로 나오고, id 가 겹쳤으면 다음 id 를 쓴다. 어느 쪽이든 다시 시도한다.
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::applyCommand( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& outSnapshot, LedgerTransferOutcome& outLedger )
    {
        using Internal = TradeStoreLogicInternal;
        outLedger      = LedgerTransferOutcome{};
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            SessionRead       session;
            const TradeResult read = readSession( tradeID, session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( session._snapshot.findSideIndex( command._actorID ) < 0 )
                return TradeResult::NotParty;
            if ( session._snapshot.isClosed() )
                return command._kind == TradeCommandKind::Confirm ? Internal::toClosedResult( session._snapshot ) : TradeResult::WrongState; // 재시도이거나 다른 서버가 먼저 처리한 경우이다

            if ( isIdleExpired( session._snapshot, _settings, nowMs ) )
            {
                TradeSnapshot      closed;
                const CloseOutcome outcome = commitClose( session, TradeCloseReason::Timeout, nowMs, closed );
                if ( outcome._bRetry )
                    continue;
                if ( outcome._result != TradeResult::Ok )
                    return outcome._result;
                outSnapshot = closed;
                return TradeResult::WrongState;
            }

            TradeSnapshot     next   = session._snapshot;
            const TradeResult result = TradeStateMachine::apply( next, command, _policy, nowMs );
            if ( result != TradeResult::Ok )
                return result;
            if ( command._kind == TradeCommandKind::Lock )
            {
                const TradeResult funds = evaluateFunds( next._arrSide[next.findSideIndex( command._actorID )] );
                if ( funds != TradeResult::Ok )
                    return funds;
            }

            ServiceTransaction transaction;
            SettlementStage    settlement;
            if ( next._state == TradeState::Open && next.isBothConfirmed() )
            {
                const TradeResult settled = stageSettlement( tradeID, command, nowMs, next, transaction, settlement, outLedger );
                if ( settled != TradeResult::Ok )
                    return settled;
            }

            if ( next.isClosed() )
            {
                if ( stageClosedTrade( session, next, tradeID, nowMs, transaction ) == false )
                    return TradeResult::Invalid;
            }
            else if ( Internal::stageSession( next, session._ownerServerID, session._version, transaction ) == false )
            {
                return TradeResult::Invalid;
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
                    return TradeResult::Unavailable;
                continue; // 상대가 그사이 바꿨으므로 다시 읽고 다시 시도한다
            }
            return TradeResult::Invalid;
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::closeIfIdle( uint64 tradeID, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            SessionRead       session;
            const TradeResult read = readSession( tradeID, session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( isIdleExpired( session._snapshot, _settings, nowMs ) == false )
                return TradeResult::WrongState;
            TradeSnapshot      closed;
            const CloseOutcome outcome = commitClose( session, TradeCloseReason::Timeout, nowMs, closed );
            if ( outcome._bRetry )
                continue;
            if ( outcome._result != TradeResult::Ok )
                return outcome._result;
            outSnapshot = closed;
            return TradeResult::Ok;
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::closeForAccount( AccountID accountID, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        using Internal = TradeStoreLogicInternal;
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            linkRaw;
            const ServiceStoreResult linkRead = _connection.readRecord( getActiveTable(), ServiceKeyUtil::makeHex64( accountID ), linkRaw );
            if ( linkRead == ServiceStoreResult::NotFound )
                return TradeResult::NotFound;
            if ( linkRead != ServiceStoreResult::Ok )
                return TradeResult::Unavailable;
            SessionRead       session;
            const TradeResult read = readSession( Internal::decodeID( linkRaw._bytes ), session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( session._snapshot.isClosed() )
                return TradeResult::WrongState;
            TradeSnapshot      closed;
            const CloseOutcome outcome = commitClose( session, reason, nowMs, closed );
            if ( outcome._bRetry )
                continue;
            if ( outcome._result != TradeResult::Ok )
                return outcome._result;
            outSnapshot = closed;
            return TradeResult::Ok;
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::recoverOwned( int64 nowMs, vector<TradeSnapshot>& outListClosed )
    {
        using Internal = TradeStoreLogicInternal;
        string prefix  = ServiceKeyUtil::makeHex64( _serverID );
        prefix.push_back( '/' );
        vector<ServiceRecord> listOwned;
        if ( _connection.listRecords( getOwnerTable(), prefix, "", Internal::kMaxRecoverCount, false, listOwned ) != ServiceStoreResult::Ok )
            return TradeResult::Unavailable;
        for ( const ServiceRecord& owned : listOwned )
        {
            uint64 tradeID = 0;
            if ( ServiceKeyUtil::parseHex64( string_view( owned._key ).substr( prefix.size() ), tradeID ) == false )
                continue;
            for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
            {
                SessionRead       session;
                const TradeResult read = readSession( tradeID, session );
                if ( read == TradeResult::NotFound || ( read == TradeResult::Ok && session._snapshot.isClosed() ) )
                {
                    ServiceTransaction transaction;
                    transaction.erase( getOwnerTable(), owned._key );
                    (void)_connection.commit( transaction );
                    break;
                }
                if ( read != TradeResult::Ok )
                    return read;
                TradeSnapshot      closed;
                const CloseOutcome outcome = commitClose( session, TradeCloseReason::ServerRestart, nowMs, closed );
                if ( outcome._bRetry )
                    continue;
                if ( outcome._result != TradeResult::Ok )
                    return outcome._result;
                outListClosed.push_back( closed );
                break;
            }
        }
        return TradeResult::Ok;
    }

    TradeResult TradeStoreLogic::readSession( uint64 tradeID, SessionRead& outSession )
    {
        ServiceRecord            raw;
        const ServiceStoreResult read = _connection.readRecord( getSessionTable(), ServiceKeyUtil::makeHex64( tradeID ), raw );
        if ( read == ServiceStoreResult::NotFound )
            return TradeResult::NotFound;
        if ( read != ServiceStoreResult::Ok )
            return TradeResult::Unavailable;
        if ( TradeStoreLogicInternal::decodeSession( ServiceKeyUtil::makeHex64( tradeID ), raw._bytes, outSession._snapshot, outSession._ownerServerID ) == false )
            return TradeResult::Unavailable; // 손상은 읽기가 오류 로그로 남겼다
        outSession._version = raw._version;
        return TradeResult::Ok;
    }

    TradeStoreLogic::CloseOutcome TradeStoreLogic::commitClose( const SessionRead& session, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outClosed )
    {
        outClosed = session._snapshot;
        TradeStateMachine::close( outClosed, TradeState::Cancelled, reason, nowMs );
        ServiceTransaction transaction;
        if ( stageClose( session, outClosed, transaction ) == false )
            return CloseOutcome{ TradeResult::Invalid, false };
        const ServiceStoreResult committed = _connection.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return CloseOutcome{ TradeResult::Ok, true };
        return CloseOutcome{ committed == ServiceStoreResult::Ok ? TradeResult::Ok : TradeResult::Unavailable, false };
    }

    TradeResult TradeStoreLogic::stageSettlement( uint64 tradeID, const TradeCommand& command, int64 nowMs, TradeSnapshot& inoutNext, ServiceTransaction& inoutTransaction,
                                                  SettlementStage& outStage, LedgerTransferOutcome& outLedger )
    {
        LedgerTransferRequest& request = outStage._request;
        if ( TradeStoreLogicInternal::makeJournalKey( tradeID, request._journalKey ) == false )
            return TradeResult::Invalid;
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
        if ( staged != LedgerResult::Ok )
            return staged == LedgerResult::Unavailable ? TradeResult::Unavailable : TradeResult::Invalid;
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
        audit._actor   = "system";
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

    TradeResult TradeStoreLogic::evaluateFunds( const TradeSide& side )
    {
        for ( const TradeLeg& leg : side._listLeg )
        {
            LedgerBalance balance;
            if ( Ledger::readBalance( _connection, LedgerHolder::makeAccount( side._accountID ), leg._assetID, balance ) != ServiceStoreResult::Ok )
                return TradeResult::Unavailable;
            if ( balance._amount < leg._amount )
                return TradeResult::InsufficientFunds;
        }
        return TradeResult::Ok;
    }
} // namespace sw
