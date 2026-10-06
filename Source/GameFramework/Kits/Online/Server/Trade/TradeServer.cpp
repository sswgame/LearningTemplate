#include "pch.h"

#include "GameFramework/Kits/Online/Server/Trade/TradeServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"

namespace sw
{
    namespace
    {
        struct TradeServerInternal
        {
            static constexpr int32 kMaxDisplayNameSize = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    TradeServer::TradeServer()
        : _mapTagToCall{}
        , _mapLookupToCall{}
        , _mapTradeToLedger{}
        , _listCompletionScratch{}
        , _listUpdateScratch{}
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
        _mapTagToCall.clear();
        _mapLookupToCall.clear();
        _pTradeService = nullptr;
    }

    void TradeServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        _nowMs                      = context._nowMs;
        const RemoteConfig* pConfig = host.getRemoteConfig();
        if ( pConfig != nullptr && pConfig->isFeatureEnabled( kFeatureFlag, context._accountId, true ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kFeatureDisabled );
            return;
        }
        const uint64 tag = _nextTag++;
        if ( context._method == TradeMethod::kInvite )
        {
            vector<uint8> nameBytes;
            if ( body.readBlob( nameBytes, TradeServerInternal::kMaxDisplayNameSize ) == false )
            {
                (void)host.respondError( context._token, OnlineError::kInvalidRequest );
                return;
            }
            const string_view name( reinterpret_cast<const utf8*>( nameBytes.data() ), nameBytes.size() );
            AccountIdentity   peer;
            if ( _pDirectory != nullptr && _pDirectory->findIdentityByDisplayName( name, peer ) )
            {
                _mapTagToCall[tag] = PendingCall{ context._token, context._accountId };
                _pTradeService->invite( context._accountId, peer._accountId, context._nowMs, tag );
                return;
            }
            if ( _pPresence == nullptr )
            {
                respondImmediately( host, context._token, TradeResult::PeerOffline );
                return;
            }
            _mapLookupToCall[_pPresence->submitFindByDisplayName( name )] = PendingLookup{ context._token, context._accountId, context._nowMs };
            return;
        }

        const uint64 tradeId  = body.readVarUint();
        bool         bDecoded = body.hasOverflowed() == false;
        switch ( context._method )
        {
            case TradeMethod::kRespond:
            {
                const bool bAccept = body.readBool();
                if ( bDecoded )
                    _pTradeService->respondInvite( context._accountId, tradeId, bAccept, context._nowMs, tag );
                break;
            }
            case TradeMethod::kSetOffer:
            {
                vector<TradeLeg> listLeg;
                bDecoded = bDecoded && TradeWire::readLegs( body, listLeg );
                if ( bDecoded )
                    _pTradeService->setOffer( context._accountId, tradeId, listLeg, context._nowMs, tag );
                break;
            }
            case TradeMethod::kLock:
            {
                if ( bDecoded )
                    _pTradeService->lock( context._accountId, tradeId, context._nowMs, tag );
                break;
            }
            case TradeMethod::kConfirm:
            {
                const uint32 ownRevision  = static_cast<uint32>( body.readVarUint() );
                const uint32 peerRevision = static_cast<uint32>( body.readVarUint() );
                bDecoded                  = bDecoded && body.hasOverflowed() == false;
                if ( bDecoded )
                    _pTradeService->confirm( context._accountId, tradeId, ownRevision, peerRevision, context._nowMs, tag );
                break;
            }
            case TradeMethod::kCancel:
            {
                if ( bDecoded )
                    _pTradeService->cancel( context._accountId, tradeId, context._nowMs, tag );
                break;
            }
            default:
            {
                (void)host.respondError( context._token, OnlineError::kNotFound );
                return;
            }
        }
        if ( bDecoded == false || body.hasOverflowed() )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        _mapTagToCall[tag] = PendingCall{ context._token, context._accountId };
    }

