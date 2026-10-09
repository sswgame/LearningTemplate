#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Trade/TradeService.h"

#include "Core/Common/HashUtil.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    namespace
    {
        struct TradeServiceInternal
        {
            /** @brief 거래 요청 하나를 저장소 스레드에서 돌립니다. 입력은 복사해 든다. */
            class TradeWork final : public IServiceStoreWork
            {
            public:
                TradeCommand _command;
                uint64       _tradeId;
                AccountId    _fromId;
                AccountId    _toId;
                uint64       _seed;

                TradeWork( TradeService* pService, const ITradePolicy* pPolicy, const ILedgerPolicy* pLedgerPolicy, uint64 serverId, TradeOperation operation,
                           uint64 requestTag, int64 nowMs )
                    : _command{}
                    , _tradeId{ 0 }
                    , _fromId{ kInvalidAccountId }
                    , _toId{ kInvalidAccountId }
                    , _seed{ 0 }
                    , _completion{}
                    , _listChanged{}
                    , _settings{ pService->getSettings() }
                    , _pService{ pService }
                    , _pPolicy{ pPolicy }
                    , _pLedgerPolicy{ pLedgerPolicy }
                    , _serverId{ serverId }
                    , _nowMs{ nowMs }
                {
                    _completion._operation  = operation;
                    _completion._requestTag = requestTag;
                }

                void run( IServiceStoreConnection& connection ) override
                {
                    TradeStoreLogic logic{ connection, *_pPolicy, _pLedgerPolicy, _serverId, _settings };
                    switch ( _completion._operation )
                    {
                        case TradeOperation::Invite:
                        {
                            _completion._result = logic.invite( _fromId, _toId, _seed, _nowMs, _completion._snapshot );
                            break;
                        }
                        case TradeOperation::Expire:
                        {
                            _completion._result = logic.closeIfIdle( _tradeId, _nowMs, _completion._snapshot );
                            break;
                        }
                        case TradeOperation::Leave:
                        {
                            _completion._result = logic.closeForAccount( _fromId, _command._reason, _nowMs, _completion._snapshot );
                            break;
                        }
                        case TradeOperation::Recover:
                        {
                            _completion._result = logic.recoverOwned( _nowMs, _listChanged );
                            break;
                        }
                        case TradeOperation::Respond:
                        case TradeOperation::SetOffer:
                        case TradeOperation::Lock:
                        case TradeOperation::Confirm:
                        case TradeOperation::Cancel:
                        {
                            _completion._result = logic.applyCommand( _tradeId, _command, _nowMs, _completion._snapshot, _completion._ledger );
                            break;
                        }
                    }
                    const bool bChanged = _completion._operation != TradeOperation::Recover && _completion._snapshot._tradeId != 0 &&
                                          ( _completion._result == TradeResult::Ok || _completion._snapshot.isClosed() );
                    if ( bChanged )
                        _listChanged.push_back( _completion._snapshot );
                }

                void complete() override { _pService->applyCompletion( std::move( _completion ), _listChanged ); }

            private:
                TradeCompletion       _completion;
                vector<TradeSnapshot> _listChanged;
                TradeSettings         _settings;
                TradeService*         _pService;
                const ITradePolicy*   _pPolicy;
                const ILedgerPolicy*  _pLedgerPolicy;
                uint64                _serverId;
                int64                 _nowMs;
            };
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
        , _pStore{ nullptr }
        , _pPolicy{ nullptr }
        , _pLedgerPolicy{ nullptr }
        , _serverId{ 0 }
        , _nextSeed{ 0 }
        , _pendingCount{ 0 }
    {
    }

    void TradeService::initialize( IServiceStore* pStore, const ITradePolicy* pPolicy, const ILedgerPolicy* pLedgerPolicy, uint64 serverId, const TradeSettings& settings )
    {
        _pStore        = pStore;
        _pPolicy       = pPolicy;
        _pLedgerPolicy = pLedgerPolicy;
        _serverId      = serverId;
        _settings      = settings;
        _nextSeed      = serverId * HashUtil::kFnvPrime64;
    }

    void TradeService::shutdown()
    {
        _mapTradeToDeadline.clear();
        _pStore = nullptr;
    }

    void TradeService::recoverOwnedTrades( int64 nowMs )
    {
        unique_ptr<TradeServiceInternal::TradeWork> work =
            make_unique<TradeServiceInternal::TradeWork>( this, _pPolicy, _pLedgerPolicy, _serverId, TradeOperation::Recover, 0, nowMs );
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void TradeService::invite( AccountId fromId, AccountId toId, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<TradeServiceInternal::TradeWork> work =
            make_unique<TradeServiceInternal::TradeWork>( this, _pPolicy, _pLedgerPolicy, _serverId, TradeOperation::Invite, requestTag, nowMs );
        work->_fromId = fromId;
        work->_toId   = toId;
        work->_seed   = ++_nextSeed ^ static_cast<uint64>( nowMs );
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void TradeService::respondInvite( AccountId responderId, uint64 tradeId, bool bAccept, int64 nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorId = responderId;
        command._kind    = bAccept ? TradeCommandKind::Accept : TradeCommandKind::Decline;
        submitCommand( TradeOperation::Respond, tradeId, command, nowMs, requestTag );
    }

    void TradeService::setOffer( AccountId actorId, uint64 tradeId, const vector<TradeLeg>& listLeg, int64 nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorId = actorId;
        command._kind    = TradeCommandKind::SetOffer;
        command._listLeg = listLeg;
        submitCommand( TradeOperation::SetOffer, tradeId, command, nowMs, requestTag );
    }

    void TradeService::lock( AccountId actorId, uint64 tradeId, int64 nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorId = actorId;
        command._kind    = TradeCommandKind::Lock;
        submitCommand( TradeOperation::Lock, tradeId, command, nowMs, requestTag );
    }

    void TradeService::confirm( AccountId actorId, uint64 tradeId, uint32 seenOwnRevision, uint32 seenPeerRevision, int64 nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorId          = actorId;
        command._kind             = TradeCommandKind::Confirm;
        command._seenOwnRevision  = seenOwnRevision;
        command._seenPeerRevision = seenPeerRevision;
        submitCommand( TradeOperation::Confirm, tradeId, command, nowMs, requestTag );
    }

    void TradeService::cancel( AccountId actorId, uint64 tradeId, int64 nowMs, uint64 requestTag )
    {
        TradeCommand command;
        command._actorId = actorId;
        command._kind    = TradeCommandKind::Cancel;
        command._reason  = TradeCloseReason::CancelledByParty;
        submitCommand( TradeOperation::Cancel, tradeId, command, nowMs, requestTag );
    }

    void TradeService::closeForAccount( AccountId accountId, TradeCloseReason reason, int64 nowMs )
    {
        unique_ptr<TradeServiceInternal::TradeWork> work =
            make_unique<TradeServiceInternal::TradeWork>( this, _pPolicy, _pLedgerPolicy, _serverId, TradeOperation::Leave, 0, nowMs );
        work->_fromId          = accountId;
        work->_command._reason = reason;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void TradeService::tick( int64 nowMs )
    {
        for ( auto tradeIt = _mapTradeToDeadline.begin(); tradeIt != _mapTradeToDeadline.end(); )
        {
            if ( nowMs < tradeIt->second )
            {
                ++tradeIt;
                continue;
            }
            unique_ptr<TradeServiceInternal::TradeWork> work =
                make_unique<TradeServiceInternal::TradeWork>( this, _pPolicy, _pLedgerPolicy, _serverId, TradeOperation::Expire, 0, nowMs );
            work->_tradeId = tradeIt->first;
            ++_pendingCount;
            tradeIt = _mapTradeToDeadline.erase( tradeIt ); // 결과(닫힘 · 아직)가 다시 넣는다
            _pStore->submit( std::move( work ) );
        }
    }

    void TradeService::applyCompletion( TradeCompletion&& completion, const vector<TradeSnapshot>& listChanged )
    {
        --_pendingCount;
        for ( const TradeSnapshot& changed : listChanged )
        {
            trackTrade( changed );
            _updateBuffer.push( changed );
        }
        if ( completion._operation == TradeOperation::Expire && completion._result == TradeResult::WrongState )
            trackTrade( completion._snapshot ); // 아직 시한 전(그새 바뀌었다) — 새 시한으로 다시 본다
        if ( completion._requestTag != 0 )
            _completionBuffer.push( std::move( completion ) );
    }

    void TradeService::submitCommand( TradeOperation operation, uint64 tradeId, const TradeCommand& command, int64 nowMs, uint64 requestTag )
    {
        unique_ptr<TradeServiceInternal::TradeWork> work =
            make_unique<TradeServiceInternal::TradeWork>( this, _pPolicy, _pLedgerPolicy, _serverId, operation, requestTag, nowMs );
        work->_tradeId = tradeId;
        work->_command = command;
        ++_pendingCount;
        _pStore->submit( std::move( work ) );
    }

    void TradeService::trackTrade( const TradeSnapshot& snapshot )
    {
        if ( snapshot._tradeId == 0 )
            return;
        if ( snapshot.isClosed() )
        {
            _mapTradeToDeadline.erase( snapshot._tradeId );
            return;
        }
        const int64 timeoutMs                  = snapshot._state == TradeState::Invited ? _settings._inviteTimeoutMs : _settings._idleTimeoutMs;
        _mapTradeToDeadline[snapshot._tradeId] = snapshot._updatedMs + timeoutMs;
    }
} // namespace sw
