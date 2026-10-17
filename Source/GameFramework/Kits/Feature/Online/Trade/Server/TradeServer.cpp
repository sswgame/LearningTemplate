#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"

namespace sw
{
    TradeServer::TradeServer()
        : _mapTagToCall{}
        , _mapLookupToCall{}
        , _mapTradeToLedger{}
        , _listCompletionScratch{}
        , _listUpdateScratch{}
        , _listFound{}
        , _listFoundScratch{}
        , _pTradeService{ nullptr }
        , _pDirectory{ nullptr }
        , _pPresence{ nullptr }
        , _nextTag{ 1 }
        , _nowMs{ 0 }
    {
    }

    void TradeServer::initialize( TradeService* pTradeService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence )
    {
        _pTradeService = pTradeService;
        _pDirectory    = pDirectory;
        _pPresence     = pPresence;
    }

    void TradeServer::shutdown()
    {
        if ( _pPresence != nullptr ) // 맡긴 이름 찾기를 회수한다. 접속 상태 관리자가 이 객체보다 오래 살아도 이 객체를 부르지 않게 하려는 것이다
        {
            for ( const auto& [lookupID, call] : _mapLookupToCall )
            {
                _pPresence->cancel( lookupID );
            }
        }
        _mapTagToCall.clear();
        _mapLookupToCall.clear();
        _listFound.clear();
        _pTradeService = nullptr;
    }

    void TradeServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        _nowMs                      = context._nowMs;
        const RemoteConfig* pConfig = host.getRemoteConfig();
        if ( pConfig != nullptr && pConfig->isFeatureEnabled( kFeatureFlag, context._accountID, true ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kFeatureDisabled );
            return;
        }
        const uint64 tag = _nextTag++;
        if ( context._method == TradeMethod::kInvite )
        {
            TradeInviteRequest request;
            if ( TradeProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            AccountIdentity peer;
            if ( _pDirectory != nullptr && _pDirectory->findIdentityByDisplayName( request._peerDisplayName, peer ) )
            {
                _mapTagToCall[tag] = PendingCall{ context._token, context._accountID };
                _pTradeService->invite( context._accountID, peer._accountID, context._nowMs, tag );
                return;
            }
            if ( _pPresence == nullptr )
            {
                respondImmediately( host, context._token, TradeResult::PeerOffline );
                return;
            }
            const uint64 lookupID      = _pPresence->submitFindByDisplayName( request._peerDisplayName, AccountPresenceDelegate::create<&TradeServer::onPresenceFound>( this ) );
            _mapLookupToCall[lookupID] = PendingLookup{ context._token, context._accountID, context._nowMs };
            return;
        }

        FixedReadResult readResult = FixedReadResult::Ok;
        switch ( context._method )
        {
            case TradeMethod::kRespond:
            {
                TradeRespondRequest request;
                readResult = TradeProtocol::MessageTypes::read( body, request );
                if ( readResult == FixedReadResult::Ok )
                    _pTradeService->respondInvite( context._accountID, request._tradeID, request._bAccept == SW_TRUE, context._nowMs, tag );
                break;
            }
            case TradeMethod::kSetOffer:
            {
                TradeOfferRequest request;
                readResult = TradeProtocol::MessageTypes::read( body, request );
                if ( readResult == FixedReadResult::Ok )
                    _pTradeService->setOffer( context._accountID, request._tradeID, request._listLeg, context._nowMs, tag );
                break;
            }
            case TradeMethod::kLock:
            {
                TradeIDRequest request;
                readResult = TradeProtocol::MessageTypes::read( body, request );
                if ( readResult == FixedReadResult::Ok )
                    _pTradeService->lock( context._accountID, request._tradeID, context._nowMs, tag );
                break;
            }
            case TradeMethod::kConfirm:
            {
                TradeConfirmRequest request;
                readResult = TradeProtocol::MessageTypes::read( body, request );
                if ( readResult == FixedReadResult::Ok )
                    _pTradeService->confirm( context._accountID, request._tradeID, request._seenOwnRevision, request._seenPeerRevision, context._nowMs, tag );
                break;
            }
            case TradeMethod::kCancel:
            {
                TradeIDRequest request;
                readResult = TradeProtocol::MessageTypes::read( body, request );
                if ( readResult == FixedReadResult::Ok )
                    _pTradeService->cancel( context._accountID, request._tradeID, context._nowMs, tag );
                break;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
        if ( readResult != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        _mapTagToCall[tag] = PendingCall{ context._token, context._accountID };
    }

    void TradeServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _pPresence != nullptr )
        {
            _listFoundScratch.clear();
            _listFoundScratch.swap( _listFound );
            for ( const AccountPresenceResult& found : _listFoundScratch )
            {
                const auto lookupIt = _mapLookupToCall.find( found._requestID );
                if ( lookupIt == _mapLookupToCall.end() )
                    continue;
                const PendingLookup lookup = lookupIt->second;
                _mapLookupToCall.erase( lookupIt );
                if ( found._identity._accountID == kInvalidAccountID )
                {
                    respondImmediately( host, lookup._token, TradeResult::PeerOffline );
                    continue;
                }
                const uint64 tag   = _nextTag++;
                _mapTagToCall[tag] = PendingCall{ lookup._token, lookup._accountID };
                _pTradeService->invite( lookup._accountID, found._identity._accountID, lookup._nowMs, tag );
            }
        }
        _pTradeService->update( nowMs );
        _listCompletionScratch.clear();
        _pTradeService->drainCompletions( _listCompletionScratch );
        _mapTradeToLedger.clear();
        for ( const TradeCompletion& completion : _listCompletionScratch )
        {
            if ( completion._snapshot._state == TradeState::Settled && completion._ledger._listHolderBalance.empty() == false )
                _mapTradeToLedger[completion._snapshot._tradeID] = completion._ledger;
            const auto callIt = _mapTagToCall.find( completion._requestTag );
            if ( callIt == _mapTagToCall.end() )
                continue;
            const PendingCall call = callIt->second;
            _mapTagToCall.erase( callIt );
            if ( completion._result == TradeResult::Unavailable )
            {
                (void)host.respondError( call._token, OnlineError::kUnavailable );
                continue;
            }
            TradeReply reply;
            reply._result   = completion._result;
            reply._snapshot = completion._snapshot;
            collectBalances( completion._ledger, call._accountID, reply._listBalance );
            respond( host, call._token, reply );
        }
        _listUpdateScratch.clear();
        _pTradeService->drainUpdates( _listUpdateScratch );
        for ( const TradeSnapshot& snapshot : _listUpdateScratch )
        {
            pushSnapshot( host, snapshot );
        }
    }

    void TradeServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        _pTradeService->closeForAccount( accountID, TradeCloseReason::PartyLeft, _nowMs );
    }