    void TradeServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _pPresence != nullptr )
        {
            _listFoundScratch.clear();
            (void)_pPresence->pollFound( _listFoundScratch );
            for ( const AccountPresenceResult& found : _listFoundScratch )
            {
                const auto lookupIt = _mapLookupToCall.find( found._requestId );
                if ( lookupIt == _mapLookupToCall.end() )
                    continue;
                const PendingLookup lookup = lookupIt->second;
                _mapLookupToCall.erase( lookupIt );
                if ( found._identity._accountId == kInvalidAccountId )
                {
                    respondImmediately( host, lookup._token, TradeResult::PeerOffline );
                    continue;
                }
                const uint64 tag   = _nextTag++;
                _mapTagToCall[tag] = PendingCall{ lookup._token, lookup._accountId };
                _pTradeService->invite( lookup._accountId, found._identity._accountId, lookup._nowMs, tag );
            }
        }
        _pTradeService->tick( nowMs );
        _listCompletionScratch.clear();
        _pTradeService->drainCompletions( _listCompletionScratch );
        _mapTradeToLedger.clear();
        for ( const TradeCompletion& completion : _listCompletionScratch )
        {
            if ( completion._snapshot._state == TradeState::Settled && completion._ledger._listHolderBalance.empty() == false )
                _mapTradeToLedger[completion._snapshot._tradeId] = completion._ledger;
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
            vector<TradeBalance> listBalance;
            collectBalances( completion._ledger, call._accountId, listBalance );
            BitWriter reply;
            TradeReplyWire::writeReply( reply, completion._result, completion._snapshot, listBalance );
            (void)host.respondOk( call._token, reply );
        }
        _listUpdateScratch.clear();
        _pTradeService->drainUpdates( _listUpdateScratch );
        for ( const TradeSnapshot& snapshot : _listUpdateScratch )
            pushSnapshot( host, snapshot );
    }

    void TradeServer::onAccountLeft( OnlineServiceHost& host, AccountId accountId )
    {
        (void)host;
        _pTradeService->closeForAccount( accountId, TradeCloseReason::PartyLeft, _nowMs );
    }

    void TradeServer::pushSnapshot( OnlineServiceHost& host, const TradeSnapshot& snapshot )
    {
        uint16 kind = TradeMethod::kPushUpdate;
        if ( snapshot._state == TradeState::Invited )
            kind = TradeMethod::kPushInvited;
        else if ( snapshot.isClosed() )
            kind = TradeMethod::kPushClosed;
        const auto ledgerIt = _mapTradeToLedger.find( snapshot._tradeId );
        for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
        {
            if ( kind == TradeMethod::kPushInvited && sideIndex == 0 )
                continue; // 신청한 쪽은 응답으로 안다
            const AccountId      accountId = snapshot._arrSide[sideIndex]._accountId;
            vector<TradeBalance> listBalance;
            if ( ledgerIt != _mapTradeToLedger.end() )
                collectBalances( ledgerIt->second, accountId, listBalance );
            BitWriter body;
            TradeReplyWire::writeReply( body, TradeResult::Ok, snapshot, listBalance );
            if ( host.sendPush( accountId, kind, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( accountId, kind, body ); // 상대가 다른 서버에 붙어 있다
        }
    }

    void TradeServer::respondImmediately( OnlineServiceHost& host, const NetRequestToken& token, TradeResult result )
    {
        BitWriter reply;
        TradeReplyWire::writeReply( reply, result, TradeSnapshot{}, vector<TradeBalance>{} );
        (void)host.respondOk( token, reply );
    }

    void TradeServer::collectBalances( const LedgerTransferOutcome& ledger, AccountId accountId, vector<TradeBalance>& outListBalance )
    {
        for ( const LedgerTransferOutcome::HolderBalance& holderBalance : ledger._listHolderBalance )
        {
            if ( holderBalance._holder._kind == LedgerHolderKind::Account && holderBalance._holder._accountId == accountId )
                outListBalance.push_back( TradeBalance{ holderBalance._balance._assetId, holderBalance._balance._amount } );
        }
    }
} // namespace sw
