#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeServer.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/OnlinePushRelay.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"

namespace sw
{
    TradeServer::TradeServer()
        : _mapCallToAccount{}
        , _mapTradeToLedger{}
        , _listCompletionScratch{}
        , _listUpdateScratch{}
        , _listFound{}
        , _listFoundScratch{}
        , _pTradeService{ nullptr }
        , _pDirectory{ nullptr }
        , _pBalanceNotification{ nullptr }
        , _presenceFinds{}
        , _nowMs{ 0 }
    {
        setReady( false );
    }

    void TradeServer::initialize( TradeService* pTradeService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence, MetricRegistry* pMetricRegistry )
    {
        _pTradeService = pTradeService;
        _pDirectory    = pDirectory;
        _presenceFinds.bind( pPresence );
        initializeCalls( pMetricRegistry, "trade" );
        setReady( pTradeService != nullptr );
    }

    void TradeServer::shutdown()
    {
        _calls.clear();
        _presenceFinds.unbind(); // 맡긴 이름 찾기를 회수한다(호출 표를 비운 뒤라 취소 완료의 실패 답은 버려진다). 접속 상태 관리자가 이 객체보다 오래 살아도 이 객체를 부르지 않게 하려는 것이다
        _mapCallToAccount.clear();
        _listFound.clear();
        _pTradeService = nullptr;
        setReady( false );
    }

    uint16 TradeServer::checkRequest( OnlineServiceHost& host, const OnlineCallContext& context )
    {
        _nowMs                      = context._nowMs;
        const RemoteConfig* pConfig = host.getRemoteConfig();
        return pConfig != nullptr && pConfig->isFeatureEnabled( kFeatureFlag, context._accountID, true ) == false ? OnlineError::kFeatureDisabled : OnlineError::kOk;
    }

    void TradeServer::handleCall( OnlineServerCall<TradeMethod::Invite> call, const OnlineCallContext& context, const TradeInviteRequest& request )
    {
        AccountIdentity peer;
        if ( _pDirectory != nullptr && _pDirectory->findIdentityByDisplayName( request._peerDisplayName, peer ) )
        {
            _mapCallToAccount[call._callID] = context._accountID;
            _pTradeService->invite( context._accountID, peer._accountID, context._nowMs, call._callID );
            return;
        }
        const PendingLookup lookup{ call._callID, context._accountID, context._nowMs };
        const bool          bFinding = _presenceFinds.findByDisplayName( request._peerDisplayName, context._accountID, [this, lookup]( const AccountPresenceResult& found, PendingRequestEnd end )
                 {
            if ( end == PendingRequestEnd::Cancelled )
                (void)_calls.fail( lookup._callID, OnlineError::kUnavailable ); // 신청한 계정이 떠났다 — 호출을 표에 남기지 않는다. 닫힌 연결의 답은 호스트가 버린다
            else
                _listFound.push_back( FoundLookup{ lookup, found._identity._accountID } );
        } );
        if ( bFinding == false )
            respondImmediately( call._callID, TradeResult::PeerOffline ); // 서버가 한 대이고 이 프로세스에 없다
    }

    void TradeServer::handleCall( OnlineServerCall<TradeMethod::Respond> call, const OnlineCallContext& context, const TradeRespondRequest& request )
    {
        _mapCallToAccount[call._callID] = context._accountID;
        _pTradeService->respondInvite( context._accountID, request._tradeID, request._bAccept == SW_TRUE, context._nowMs, call._callID );
    }

    void TradeServer::handleCall( OnlineServerCall<TradeMethod::SetOffer> call, const OnlineCallContext& context, const TradeOfferRequest& request )
    {
        _mapCallToAccount[call._callID] = context._accountID;
        _pTradeService->setOffer( context._accountID, request._tradeID, request._listLeg, context._nowMs, call._callID );
    }

