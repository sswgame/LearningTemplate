#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"

#include "Core/Hash/HashUtil.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    /** @brief 거래 요청 하나의 입력입니다. 저장소 스레드가 읽는 값 복사입니다. */
    struct TradeCallInput
    {
        TradeCommand     _command{};
        uint64           _tradeID{ 0 };
        AccountID        _fromID{ kInvalidAccountID };
        AccountID        _toID{ kInvalidAccountID };
        uint64           _seed{ 0 };
        uint64           _requestTag{ 0 };
        UnixMilliseconds _nowMs{ 0 };
        TradeOperation   _operation{ TradeOperation::Invite };
    };

    namespace
    {
        /** @brief 거래 일이 완료로 넘기는 값입니다. */
        struct TradeCallResult
        {
            TradeCompletion       _completion{};
            vector<TradeSnapshot> _listChanged{};
        };

        /** @brief 저장소 스레드에서 도는 거래 일입니다. 서비스 멤버를 만지지 않고 인자로 받은 값만 씁니다. */
        struct TradeStoreRun
        {
            static TradeCallResult runCall( IServiceStoreConnection& connection, const ITradePolicy& policy, const ILedgerPolicy* pLedgerPolicy, uint64 serverID,
                                            const TradeServiceConfig& settings, const TradeCallInput& input )
            {
                TradeCallResult  done;
                TradeCompletion& completion = done._completion;
                completion._operation       = input._operation;
                completion._requestTag      = input._requestTag;
                const int64     nowMs       = input._nowMs.count();
                TradeStoreLogic logic{ connection, policy, pLedgerPolicy, serverID, settings };
                switch ( input._operation )
                {
                    case TradeOperation::Invite:
                    {
                        completion._result = logic.invite( input._fromID, input._toID, input._seed, nowMs, completion._snapshot );
                        break;
                    }
                    case TradeOperation::Expire:
                    {
                        completion._result = logic.closeIfIdle( input._tradeID, nowMs, completion._snapshot );
                        break;
                    }
                    case TradeOperation::Leave:
                    {
                        completion._result = logic.closeForAccount( input._fromID, input._command._reason, nowMs, completion._snapshot );
                        break;
                    }
                    case TradeOperation::Recover:
                    {
                        completion._result = logic.recoverOwned( nowMs, done._listChanged );
                        break;
                    }
                    case TradeOperation::Respond:
                    case TradeOperation::SetOffer:
                    case TradeOperation::Lock:
                    case TradeOperation::Confirm:
                    case TradeOperation::Cancel:
                    {
                        completion._result = logic.applyCommand( input._tradeID, input._command, nowMs, completion._snapshot, completion._ledger );
                        break;
                    }
                }
                const bool bChanged = input._operation != TradeOperation::Recover && completion._snapshot._tradeID != 0 &&
                                      ( completion._result == TradeResult::Ok || completion._snapshot.isClosed() );
                if ( bChanged )
                    done._listChanged.push_back( completion._snapshot );
                return done;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TradeService::TradeService()
        : _completionBuffer{}
        , _updateBuffer{}
        , _mapTradeToDeadline{}
        , _settings{}
        , _pPolicy{ nullptr }
        , _pLedgerPolicy{ nullptr }
        , _serverID{ 0 }
        , _nextSeed{ 0 }
        , _store{}
    {
    }

    void TradeService::initialize( IServiceStore* pStore, const ITradePolicy* pPolicy, const ILedgerPolicy* pLedgerPolicy, uint64 serverID, const TradeServiceConfig& settings )
    {
        _store.bindStore( pStore );
        _pPolicy       = pPolicy;
        _pLedgerPolicy = pLedgerPolicy;
        _serverID      = serverID;
        _settings      = settings;
        _nextSeed      = HashUtil::fnvStep64( HashUtil::kFNVOffset64, serverID );
    }

    void TradeService::shutdown()
    {
        _mapTradeToDeadline.clear();
        _store.bindStore( nullptr );
    }

    void TradeService::recoverOwnedTrades( UnixMilliseconds nowMs )
    {
        TradeCallInput work = makeCallInput( TradeOperation::Recover, uint64{ 0 }, nowMs );
        submitCall( std::move( work ) );
    }

    void TradeService::invite( AccountID fromID, AccountID toID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCallInput work = makeCallInput( TradeOperation::Invite, requestTag, nowMs );
        work._fromID        = fromID;
        work._toID          = toID;
        work._seed          = ++_nextSeed ^ static_cast<uint64>( nowMs.count() );
        submitCall( std::move( work ) );
    }

    void TradeService::respondInvite( AccountID responderID, uint64 tradeID, bool bAccept, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorID = responderID;
        command._kind    = bAccept ? TradeCommandKind::Accept : TradeCommandKind::Decline;
        submitCommand( TradeOperation::Respond, tradeID, command, nowMs, requestTag );
    }

    void TradeService::setOffer( AccountID actorID, uint64 tradeID, const vector<TradeLeg>& listLeg, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorID = actorID;
        command._kind    = TradeCommandKind::SetOffer;
        command._listLeg = listLeg;
        submitCommand( TradeOperation::SetOffer, tradeID, command, nowMs, requestTag );
    }

    void TradeService::lock( AccountID actorID, uint64 tradeID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorID = actorID;
        command._kind    = TradeCommandKind::Lock;
        submitCommand( TradeOperation::Lock, tradeID, command, nowMs, requestTag );
    }

    void TradeService::confirm( AccountID actorID, uint64 tradeID, uint32 seenOwnRevision, uint32 seenPeerRevision, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorID          = actorID;
        command._kind             = TradeCommandKind::Confirm;
        command._seenOwnRevision  = seenOwnRevision;
        command._seenPeerRevision = seenPeerRevision;
        submitCommand( TradeOperation::Confirm, tradeID, command, nowMs, requestTag );
    }

    void TradeService::cancel( AccountID actorID, uint64 tradeID, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorID = actorID;
        command._kind    = TradeCommandKind::Cancel;
        command._reason  = TradeCloseReason::CancelledByParty;
        submitCommand( TradeOperation::Cancel, tradeID, command, nowMs, requestTag );
    }

    void TradeService::closeForAccount( AccountID accountID, TradeCloseReason reason, UnixMilliseconds nowMs )
    {
        TradeCallInput work   = makeCallInput( TradeOperation::Leave, uint64{ 0 }, nowMs );
        work._fromID          = accountID;
        work._command._reason = reason;
        submitCall( std::move( work ) );
    }

    void TradeService::update( UnixMilliseconds nowMs )
    {
        for ( auto tradeIt = _mapTradeToDeadline.begin(); tradeIt != _mapTradeToDeadline.end(); )
        {
            if ( nowMs.count() < tradeIt->second )
            {
                ++tradeIt;
                continue;
            }
            TradeCallInput work = makeCallInput( TradeOperation::Expire, uint64{ 0 }, nowMs );
            work._tradeID       = tradeIt->first;
            tradeIt             = _mapTradeToDeadline.erase( tradeIt ); // 결과가 닫힘이면 그대로 두고, 아직이면 다시 넣는다
            submitCall( std::move( work ) );
        }
    }

    TradeCallInput TradeService::makeCallInput( TradeOperation operation, uint64 requestTag, UnixMilliseconds nowMs )
    {
        TradeCallInput input;
        input._operation  = operation;
        input._requestTag = requestTag;
        input._nowMs      = nowMs;
        return input;
    }

    void TradeService::submitCall( TradeCallInput&& input )
    {
        const ITradePolicy*  pPolicy       = _pPolicy;
        const ILedgerPolicy* pLedgerPolicy = _pLedgerPolicy;
        const uint64         serverID      = _serverID;
        _store.submit( [pPolicy, pLedgerPolicy, serverID, settings = _settings, input]( IServiceStoreConnection& connection )
        { return TradeStoreRun::runCall( connection, *pPolicy, pLedgerPolicy, serverID, settings, input ); },
                       [this, tradeID = input._tradeID, nowMs = input._nowMs]( TradeCallResult& done )
        {
            const bool bExpiryFailed = done._completion._operation == TradeOperation::Expire && done._completion._result.hasError( OnlineError::kUnavailable );
            if ( bExpiryFailed )
                retryExpiry( tradeID, nowMs );
            applyCompletion( std::move( done._completion ), done._listChanged );
        } );
    }

    void TradeService::applyCompletion( TradeCompletion&& completion, const vector<TradeSnapshot>& listChanged )
    {
        for ( const TradeSnapshot& changed : listChanged )
        {
            trackTrade( changed );
            _updateBuffer.push( changed );
        }
        if ( completion._operation == TradeOperation::Expire && completion._result == TradeResult::WrongState )
            trackTrade( completion._snapshot ); // 아직 시한 전이다. 그사이 바뀌었으므로 새 시한으로 다시 확인한다
        if ( completion._requestTag != 0 )
            _completionBuffer.push( std::move( completion ) );
    }

    void TradeService::retryExpiry( uint64 tradeID, UnixMilliseconds nowMs )
    {
        _mapTradeToDeadline[tradeID] = nowMs.count() + TradeConstant::kExpiryRetryMs; // 잊으면 당사자가 시한을 알림으로 받지 못한다. 저장소가 돌아오면 닫힌다
    }

    void TradeService::submitCommand( TradeOperation operation, uint64 tradeID, const TradeCommand& command, UnixMilliseconds nowMs, uint64 requestTag )
    {
        TradeCallInput work = makeCallInput( operation, requestTag, nowMs );
        work._tradeID       = tradeID;
        work._command       = command;
        submitCall( std::move( work ) );
    }

    void TradeService::trackTrade( const TradeSnapshot& snapshot )
    {
        if ( snapshot._tradeID == 0 )
            return;
        if ( snapshot.isClosed() )
        {
            _mapTradeToDeadline.erase( snapshot._tradeID );
            return;
        }
        const int64 timeoutMs                  = snapshot._state == TradeState::Invited ? _settings._inviteTimeoutMs : _settings._idleTimeoutMs;
        _mapTradeToDeadline[snapshot._tradeID] = snapshot._updatedMs + timeoutMs;
    }
} // namespace sw