    void TradeServer::pushSnapshot( OnlineServiceHost& host, const TradeSnapshot& snapshot )
    {
        uint16 kind = TradeMethod::kPushUpdate;
        if ( snapshot._state == TradeState::Invited )
            kind = TradeMethod::kPushInvited;
        else if ( snapshot.isClosed() )
            kind = TradeMethod::kPushClosed;
        const auto ledgerIt = _mapTradeToLedger.find( snapshot._tradeID );
        for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
        {
            if ( kind == TradeMethod::kPushInvited && sideIndex == 0 )
                continue; // 신청한 쪽은 응답으로 알게 된다
            const AccountID accountID = snapshot._arrSide[sideIndex]._accountID;
            TradeReply      push;
            push._snapshot = snapshot;
            if ( ledgerIt != _mapTradeToLedger.end() )
                collectBalances( ledgerIt->second, accountID, push._listBalance );
            BitWriter body;
            if ( TradeProtocol::MessageTypes::write( body, push ) == false )
                continue; // write 가 범위 밖 값을 로그에 남겼다
            if ( host.sendPush( accountID, kind, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( accountID, kind, body ); // 상대가 다른 서버에 접속해 있다
        }
    }

    void TradeServer::onPresenceFound( const AccountPresenceResult& found )
    {
        _listFound.push_back( found );
    }

    void TradeServer::respondImmediately( OnlineServiceHost& host, const NetRequestToken& token, TradeResult result )
    {
        TradeReply reply;
        reply._result = result;
        respond( host, token, reply );
    }

    void TradeServer::respond( OnlineServiceHost& host, const NetRequestToken& token, const TradeReply& reply )
    {
        (void)host.respondMessage<TradeProtocol::MessageTypes>( token, reply );
    }

    void TradeServer::collectBalances( const LedgerTransferOutcome& ledger, AccountID accountID, vector<TradeBalance>& outListBalance )
    {
        for ( const LedgerTransferOutcome::HolderBalance& holderBalance : ledger._listHolderBalance )
        {
            if ( holderBalance._holder._kind == LedgerHolderKind::Account && holderBalance._holder._accountID == accountID )
                outListBalance.push_back( TradeBalance{ holderBalance._balance._assetID, holderBalance._balance._amount } );
        }
    }
} // namespace sw