    void TradeServer::handleCall( OnlineServerCall<TradeMethod::Lock> call, const OnlineCallContext& context, const TradeIDRequest& request )
    {
        _mapCallToAccount[call._callID] = context._accountID;
        _pTradeService->lock( context._accountID, request._tradeID, context._nowMs, call._callID );
    }

    void TradeServer::handleCall( OnlineServerCall<TradeMethod::Confirm> call, const OnlineCallContext& context, const TradeConfirmRequest& request )
    {
        _mapCallToAccount[call._callID] = context._accountID;
        _pTradeService->confirm( context._accountID, request._tradeID, request._seenOwnRevision, request._seenPeerRevision, context._nowMs, call._callID );
    }

    void TradeServer::handleCall( OnlineServerCall<TradeMethod::Cancel> call, const OnlineCallContext& context, const TradeIDRequest& request )
    {
        _mapCallToAccount[call._callID] = context._accountID;
        _pTradeService->cancel( context._accountID, request._tradeID, context._nowMs, call._callID );
    }

    void TradeServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        _nowMs = nowMs;
        if ( _pTradeService == nullptr )
            return;
        _calls.publishPendingCount();
        _listFoundScratch.clear();
        _listFoundScratch.swap( _listFound );
        for ( const FoundLookup& found : _listFoundScratch )
        {
            const PendingLookup& lookup = found._lookup;
            if ( found._peerID == kInvalidAccountID )
            {
                respondImmediately( lookup._callID, TradeResult::PeerOffline );
                continue;
            }
            _mapCallToAccount[lookup._callID] = lookup._accountID;
            _pTradeService->invite( lookup._accountID, found._peerID, lookup._nowMs, lookup._callID );
        }
        _pTradeService->update( nowMs );
        _listCompletionScratch.clear();
        _pTradeService->takeCompletions( _listCompletionScratch );
        _mapTradeToLedger.clear();
        for ( const TradeCompletion& completion : _listCompletionScratch )
        {
            if ( completion._snapshot._state == TradeState::Settled && completion._ledger._listHolderBalance.empty() == false )
            {
                _mapTradeToLedger[completion._snapshot._tradeID] = completion._ledger;
                notifySettledBalances( completion._ledger );
            }
            const auto callIt = _mapCallToAccount.find( completion._requestTag );
            if ( callIt == _mapCallToAccount.end() )
                continue;
            const AccountID accountID = callIt->second;
            _mapCallToAccount.erase( callIt );
            TradeReply reply;
            reply._result   = completion._result._result;
            reply._snapshot = completion._snapshot;
            collectBalances( completion._ledger, accountID, reply._listBalance );
            (void)finishCall( completion._requestTag, completion._result, reply ); // 규칙 밖 값 · 일시 장애는 공통 범주로 끝난다
        }
        _listUpdateScratch.clear();
        _pTradeService->pollUpdates( _listUpdateScratch );
        for ( const TradeSnapshot& snapshot : _listUpdateScratch )
        {
            pushSnapshot( host, snapshot );
        }
    }

    void TradeServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        if ( _pTradeService == nullptr )
            return;
        // 이름 찾기를 기다리던 신청도 함께 거둔다 — 찾기가 뒤에 끝나면 떠난 계정이 신청한 거래가 열려 아무도 닫지 않는다(초대 시한까지 양쪽이 "거래 중" 이다).
        _presenceFinds.cancelOwnedBy( accountID ); // 답을 기다리는 찾기 — 취소 완료가 호출을 실패로 끝낸다
        for ( auto foundIt = _listFound.begin(); foundIt != _listFound.end(); )
        {
            if ( foundIt->_lookup._accountID != accountID )
            {
                ++foundIt;
                continue;
            }
            (void)_calls.fail( foundIt->_lookup._callID, OnlineError::kUnavailable ); // 답했지만 아직 잇지 않은 신청도 거둔다
            foundIt = _listFound.erase( foundIt );
        }
        _pTradeService->closeForAccount( accountID, TradeCloseReason::PartyLeft, _nowMs );
    }

    void TradeServer::pushSnapshot( OnlineServiceHost& host, const TradeSnapshot& snapshot )
    {
        uint16 kind = TradeMethod::Update::kKind;
        if ( snapshot._state == TradeState::Invited )
            kind = TradeMethod::Invited::kKind;
        else if ( snapshot.isClosed() )
            kind = TradeMethod::Closed::kKind;
        const auto ledgerIt = _mapTradeToLedger.find( snapshot._tradeID );
        for ( int32 sideIndex = 0; sideIndex < 2; ++sideIndex )
        {
            if ( kind == TradeMethod::Invited::kKind && sideIndex == 0 )
                continue; // 신청한 쪽은 응답으로 알게 된다
            const AccountID   accountID = snapshot._arrSide[sideIndex]._accountID;
            TradeNotification push;
            push._snapshot = snapshot;
            if ( ledgerIt != _mapTradeToLedger.end() )
                collectBalances( ledgerIt->second, accountID, push._listBalance );
            BitWriter body;
            if ( TradeSessionRecord::MessageTypes::write( body, push ) == false )
                continue; // write 가 범위 밖 값을 로그에 남겼다
            if ( host.sendPush( accountID, kind, body ) == false && _presenceFinds.hasPresence() )
                (void)_presenceFinds.getPresence()->sendRemotePush( accountID, static_cast<TradePushName>( kind - TradeMethod::kFirstPush ), push ); // 상대가 다른 서버에 접속해 있다
        }
    }

    void TradeServer::respondImmediately( uint64 callID, TradeServiceResult result )
    {
        TradeReply reply;
        reply._result = result._result;
        (void)finishCall( callID, result, reply );
    }

    void TradeServer::notifySettledBalances( const LedgerTransferResult& ledger )
    {
        if ( _pBalanceNotification == nullptr )
            return;
        unordered_map<AccountID, vector<LedgerBalance>> mapAccountToBalance;
        for ( const LedgerTransferResult::HolderBalance& holderBalance : ledger._listHolderBalance )
        {
            if ( holderBalance._holder._kind == LedgerHolderKind::Account )
                mapAccountToBalance[holderBalance._holder._accountID].push_back( holderBalance._balance );
        }
        for ( const auto& [accountID, listBalance] : mapAccountToBalance )
        {
            _pBalanceNotification->notifyBalancesChanged( accountID, listBalance ); // 지갑은 서버의 잔액 변경 알림으로 맞는다 — 게임이 정산 잔액을 옮기지 않는다
        }
    }

    void TradeServer::collectBalances( const LedgerTransferResult& ledger, AccountID accountID, vector<TradeBalance>& outListBalance )
    {
        for ( const LedgerTransferResult::HolderBalance& holderBalance : ledger._listHolderBalance )
        {
            if ( holderBalance._holder._kind == LedgerHolderKind::Account && holderBalance._holder._accountID == accountID )
                outListBalance.push_back( TradeBalance{ holderBalance._balance._assetID, holderBalance._balance._amount } );
        }
    }
} // namespace sw

namespace sw
{
    // 다른 서버에 접속한 계정에게 가는 알림이다. 받는 서버가 이 항목으로 자기 빌드의 번호와 B 를 만든다(OnlinePushRelay.h).
    const OnlinePushRelayEntry kTradeInvitedRelay{ "Trade.Invited", TradeMethod{}, TradePushName::Invited, TradeSessionRecord::MessageTypes{}, OnlinePushBody<TradeNotification>{} };
    const OnlinePushRelayEntry kTradeUpdateRelay{ "Trade.Update", TradeMethod{}, TradePushName::Update, TradeSessionRecord::MessageTypes{}, OnlinePushBody<TradeNotification>{} };
    const OnlinePushRelayEntry kTradeClosedRelay{ "Trade.Closed", TradeMethod{}, TradePushName::Closed, TradeSessionRecord::MessageTypes{}, OnlinePushBody<TradeNotification>{} };
} // namespace sw
