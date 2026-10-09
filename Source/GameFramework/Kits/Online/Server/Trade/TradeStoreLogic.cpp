#include "pch.h"

#include "GameFramework/Kits/Online/Server/Trade/TradeStoreLogic.h"

#include "Core/Common/HashUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    SW_LOG_CALLER( "TradeStoreLogic" );

    namespace
    {
        struct TradeStoreLogicInternal
        {
            static constexpr uint8  kRecordFormat      = 1;
            static constexpr uint64 kSettleAuditUnique = 0x5E771Eull; ///< 정산 감사 줄은 거래당 하나 — 재시도가 둘을 만들지 않게
            static constexpr int32  kMaxRecoverCount   = 256;

            static uint64 mixSeed( uint64 value )
            {
                return HashUtil::mix64( value + HashUtil::kGoldenRatio64 );
            }

            static string makeOwnerKey( uint64 serverId, uint64 tradeId )
            {
                string key = ServiceKeyUtil::makeHex64( serverId );
                key.push_back( '/' );
                ServiceKeyUtil::appendHex64( key, tradeId );
                return key;
            }

            static vector<uint8> encodeId( uint64 id )
            {
                BitWriter writer;
                writer.writeVarUint( id );
                return writer.releaseBytes();
            }

            static uint64 decodeId( const vector<uint8>& bytes )
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
                    text += ServiceKeyUtil::makeHex64( side._accountId );
                    for ( const TradeLeg& leg : side._listLeg )
                    {
                        text += "," + leg._assetId + "x" + to_string( leg._amount );
                    }
                }
                return text;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TradeStoreLogic::TradeStoreLogic( IServiceStoreConnection& connection, const ITradePolicy& policy, const ILedgerPolicy* pLedgerPolicy, uint64 serverId,
                                      const TradeSettings& settings )
        : _connection{ connection }
        , _policy{ policy }
        , _pLedgerPolicy{ pLedgerPolicy }
        , _settings{ settings }
        , _serverId{ serverId }
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

    bool TradeStoreLogic::isIdleExpired( const TradeSnapshot& trade, const TradeSettings& settings, int64 nowMs )
    {
        if ( trade.isClosed() )
            return false;
        const int64 timeoutMs = trade._state == TradeState::Invited ? settings._inviteTimeoutMs : settings._idleTimeoutMs;
        return nowMs >= trade._updatedMs + timeoutMs;
    }

    TradeResult TradeStoreLogic::invite( AccountId fromId, AccountId toId, uint64 seed, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        using Internal = TradeStoreLogicInternal;
        if ( fromId == kInvalidAccountId || toId == kInvalidAccountId || fromId == toId )
            return TradeResult::Invalid;
        uint64 candidate = seed;
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            candidate                      = Internal::mixSeed( candidate );
            const uint64       tradeId     = candidate == 0 ? 1 : candidate;
            const AccountId    arrParty[2] = { fromId, toId };
            ServiceTransaction transaction;
            uint64             arrLinkVersion[2] = { ServiceRecord::kAbsentVersion, ServiceRecord::kAbsentVersion };
            uint64             closedTradeId     = 0;
            for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
            {
                ServiceRecord            linkRaw;
                const ServiceStoreResult linkRead = _connection.readRecord( getActiveTable(), ServiceKeyUtil::makeHex64( arrParty[sideIndex] ), linkRaw );
                if ( linkRead == ServiceStoreResult::NotFound )
                    continue;
                if ( linkRead != ServiceStoreResult::Ok )
                    return TradeResult::Unavailable;
                // 남은 링크 — 그 거래가 닫혔거나 시한이 지났으면 정리하고 이어 간다(게으른 만료).
                SessionRead       stale;
                const TradeResult staleRead = readSession( Internal::decodeId( linkRaw._bytes ), stale );
                const bool        bOrphan   = staleRead == TradeResult::NotFound;
                const bool        bExpired  = staleRead == TradeResult::Ok && ( stale._snapshot.isClosed() || isIdleExpired( stale._snapshot, _settings, nowMs ) );
                if ( bOrphan == false && bExpired == false )
                    return staleRead == TradeResult::Ok ? ( sideIndex == 0 ? TradeResult::AlreadyTrading : TradeResult::PeerBusy ) : TradeResult::Unavailable;
                arrLinkVersion[sideIndex]     = linkRaw._version;
                const bool bAlreadyClosedHere = closedTradeId != 0 && closedTradeId == stale._snapshot._tradeId; // 두 사람의 옛 거래가 같은 것
                if ( bExpired && stale._snapshot.isClosed() == false && bAlreadyClosedHere == false )
                {
                    closedTradeId        = stale._snapshot._tradeId;
                    TradeSnapshot closed = stale._snapshot;
                    TradeStateMachine::close( closed, TradeState::Cancelled, TradeCloseReason::Timeout, nowMs );
                    transaction.put( getSessionTable(), ServiceKeyUtil::makeHex64( closed._tradeId ), TradeRecordUtil::encodeSession( closed, stale._ownerServerId ),
                                     stale._version );
                    transaction.erase( getOwnerTable(), Internal::makeOwnerKey( stale._ownerServerId, closed._tradeId ) );
                }
            }
            TradeSnapshot trade;
            trade._tradeId               = tradeId;
            trade._createdMs             = nowMs;
            trade._updatedMs             = nowMs;
            trade._state                 = TradeState::Invited;
            trade._arrSide[0]._accountId = fromId;
            trade._arrSide[1]._accountId = toId;
            // 쓰기 번호 0 · 1 = 두 활성 링크(Conflict 번호로 누가 바쁜지 가린다)
            transaction.put( getActiveTable(), ServiceKeyUtil::makeHex64( fromId ), Internal::encodeId( tradeId ), arrLinkVersion[0] );
            transaction.put( getActiveTable(), ServiceKeyUtil::makeHex64( toId ), Internal::encodeId( tradeId ), arrLinkVersion[1] );
            transaction.put( getSessionTable(), ServiceKeyUtil::makeHex64( tradeId ), TradeRecordUtil::encodeSession( trade, _serverId ), ServiceRecord::kAbsentVersion );
            transaction.put( getOwnerTable(), Internal::makeOwnerKey( _serverId, tradeId ), vector<uint8>{}, ServiceRecord::kAbsentVersion );
            ServiceCommitInfo        info;
            const ServiceStoreResult committed = _connection.commit( transaction, &info );
            if ( committed == ServiceStoreResult::Ok )
            {
                outSnapshot = trade;
                return TradeResult::Ok;
            }
            if ( committed != ServiceStoreResult::Conflict )
                return TradeResult::Unavailable;
            // 그새 다른 서버가 링크를 넣었거나(다시 읽으면 바쁘다) 거래 id 가 겹쳤다(다음 id) — 다시.
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::applyCommand( uint64 tradeId, const TradeCommand& command, int64 nowMs, TradeSnapshot& outSnapshot, LedgerTransferOutcome& outLedger )
    {
        using Internal = TradeStoreLogicInternal;
        outLedger      = LedgerTransferOutcome{};
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            SessionRead       session;
            const TradeResult read = readSession( tradeId, session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( session._snapshot.findSideIndex( command._actorId ) < 0 )
                return TradeResult::NotParty;
            if ( session._snapshot.isClosed() )
                return command._kind == TradeCommandKind::Confirm ? Internal::toClosedResult( session._snapshot ) : TradeResult::WrongState; // 재시도 · 다른 서버가 먼저

            ServiceTransaction transaction;
            if ( isIdleExpired( session._snapshot, _settings, nowMs ) )
            {
                TradeSnapshot closed = session._snapshot;
                TradeStateMachine::close( closed, TradeState::Cancelled, TradeCloseReason::Timeout, nowMs );
                stageClose( session, closed, transaction );
                const ServiceStoreResult committed = _connection.commit( transaction );
                if ( committed == ServiceStoreResult::Conflict )
                    continue;
                if ( committed != ServiceStoreResult::Ok )
                    return TradeResult::Unavailable;
                outSnapshot = closed;
                return TradeResult::WrongState;
            }

            TradeSnapshot     next   = session._snapshot;
            const TradeResult result = TradeStateMachine::apply( next, command, _policy, nowMs );
            if ( result != TradeResult::Ok )
                return result;
            if ( command._kind == TradeCommandKind::Lock )
            {
                const TradeResult funds = evaluateFunds( next._arrSide[next.findSideIndex( command._actorId )] );
                if ( funds != TradeResult::Ok )
                    return funds;
            }

            LedgerTransferRequest request;
            const bool            bSettle = next._state == TradeState::Open && next.isBothConfirmed();
            bool                  bStaged = false;
            if ( bSettle )
            {
                if ( TradeRecordUtil::makeJournalKey( tradeId, request._journalKey ) == false )
                    return TradeResult::Invalid;
                request._reason    = "trade.settle";
                request._pPolicy   = _pLedgerPolicy;
                request._timeMs    = nowMs;
                request._actorKind = LedgerActorKind::Player;
                request._actorId   = command._actorId; // 마지막 확정 — 분개는 두 계정 모두의 내역에 든다
                for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
                {
                    const TradeSide& giver    = next._arrSide[sideIndex];
                    const TradeSide& receiver = next._arrSide[1 - sideIndex];
                    for ( const TradeLeg& leg : giver._listLeg )
                    {
                        request._listPosting.push_back(
                            LedgerPosting{ LedgerHolder::makeAccount( giver._accountId ), LedgerHolder::makeAccount( receiver._accountId ), leg._assetId, leg._amount } );
                    }
                }
                if ( request._listPosting.empty() )
                {
                    TradeStateMachine::close( next, TradeState::Settled, TradeCloseReason::None, nowMs ); // 빈 거래도 닫힌다 — 원장은 건드리지 않는다
                }
                else
                {
                    const LedgerResult staged = Ledger::stageTransfer( _connection, request, transaction, outLedger );
                    if ( staged == LedgerResult::InsufficientFunds || staged == LedgerResult::CapExceeded )
                    {
                        transaction.clear(); // 원장 몫은 붙지 않았다 — 거래만 실패로 닫는다
                        TradeStateMachine::close( next, TradeState::Failed,
                                                  staged == LedgerResult::InsufficientFunds ? TradeCloseReason::InsufficientFunds : TradeCloseReason::CapExceeded, nowMs );
                    }
                    else if ( staged != LedgerResult::Ok )
                    {
                        return staged == LedgerResult::Unavailable ? TradeResult::Unavailable : TradeResult::Invalid;
                    }
                    else
                    {
                        bStaged = outLedger._bReplayed == SW_FALSE;
                        TradeStateMachine::close( next, TradeState::Settled, TradeCloseReason::None, nowMs ); // 분개가 이미 있으면 정산은 끝났다
                    }
                }
            }

            if ( next.isClosed() )
            {
                stageClose( session, next, transaction );
                ServiceAuditEntry audit;
                audit._actor   = "system";
                audit._action  = next._state == TradeState::Settled ? "trade.settle" : "trade.fail";
                audit._subject = "trade." + ServiceKeyUtil::makeHex64( tradeId );
                audit._after   = Internal::describeForAudit( next );
                audit._timeMs  = nowMs < 0 ? 0 : nowMs;
                if ( next._state != TradeState::Cancelled )
                    ServiceAuditLog::stageEntry( transaction, audit, tradeId, Internal::kSettleAuditUnique );
            }
            else
            {
                transaction.put( getSessionTable(), ServiceKeyUtil::makeHex64( tradeId ), TradeRecordUtil::encodeSession( next, session._ownerServerId ), session._version );
            }

            const ServiceStoreResult committed = _connection.commit( transaction );
            if ( committed == ServiceStoreResult::Ok )
            {
                outSnapshot = next;
                return next._state == TradeState::Failed ? Internal::toClosedResult( next ) : TradeResult::Ok;
            }
            if ( committed == ServiceStoreResult::Conflict || committed == ServiceStoreResult::Unavailable )
            {
                // 다른 서버 · 앞선 시도가 같은 분개로 이미 정산했나(응답 유실 포함) — 원장이 가린다. 분개가 있으면 다음 고리의 읽기가 Settled 다.
                if ( bStaged && Ledger::resolveConflict( _connection, request, outLedger ) == LedgerResult::Ok )
                    continue;
                if ( committed == ServiceStoreResult::Unavailable )
                    return TradeResult::Unavailable;
                continue; // 상대가 그새 바꿨다 — 다시 읽고 다시
            }
            return TradeResult::Invalid;
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::closeIfIdle( uint64 tradeId, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            SessionRead       session;
            const TradeResult read = readSession( tradeId, session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( isIdleExpired( session._snapshot, _settings, nowMs ) == false )
                return TradeResult::WrongState;
            TradeSnapshot closed = session._snapshot;
            TradeStateMachine::close( closed, TradeState::Cancelled, TradeCloseReason::Timeout, nowMs );
            ServiceTransaction transaction;
            stageClose( session, closed, transaction );
            const ServiceStoreResult committed = _connection.commit( transaction );
            if ( committed == ServiceStoreResult::Conflict )
                continue;
            if ( committed != ServiceStoreResult::Ok )
                return TradeResult::Unavailable;
            outSnapshot = closed;
            return TradeResult::Ok;
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::closeForAccount( AccountId accountId, TradeCloseReason reason, int64 nowMs, TradeSnapshot& outSnapshot )
    {
        using Internal = TradeStoreLogicInternal;
        for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
        {
            ServiceRecord            linkRaw;
            const ServiceStoreResult linkRead = _connection.readRecord( getActiveTable(), ServiceKeyUtil::makeHex64( accountId ), linkRaw );
            if ( linkRead == ServiceStoreResult::NotFound )
                return TradeResult::NotFound;
            if ( linkRead != ServiceStoreResult::Ok )
                return TradeResult::Unavailable;
            SessionRead       session;
            const TradeResult read = readSession( Internal::decodeId( linkRaw._bytes ), session );
            if ( read != TradeResult::Ok )
                return read;
            outSnapshot = session._snapshot;
            if ( session._snapshot.isClosed() )
                return TradeResult::WrongState;
            TradeSnapshot closed = session._snapshot;
            TradeStateMachine::close( closed, TradeState::Cancelled, reason, nowMs );
            ServiceTransaction transaction;
            stageClose( session, closed, transaction );
            const ServiceStoreResult committed = _connection.commit( transaction );
            if ( committed == ServiceStoreResult::Conflict )
                continue;
            if ( committed != ServiceStoreResult::Ok )
                return TradeResult::Unavailable;
            outSnapshot = closed;
            return TradeResult::Ok;
        }
        return TradeResult::Unavailable;
    }

    TradeResult TradeStoreLogic::recoverOwned( int64 nowMs, vector<TradeSnapshot>& outListClosed )
    {
        using Internal = TradeStoreLogicInternal;
        string prefix  = ServiceKeyUtil::makeHex64( _serverId );
        prefix.push_back( '/' );
        vector<ServiceRecord> listOwned;
        if ( _connection.listRecords( getOwnerTable(), prefix, "", Internal::kMaxRecoverCount, false, listOwned ) != ServiceStoreResult::Ok )
            return TradeResult::Unavailable;
        for ( const ServiceRecord& owned : listOwned )
        {
            uint64 tradeId = 0;
            if ( ServiceKeyUtil::parseHex64( string_view( owned._key ).substr( prefix.size() ), tradeId ) == false )
                continue;
            for ( int32 attempt = 0; attempt < LedgerConstant::kMaxRetryCount; ++attempt )
            {
                SessionRead        session;
                const TradeResult  read = readSession( tradeId, session );
                ServiceTransaction transaction;
                if ( read == TradeResult::NotFound || ( read == TradeResult::Ok && session._snapshot.isClosed() ) )
                {
                    transaction.erase( getOwnerTable(), owned._key );
                    (void)_connection.commit( transaction );
                    break;
                }
                if ( read != TradeResult::Ok )
                    return read;
                TradeSnapshot closed = session._snapshot;
                TradeStateMachine::close( closed, TradeState::Cancelled, TradeCloseReason::ServerRestart, nowMs );
                stageClose( session, closed, transaction );
                const ServiceStoreResult committed = _connection.commit( transaction );
                if ( committed == ServiceStoreResult::Conflict )
                    continue;
                if ( committed != ServiceStoreResult::Ok )
                    return TradeResult::Unavailable;
                outListClosed.push_back( closed );
                break;
            }
        }
        return TradeResult::Ok;
    }

    TradeResult TradeStoreLogic::readSession( uint64 tradeId, SessionRead& outSession )
    {
        ServiceRecord            raw;
        const ServiceStoreResult read = _connection.readRecord( getSessionTable(), ServiceKeyUtil::makeHex64( tradeId ), raw );
        if ( read == ServiceStoreResult::NotFound )
            return TradeResult::NotFound;
        if ( read != ServiceStoreResult::Ok )
            return TradeResult::Unavailable;
        if ( TradeRecordUtil::decodeSession( raw._bytes, outSession._snapshot, outSession._ownerServerId ) == false )
        {
            SW_LOG_ERROR( "trade record %# is corrupt", ServiceKeyUtil::makeHex64( tradeId ).c_str() );
            return TradeResult::Unavailable;
        }
        outSession._version = raw._version;
        return TradeResult::Ok;
    }

    void TradeStoreLogic::stageClose( const SessionRead& session, const TradeSnapshot& closed, ServiceTransaction& inoutTransaction )
    {
        inoutTransaction.put( getSessionTable(), ServiceKeyUtil::makeHex64( closed._tradeId ), TradeRecordUtil::encodeSession( closed, session._ownerServerId ),
                              session._version );
        for ( const TradeSide& side : closed._arrSide )
        {
            inoutTransaction.erase( getActiveTable(), ServiceKeyUtil::makeHex64( side._accountId ) );
        }
        inoutTransaction.erase( getOwnerTable(), TradeStoreLogicInternal::makeOwnerKey( session._ownerServerId, closed._tradeId ) );
    }

    TradeResult TradeStoreLogic::evaluateFunds( const TradeSide& side )
    {
        for ( const TradeLeg& leg : side._listLeg )
        {
            LedgerBalance balance;
            if ( Ledger::readBalance( _connection, LedgerHolder::makeAccount( side._accountId ), leg._assetId, balance ) != ServiceStoreResult::Ok )
                return TradeResult::Unavailable;
            if ( balance._amount < leg._amount )
                return TradeResult::InsufficientFunds;
        }
        return TradeResult::Ok;
    }

    vector<uint8> TradeRecordUtil::encodeSession( const TradeSnapshot& snapshot, uint64 ownerServerId )
    {
        BitWriter writer;
        writer.writeBits( TradeStoreLogicInternal::kRecordFormat, 8 );
        writer.writeVarUint( ownerServerId );
        TradeWire::writeSnapshot( writer, snapshot );
        return writer.releaseBytes();
    }

    bool TradeRecordUtil::decodeSession( const vector<uint8>& bytes, TradeSnapshot& outSnapshot, uint64& outOwnerServerId )
    {
        BitReader reader{ bytes.data(), static_cast<int32>( bytes.size() ) };
        if ( reader.readBits( 8 ) != TradeStoreLogicInternal::kRecordFormat )
            return false;
        outOwnerServerId = reader.readVarUint();
        return TradeWire::readSnapshot( reader, outSnapshot );
    }

    bool TradeRecordUtil::makeJournalKey( uint64 tradeId, string& outKey ) { return LedgerJournalKey::makeFromToken( "trade", ServiceKeyUtil::makeHex64( tradeId ), outKey ); }
} // namespace sw
