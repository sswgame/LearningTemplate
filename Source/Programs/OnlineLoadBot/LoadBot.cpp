#include "pch.h"

#include "Programs/OnlineLoadBot/LoadBot.h"

#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Programs/OnlineLoadBot/LoadBotRunner.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct LoadBotInternal
        {
            struct StatusName
            {
                SocialPresenceStatus _status;
                string_view          _name;
            };

            static constexpr StatusName kArrStatusName[] = {
                { SocialPresenceStatus::Online,  "online"},
                {   SocialPresenceStatus::Away,    "away"},
                {   SocialPresenceStatus::Busy,    "busy"},
                { SocialPresenceStatus::InGame, "in_game"},
                {SocialPresenceStatus::Offline, "offline"},
            };

            static constexpr string_view kGameBuild           = "loadbot";
            static constexpr string_view kClientBuild         = "1.0.0";
            static constexpr string_view kClientPlatform      = "loadbot";
            static constexpr string_view kPassword            = "loadbot-password";
            static constexpr string_view kDefaultKind         = "game";
            static constexpr uint32      kLiveOpsBuild        = 1;
            static constexpr int32       kSecretWordCount     = LoginConstant::kDeviceSecretSize / static_cast<int32>( sizeof( uint64 ) );
            static constexpr string_view kBotPlaceholder      = "{bot}";
            static constexpr string_view kSequencePlaceholder = "{seq}";
            static constexpr string_view kFakeStoreName       = "fake"; ///< 개발 서버의 테스트 공급자(`FakeReceiptValidator`) — 실제 상점을 부르지 않는다
            static constexpr int32       kPurchaseCount       = 1;

            /** @brief 봇마다 다른 씨앗 — 시나리오 씨앗과 봇 번호를 섞는다(같은 씨앗 → 같은 난수열). */
            static uint64 makeBotKey( uint32 seed, int32 botIndex ) { return HashUtil::mix64( ( static_cast<uint64>( seed ) << 32 ) | static_cast<uint32>( botIndex ) ); }

            static SocialPresenceStatus findStatus( string_view name )
            {
                for ( const StatusName& entry : kArrStatusName )
                {
                    if ( entry._name == name )
                        return entry._status;
                }
                return SocialPresenceStatus::Online;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoadBot::LoadBot( LoadBotRunner* pRunner, int32 botIndex )
        : _client{}
        , _accountClient{}
        , _directoryClient{}
        , _chatClient{}
        , _socialClient{}
        , _leaderboardClient{}
        , _matchmakingClient{}
        , _liveOpsClient{}
        , _economyClient{}
        , _mailboxClient{}
        , _tradeClient{}
        , _random{ static_cast<uint32>( LoadBotInternal::makeBotKey( pRunner->getScenario()._seed, botIndex ) ) }
        , _schedule{ pRunner->getScenario()._pacing }
        , _listFrame{}
        , _listAccountReply{}
        , _listChatMessage{}
        , _listSocialNotification{}
        , _listMatchAssignment{}
        , _listPartySnapshot{}
        , _listPartyInvite{}
        , _listLobbySnapshot{}
        , _listNewMail{}
        , _listTradeReply{}
        , _listTradeUpdate{}
        , _listTradeLeg{}
        , _displayName{}
        , _pRunner{ pRunner }
        , _connection{}
        , _accountID{ kInvalidAccountID }
        , _tradeID{ 0 }
        , _waitUntilMs{ 0 }
        , _requestStartUs{ 0 }
        , _botIndex{ botIndex }
        , _sendSequence{ 0 }
        , _pendingAccountOperation{ AccountMethodName::Login }
        , _pendingAction{ LoadBotAction::Wait }
        , _awaitKind{ AwaitKind::None }
        , _bPasswordLogin{ SW_FALSE }
        , _bConnecting{ SW_FALSE }
        , _bConnectionOpen{ SW_FALSE }
        , _bFinished{ SW_FALSE }
        , _bTradeCallPending{ SW_FALSE }
        , _bTradeInvited{ SW_FALSE }
    {
        AccountClientInfo clientInfo;
        clientInfo._build    = LoadBotInternal::kClientBuild;
        clientInfo._platform = LoadBotInternal::kClientPlatform;
        _accountClient.initialize( &_client, clientInfo );
        _directoryClient.initialize( &_client );
        _chatClient.initialize( &_client );
        _socialClient.initialize( &_client );
        _leaderboardClient.initialize( &_client );
        _matchmakingClient.initialize( &_client );
        _liveOpsClient.initialize( &_client );
        _economyClient.initialize( &_client );
        _mailboxClient.initialize( &_client );
        _tradeClient.initialize( &_client );
        const bool bClaimed = _chatClient.claimMessages( _chatLease ) && _socialClient.claimNotifications( _socialLease ) && _matchmakingClient.claimNotifications( _matchmakingLease ) &&
                              _mailboxClient.claimNewMail( _mailboxLease );
        if ( bClaimed == false )
            SW_LOG_ERROR_ID( "LoadBot.LoadBotCouldNot", "Load bot %# could not claim its notification buffers", botIndex );
        const bool bRegistered = _client.registerClientService( &_accountClient ) && _client.registerClientService( &_directoryClient ) &&
                                 _client.registerClientService( &_chatClient ) && _client.registerClientService( &_socialClient ) &&
                                 _client.registerClientService( &_leaderboardClient ) && _client.registerClientService( &_matchmakingClient ) &&
                                 _client.registerClientService( &_liveOpsClient ) && _client.registerClientService( &_economyClient ) &&
                                 _client.registerClientService( &_mailboxClient ) && _client.registerClientService( &_tradeClient );
        if ( bRegistered == false )
            SW_LOG_ERROR_ID( "LoadBot.LoadBotCouldNot2", "Load bot %# could not register its kit clients - two kits claim one method range", botIndex );
        _listFrame.push_back( Frame{ &pRunner->getScenario()._listStep, 0, 1 } );
    }

    LoadBot::~LoadBot()
    {
        shutdown();
    }

    void LoadBot::start()
    {
        _schedule.start( _pRunner->getNowUs() );
        _connection  = _pRunner->connectBot( _botIndex );
        _bConnecting = _connection.isValid() ? SW_TRUE : SW_FALSE;
        if ( _connection.isValid() == false )
            _bFinished = SW_TRUE;
    }

    void LoadBot::shutdown()
    {
        _bFinished = SW_TRUE; // 내리는 동안 끝나는 요청은 지표에 적지 않는다
        _gameSession.stop();
        _client.shutdown();
    }

    void LoadBot::stop()
    {
        _bFinished = SW_TRUE;
        _gameSession.stop();
        if ( _bConnectionOpen == SW_TRUE )
            _pRunner->closeBot( _connection );
    }

    void LoadBot::handleOpened( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle )
    {
        _bConnecting     = SW_FALSE;
        _bConnectionOpen = SW_TRUE;
        OnlineServiceClientConfig settings{ OnlineClientSecurity::sharedEndpoint(), OnlineRetryPolicy::none() }; // TLS 는 실행기의 공유 엔드포인트가 정한다. 다시 보내지 않는다 — 봇은 kBusy · kRateLimited 비율을 잰다
        settings._gameBuild = LoadBotInternal::kGameBuild;
        if ( _client.initializeOnSharedEndpoint( pEndpoint, pRequestClient, handle, settings ) == false )
            stop();
    }

    void LoadBot::handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize )
    {
        _client.handleFrame( kind, pBody, bodySize );
    }

    void LoadBot::handleClosed( StreamCloseReason reason )
    {
        _bConnecting     = SW_FALSE;
        _bConnectionOpen = SW_FALSE;
        _client.handleClosed( reason );
        _client.shutdown(); // 다시 연결하면 새 연결로 다시 올린다(Hello — 계정 클라이언트는 토큰으로 재접속)
        if ( _awaitKind == AwaitKind::Closed )
        {
            finishStep( string_view{} );
            return;
        }
        if ( _bFinished == SW_FALSE && _awaitKind != AwaitKind::Ready )
            _bFinished = SW_TRUE; // 시나리오가 끊지 않았는데 끊겼다(로그아웃 뒤 · 서버가 닫음) — 더 돌 수 없다
    }

    bool LoadBot::update( MonotonicMilliseconds nowMs )
    {
        if ( _bFinished == SW_TRUE )
            return false;
        if ( _bConnecting == SW_TRUE )
            return true; // 열림 · 닫힘을 기다린다
        if ( _gameSession.getState() != LoadBotGameSession::State::Idle )
            _gameSession.update( static_cast<float64>( _pRunner->getNowUs() ) * 1.0e-6 ); // 서비스 연결과 따로 사는 게임 UDP 세션도 같은 스레드에서 펌프한다
        if ( _bConnectionOpen == SW_TRUE )
            _client.update( nowMs );
        pollNotifications();
        pollAccountReplies();
        switch ( _awaitKind )
        {
            case AwaitKind::Match:
            {
                pollMatchResult( nowMs );
                return _bFinished == SW_FALSE;
            }
            case AwaitKind::Ready:
            {
                if ( _client.isReady() && _accountClient.getToken().isEmpty() )
                    finishStep( string_view{} ); // 토큰이 없으면 재접속할 것이 없다 — Hello 로 끝
                return _bFinished == SW_FALSE;
            }
            case AwaitKind::Game:
            {
                pollGameSession();
                return _bFinished == SW_FALSE;
            }
            case AwaitKind::Trade:
            {
                pollTrade( nowMs );
                return _bFinished == SW_FALSE;
            }
            case AwaitKind::Response:
            case AwaitKind::Closed:
            {
                return true;
            }
            case AwaitKind::None:
            {
                break;
            }
        }
        if ( nowMs < _waitUntilMs )
            return true;
        const LoadBotStep* pStep = findCurrentStep();
        if ( pStep == nullptr )
        {
            stop(); // 다 돌았다
            return false;
        }
        if ( pStep->_action != LoadBotAction::Reconnect )
        {
            if ( _bConnectionOpen == SW_FALSE )
            {
                _bFinished = SW_TRUE; // 연결을 잃었다(열리지 못함)
                return false;
            }
            if ( _client.isReady() == false )
                return true; // Hello 를 기다린다
        }
        startStep( *pStep, nowMs );
        return _bFinished == SW_FALSE;
    }

    const LoadBotStep* LoadBot::findCurrentStep() const
    {
        if ( _listFrame.empty() )
            return nullptr;
        const Frame& frame = _listFrame.back();
        if ( frame._stepIndex >= static_cast<int32>( frame._pListStep->size() ) )
            return nullptr;
        return &( *frame._pListStep )[static_cast<size_t>( frame._stepIndex )];
    }

    void LoadBot::advance()
    {
        while ( _listFrame.empty() == false )
        {
            Frame& frame = _listFrame.back();
            if ( frame._bSingle == SW_FALSE && ++frame._stepIndex < static_cast<int32>( frame._pListStep->size() ) )
                return; // `pick` 이 고른 단계(하나만)는 다음 단계로 가지 않고 끝난다
            if ( --frame._repeatRemaining > 0 )
            {
                frame._stepIndex = 0; // 반복 — 처음부터
                return;
            }
            _listFrame.pop_back(); // 이 반복이 끝났다 — 고리가 바깥 프레임의 번호를 올린다(맨 바깥이면 비어 다 돈 것이다)
        }
    }

    void LoadBot::startStep( const LoadBotStep& step, MonotonicMilliseconds nowMs )
    {
        _pendingAction    = step._action;
        const int64 nowUs = _pRunner->getNowUs();
        if ( step._action == LoadBotAction::Wait )
        {
            const int32 waitMs = _random.nextInt( static_cast<int32>( step._minMs ), static_cast<int32>( step._maxMs ) );
            const int64 wakeUs = _schedule.addWait( static_cast<int64>( waitMs ) * constant::time::kMicrosecondsPerMillisecond, nowUs );
            _waitUntilMs       = LoadBotRunner::kRunStart + wakeUs / constant::time::kMicrosecondsPerMillisecond; // 열린 루프에서 이미 지났으면 다음 틱에 바로 보낸다
            advance();
            return;
        }
        if ( step._action == LoadBotAction::Repeat )
        {
            _listFrame.push_back( Frame{ &step._listStep, 0, step._count, SW_FALSE } ); // 단계를 묶기만 한다 — 시각표를 건드리지 않는다
            return;
        }
        if ( step._action == LoadBotAction::Pick )
        {
            startPick( step ); // 고르기만 한다 — 시각표를 건드리지 않는다
            return;
        }
        _requestStartUs = _schedule.beginStep( nowUs );
        switch ( step._action )
        {
            case LoadBotAction::WaitMatch:
            {
                _awaitKind   = AwaitKind::Match;
                _waitUntilMs = nowMs + step._minMs;
                return;
            }
            case LoadBotAction::Disconnect:
            {
                _awaitKind = AwaitKind::Closed;
                _pRunner->closeBot( _connection );
                return;
            }
            case LoadBotAction::GameConnect:
            {
                const NetAddress& gameAddress = _pRunner->getSettings()._gameAddress;
                if ( gameAddress.isValid() == false )
                {
                    finishStep( "game.noaddress" );
                    return;
                }
                if ( _gameSession.start( gameAddress ) == false )
                {
                    finishStep( "game.open" );
                    return;
                }
                _awaitKind = AwaitKind::Game;
                return;
            }
            case LoadBotAction::GameDisconnect:
            {
                _gameSession.stop();
                finishStep( string_view{} );
                return;
            }
            case LoadBotAction::Reconnect:
            {
                if ( _bConnectionOpen == SW_TRUE )
                {
                    finishStep( string_view{} ); // 이미 붙어 있다
                    return;
                }
                _awaitKind   = AwaitKind::Ready;
                _connection  = _pRunner->connectBot( _botIndex );
                _bConnecting = _connection.isValid() ? SW_TRUE : SW_FALSE;
                if ( _connection.isValid() == false )
                {
                    finishStep( "reconnect.failed" );
                    _bFinished = SW_TRUE;
                }
                return;
            }
            case LoadBotAction::Login:
            {
                startLogin( step );
                return;
            }
            case LoadBotAction::Trade:
            {
                startTrade( step, nowMs );
                return;
            }
            case LoadBotAction::Logout:
            {
                _awaitKind               = AwaitKind::Response;
                _pendingAccountOperation = AccountMethodName::Logout;
                (void)_accountClient.logout();
                return;
            }
            case LoadBotAction::SocialFriendRandom:
            {
                const LoadBot* pOther           = _pRunner->findBot( _random.nextInt( 0, _pRunner->getBotCount() - 1 ) );
                const bool     bNoPeerAvailable = pOther == nullptr || pOther == this || pOther->getAccountID() == kInvalidAccountID;
                if ( bNoPeerAvailable )
                {
                    advance(); // 고를 상대가 아직 없다 — 건너뛴다(지표에 적지 않는다)
                    return;
                }
                _awaitKind = AwaitKind::Response;
                callKit<SocialMethod::RequestFriend>( _socialClient, SocialAccountRequest{ pOther->getAccountID() } );
                return;
            }
            default:
            {
                _awaitKind = AwaitKind::Response; // 응답이 그 자리에서 올 수 있다 — 보내기 전에
                if ( startRemoteStep( step ) == false )
                    finishStep( "unsupported" );
                return;
            }
        }
    }

    void LoadBot::startLogin( const LoadBotStep& step )
    {
        _awaitKind      = AwaitKind::Response;
        _bPasswordLogin = step._text == "password" ? SW_TRUE : SW_FALSE;
        if ( _bPasswordLogin == SW_TRUE )
        {
            _pendingAccountOperation = AccountMethodName::Register;                              // 처음이면 가입 — 이름이 있으면 그대로 로그인
            (void)_accountClient.registerAccount( makeLoginName(), LoadBotInternal::kPassword ); // 요청 id 는 쓰지 않는다 — 결과는 답(완료)으로 받는다
            return;
        }
        // 게스트 — 장치 비밀은 씨앗 · 봇 번호에서 정해진다(같은 시나리오를 다시 돌리면 같은 계정)
        uint8        arrSecret[LoginConstant::kDeviceSecretSize];
        const uint64 botKey = LoadBotInternal::makeBotKey( _pRunner->getScenario()._seed, _botIndex );
        for ( int32 wordIndex = 0; wordIndex < LoadBotInternal::kSecretWordCount; ++wordIndex )
        {
            const uint64 word = HashUtil::mix64( botKey + HashUtil::kGoldenRatio64 * static_cast<uint64>( wordIndex + 1 ) );
            Memory::copy( arrSecret + wordIndex * static_cast<int32>( sizeof( uint64 ) ), &word, sizeof( uint64 ) );
        }
        _pendingAccountOperation = AccountMethodName::GuestLogin;
        (void)_accountClient.guestLogin( arrSecret );
    }

    bool LoadBot::startRemoteStep( const LoadBotStep& step )
    {
        const string& region = _pRunner->getScenario()._region;
        switch ( step._action )
        {
            case LoadBotAction::DirectoryStatus:
            {
                (void)_directoryClient.call<ServerDirectoryMethod::GetStatus>( SW_DELEGATE_METHOD( OnlineReplyDelegate<ServerDirectoryMethod::GetStatus>, &LoadBot::onDirectoryStatus, this ) );
                return true;
            }
            case LoadBotAction::DirectoryAssign:
            {
                ServerAssignmentRequest request;
                request._kind   = step._text.empty() ? string( LoadBotInternal::kDefaultKind ) : step._text;
                request._region = region;
                (void)_directoryClient.call<ServerDirectoryMethod::AssignServer>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<ServerDirectoryMethod::AssignServer>, &LoadBot::onDirectoryAssignment, this ) );
                return true;
            }
            case LoadBotAction::ChatJoin:
            {
                ChatChannelRequest request;
                request._channelID = step._text;
                callKit<ChatMethod::Join>( _chatClient, request );
                return true;
            }
            case LoadBotAction::ChatSend:
            {
                ChatSendRequest request;
                request._channelID = step._text;
                request._text      = expandText( step._secondText );
                callKit<ChatMethod::Send>( _chatClient, request );
                return true;
            }
            case LoadBotAction::ChatHistory:
            {
                ChatHistoryRequest request;
                request._channelID = step._text;
                request._maxCount  = step._count;
                callKit<ChatMethod::History>( _chatClient, request );
                return true;
            }
            case LoadBotAction::SocialPresence:
            {
                SocialPresenceRequest request;
                request._status = LoadBotInternal::findStatus( step._text );
                request._text   = step._secondText;
                callKit<SocialMethod::SetPresence>( _socialClient, request );
                return true;
            }
            case LoadBotAction::LeaderboardTop:
            {
                LeaderboardTopRequest request;
                request._boardID = step._text;
                request._count   = step._count;
                callKit<LeaderboardMethod::GetTop>( _leaderboardClient, request );
                return true;
            }
            case LoadBotAction::LeaderboardAround:
            {
                LeaderboardAroundRequest request;
                request._boardID = step._text;
                request._radius  = step._count;
                callKit<LeaderboardMethod::GetAround>( _leaderboardClient, request );
                return true;
            }
            case LoadBotAction::MatchmakingQueue:
            {
                MatchmakingModeRequest request;
                request._modeID = step._text;
                request._region = region;
                callKit<MatchmakingMethod::QueueJoin>( _matchmakingClient, request );
                return true;
            }
            case LoadBotAction::PartyCreate:
            {
                callKit<MatchmakingMethod::PartyCreate>( _matchmakingClient, OnlineNoBody{} ); // 결과는 onKitReply 로 온다
                return true;
            }
            case LoadBotAction::LiveOpsState:
            {
                (void)_liveOpsClient.requestLiveState( region, LoadBotInternal::kLiveOpsBuild, SW_DELEGATE_METHOD( OnlineReplyDelegate<LiveOpsMethod::GetLiveState>, &LoadBot::onKitReply<LiveOpsStateReply>, this ) );
                return true;
            }
            case LoadBotAction::EconomyWallet:
            {
                callKit<EconomyMethod::GetWallet>( _economyClient, OnlineNoBody{} );
                return true;
            }
            case LoadBotAction::EconomyPurchase:
            {
                EconomyPurchaseRequest request;
                request._offerID = step._text;
                request._count   = LoadBotInternal::kPurchaseCount;
                callKit<EconomyMethod::Purchase>( _economyClient, request );
                return true;
            }
            case LoadBotAction::EconomyRedeem:
            {
                EconomyRedeemRequest request;
                request._storeName = string( LoadBotInternal::kFakeStoreName );
                request._payload   = makeReceiptPayload( step._text );
                callKit<EconomyMethod::RedeemReceipt>( _economyClient, request );
                return true;
            }
            case LoadBotAction::MailboxClaimAll:
            {
                callKit<MailboxMethod::ClaimAll>( _mailboxClient, OnlineNoBody{} );
                return true;
            }
            case LoadBotAction::GuildCreate:
            {
                SocialTextRequest request;
                request._text = makeGuildName();
                (void)_socialClient.call<SocialMethod::GuildCreate>( request, SW_DELEGATE_METHOD( OnlineReplyDelegate<SocialMethod::GuildCreate>, &LoadBot::onGuildCreated, this ) );
                return true;
            }
            case LoadBotAction::GuildDeposit:
            {
                GuildAssetRequest request;
                request._assetID = step._text;
                request._amount  = step._amount;
                callKit<SocialMethod::GuildDeposit>( _socialClient, request );
                return true;
            }
            case LoadBotAction::GuildWithdraw:
            {
                GuildAssetRequest request; // 맡긴 사람을 비우면 공용 창고(이전 가능 자산) — 길드장인 봇이 꺼낸다
                request._assetID = step._text;
                request._amount  = step._amount;
                callKit<SocialMethod::GuildWithdraw>( _socialClient, request );
                return true;
            }
            case LoadBotAction::LeaderboardSubmit:
            {
                LeaderboardSubmitRequest request;
                request._boardID = step._text;
                request._score   = step._amount;
                callKit<LeaderboardMethod::SubmitScore>( _leaderboardClient, request );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    void LoadBot::pollGameSession()
    {
        switch ( _gameSession.getState() )
        {
            case LoadBotGameSession::State::Connected:
            {
                finishStep( string_view{} );
                return;
            }
            case LoadBotGameSession::State::Failed:
            {
                fixed_string<constant::buffer::kMax64> errorKey;
                formatstring( errorKey.data(), errorKey.capacity(), "game.%#", toString( _gameSession.getFailReason() ) );
                finishStep( errorKey.view() );
                return;
            }
            case LoadBotGameSession::State::Idle:
            case LoadBotGameSession::State::Connecting:
            {
                return;
            }
        }
    }

    void LoadBot::startPick( const LoadBotStep& step )
    {
        int32 totalWeight = 0;
        for ( const LoadBotStep& child : step._listStep )
        {
            totalWeight += child._weight;
        }
        int32 roll = _random.nextInt( 0, totalWeight - 1 ); // 읽기가 자식마다 `_weight` > 0 을 지켰다
        for ( size_t childIndex = 0; childIndex < step._listStep.size(); ++childIndex )
        {
            if ( roll < step._listStep[childIndex]._weight )
            {
                _listFrame.push_back( Frame{ &step._listStep, static_cast<int32>( childIndex ), 1, SW_TRUE } );
                return;
            }
            roll -= step._listStep[childIndex]._weight;
        }
    }

    void LoadBot::startTrade( const LoadBotStep& step, MonotonicMilliseconds nowMs )
    {
        if ( _pRunner->findBot( _botIndex ^ 1 ) == nullptr )
        {
            advance(); // 짝이 없다(홀수 마지막 봇) — 건너뛴다(지표에 적지 않는다)
            return;
        }
        _listTradeReply.clear();
        (void)_tradeClient.pollReplies( _listTradeReply ); // 지난 거래의 늦은 답(취소 등)을 버린다
        _listTradeLeg.clear();
        if ( ( _botIndex & 1 ) == 0 )
        {
            TradeLeg& leg = _listTradeLeg.emplace_back(); // 신청하는 쪽만 내놓는다 — 받는 쪽은 빈 제시로 잠근다
            leg._assetID  = step._text;
            leg._amount   = step._amount;
        }
        _tradeID           = 0;
        _bTradeCallPending = SW_FALSE;
        _bTradeInvited     = SW_FALSE;
        _awaitKind         = AwaitKind::Trade;
        _waitUntilMs       = nowMs + step._minMs;
    }

    void LoadBot::pollTrade( MonotonicMilliseconds nowMs )
    {
        _listTradeReply.clear();
        (void)_tradeClient.pollReplies( _listTradeReply );
        for ( const TradeClientReply& reply : _listTradeReply )
        {
            _bTradeCallPending = SW_FALSE;
            if ( reply._errorCode != 0 || reply._result != TradeResult::Ok )
            {
                if ( _tradeID != 0 )
                    (void)_tradeClient.cancel( _tradeID ); // 짝이 시한까지 기다리지 않게 닫는다
                fixed_string<constant::buffer::kMax64> errorKey;
                if ( reply._errorCode != 0 )
                    formatstring( errorKey.data(), errorKey.capacity(), "code%#", reply._errorCode );
                else
                    formatstring( errorKey.data(), errorKey.capacity(), "trade.result%#", static_cast<int64>( reply._result ) );
                finishStep( errorKey.view() );
                return;
            }
            if ( _tradeID == 0 )
                _tradeID = reply._snapshot._tradeID;
        }
        const TradeSnapshot&  snapshot  = _tradeClient.getSnapshot();
        const optional<int32> sideIndex = snapshot.findSideIndex( _accountID );
        const bool            bInvited  = _tradeID == 0 && sideIndex.has_value() && *sideIndex == 1 && snapshot._state == TradeState::Invited;
        if ( bInvited )
            _tradeID = snapshot._tradeID; // 받는 쪽 — 신청 알림이 스냅샷에 들어왔다
        const bool bThisTrade = _tradeID != 0 && snapshot._tradeID == _tradeID && sideIndex.has_value();
        if ( bThisTrade && snapshot.isClosed() )
        {
            fixed_string<constant::buffer::kMax64> errorKey;
            if ( snapshot._state != TradeState::Settled )
                formatstring( errorKey.data(), errorKey.capacity(), "trade.closed%#", static_cast<int64>( snapshot._closeReason ) );
            finishStep( errorKey.view() );
            return;
        }
        if ( nowMs >= _waitUntilMs )
        {
            if ( _tradeID != 0 )
                (void)_tradeClient.cancel( _tradeID );
            finishStep( "trade.timeout" );
            return;
        }
        if ( _bTradeCallPending == SW_TRUE )
            return;
        const bool bInviter = ( _botIndex & 1 ) == 0;
        if ( _tradeID == 0 )
        {
            const LoadBot* pPeer        = bInviter && _bTradeInvited == SW_FALSE ? _pRunner->findBot( _botIndex ^ 1 ) : nullptr;
            const bool     bPeerWaiting = pPeer != nullptr && pPeer->isWaitingTradeInvite() && pPeer->getDisplayName().empty() == false;
            if ( bPeerWaiting == false )
                return; // 받는 쪽은 신청을, 신청하는 쪽은 짝이 거래 단계에 오기를 기다린다
            _bTradeInvited     = SW_TRUE;
            _bTradeCallPending = SW_TRUE;
            (void)_tradeClient.invite( pPeer->getDisplayName() );
            return;
        }
        if ( bThisTrade == false )
            return; // 이 거래의 스냅샷이 아직 오지 않았다
        if ( snapshot._state == TradeState::Invited )
        {
            if ( *sideIndex == 1 )
            {
                _bTradeCallPending = SW_TRUE;
                (void)_tradeClient.respond( _tradeID, true );
            }
            return;
        }
        // 열린 거래 — 스냅샷마다 다음 명령 하나. 짝이 제시를 바꾸면 잠금이 풀린 스냅샷이 와서 다시 잠근다
        const TradeSide& own  = snapshot._arrSide[*sideIndex];
        const TradeSide& peer = snapshot._arrSide[1 - *sideIndex];
        if ( _listTradeLeg.empty() == false && own._offerRevision == 0 )
        {
            _bTradeCallPending = SW_TRUE;
            (void)_tradeClient.setOffer( _tradeID, _listTradeLeg );
        }
        else if ( own._bLocked == SW_FALSE )
        {
            _bTradeCallPending = SW_TRUE;
            (void)_tradeClient.lock( _tradeID );
        }
        else if ( peer._bLocked == SW_TRUE && own._bConfirmed == SW_FALSE )
        {
            _bTradeCallPending = SW_TRUE;
            (void)_tradeClient.confirm( _tradeID ); // 지금 스냅샷의 두 제시 버전으로 확정한다
        }
    }

    string LoadBot::makeLoginName() const
    {
        StringBuilder<constant::buffer::kMax64> name;
        name.appendFormat( "bot%#_%#", _pRunner->getScenario()._seed, _botIndex );
        return string( name.view() );
    }

    string LoadBot::makeGuildName() const
    {
        StringBuilder<constant::buffer::kMax64> name;
        name.appendFormat( "lb%#_%#", _pRunner->getScenario()._seed, _botIndex );
        return string( name.view() );
    }

    string LoadBot::makeReceiptPayload( const string& productID )
    {
        // 가짜 스토어 영수증 `fake|<상품>|<거래 id>|ok` — 거래 id 는 실행마다(`getRunID`) · 봇마다 · 순번마다 달라 같은 계정이 다시 돌려도 새 지급이다
        StringBuilder<constant::buffer::kMax256> payload;
        payload.appendFormat( "%#|%#|lb%#.%#.%#.%#|ok", LoadBotInternal::kFakeStoreName, productID, _pRunner->getRunID(), _pRunner->getScenario()._seed, _botIndex, _sendSequence );
        ++_sendSequence;
        return string( payload.view() );
    }

    void LoadBot::finishStep( string_view errorKey )
    {
        _pRunner->getMutableMetrics().recordLatency( _pendingAction, _pRunner->getNowUs() - _requestStartUs, errorKey );
        _awaitKind   = AwaitKind::None;
        _waitUntilMs = LoadBotRunner::kRunStart; // 시한(`wait_match` · `trade`)을 쓴 단계가 일찍 끝나도 다음 단계가 그 시한까지 기다리지 않는다
        advance();
    }

    void LoadBot::finishWithResult( uint16 errorCode, uint8 result )
    {
        if ( _bFinished == SW_TRUE || _awaitKind != AwaitKind::Response )
            return; // 내리는 중 · 늦게 온 응답
        if ( errorCode != 0 )
        {
            fixed_string<constant::buffer::kMax64> errorKey;
            formatstring( errorKey.data(), errorKey.capacity(), "code%#", errorCode );
            finishStep( errorKey.view() );
            return;
        }
        if ( result != 0 )
        {
            fixed_string<constant::buffer::kMax64> errorKey;
            formatstring( errorKey.data(), errorKey.capacity(), "%#.result%#", toString( _pendingAction ), static_cast<int32>( result ) );
            finishStep( errorKey.view() );
            return;
        }
        finishStep( string_view{} );
    }

    string LoadBot::expandText( const string& text )
    {
        string expanded = StringUtil::replace( text, LoadBotInternal::kBotPlaceholder, to_string( _botIndex ) );
        expanded        = StringUtil::replace( expanded, LoadBotInternal::kSequencePlaceholder, to_string( _sendSequence ) );
        ++_sendSequence;
        return expanded;
    }

    void LoadBot::pollNotifications()
    {
        _listChatMessage.clear();
        _chatClient.pollMessages( _chatLease, _listChatMessage );
        _listPartySnapshot.clear();
        _matchmakingClient.pollPartyUpdates( _matchmakingLease, _listPartySnapshot );
        _listPartyInvite.clear();
        _matchmakingClient.pollInvites( _matchmakingLease, _listPartyInvite );
        _listLobbySnapshot.clear();
        _matchmakingClient.pollLobbyUpdates( _matchmakingLease, _listLobbySnapshot );
        _listNewMail.clear();
        _mailboxClient.pollNewMail( _mailboxLease, _listNewMail ); // 새 우편은 `mailbox_claim_all` 이 받는다 — 알림은 비우기만 한다
        _listTradeUpdate.clear();
        (void)_tradeClient.pollUpdates( _listTradeUpdate ); // 거래 단계는 클라이언트의 스냅샷을 본다 — 알림은 비우기만 한다
        _listSocialNotification.clear();
        _socialClient.pollNotifications( _socialLease, _listSocialNotification );
        for ( const SocialNotification& notification : _listSocialNotification )
        {
            if ( notification._kind == SocialNotificationKind::FriendRequested ) // 받은 신청은 바로 수락한다(측정하지 않는다)
                (void)_socialClient.call<SocialMethod::RespondFriend>( SocialRespondRequest{ notification._otherID, SW_TRUE }, SW_DELEGATE_METHOD( OnlineReplyDelegate<SocialMethod::RespondFriend>, &LoadBot::onFriendAccepted, this ) );
        }
    }

    void LoadBot::pollAccountReplies()
    {
        _listAccountReply.clear();
        (void)_accountClient.pollReplies( _listAccountReply );
        for ( const AccountClientReply& reply : _listAccountReply )
        {
            if ( reply._bAutomatic == SW_TRUE )
            {
                if ( _awaitKind == AwaitKind::Ready ) // 다시 연결 — 재접속 응답까지
                {
                    _awaitKind = AwaitKind::Response;
                    finishWithResult( reply._errorCode, static_cast<uint8>( reply._result._result ) );
                }
                continue;
            }
            if ( _awaitKind != AwaitKind::Response || reply._operation != _pendingAccountOperation )
                continue;
            if ( reply._operation == AccountMethodName::Register )
            {
                _pendingAccountOperation = AccountMethodName::Login; // 가입 결과(이미 있음 포함)와 상관없이 로그인
                (void)_accountClient.login( makeLoginName(), LoadBotInternal::kPassword );
                continue;
            }
            const bool bLoggedIn = reply._errorCode == 0 && reply._result == LoginResult::Ok &&
                                   ( reply._operation == AccountMethodName::Login || reply._operation == AccountMethodName::GuestLogin );
            if ( bLoggedIn )
            {
                _accountID   = reply._grant._identity._accountID;
                _displayName = reply._grant._identity._displayName;
                _tradeClient.setAccountID( _accountID );
            }
            finishWithResult( reply._errorCode, static_cast<uint8>( reply._result._result ) );
        }
    }

    void LoadBot::pollMatchResult( MonotonicMilliseconds nowMs )
    {
        _listMatchAssignment.clear();
        _matchmakingClient.pollMatchResults( _matchmakingLease, _listMatchAssignment );
        if ( _listMatchAssignment.empty() == false )
        {
            const MatchAssignment& assignment = _listMatchAssignment.front();
            if ( assignment._result == MatchQueueResult::Found )
            {
                _pRunner->getMutableMetrics().recordMatch( assignment._matchID );
                finishStep( string_view{} );
                return;
            }
            fixed_string<constant::buffer::kMax64> errorKey;
            formatstring( errorKey.data(), errorKey.capacity(), "wait_match.outcome%#", static_cast<int64>( assignment._result ) );
            finishStep( errorKey.view() );
            return;
        }
        if ( nowMs >= _waitUntilMs )
            finishStep( "wait_match.timeout" );
    }

    void LoadBot::onDirectoryStatus( const OnlineResult<ServerDirectoryStatus>& result )
    {
        finishWithResult( result._errorCode, 0 );
    }

    void LoadBot::onDirectoryAssignment( const OnlineResult<ServerAssignment>& result )
    {
        finishWithResult( result._errorCode, 0 );
    }

    void LoadBot::onGuildCreated( const OnlineResult<GuildInfoReply>& result )
    {
        // 같은 씨앗으로 다시 돌리면(저장소가 남는 PostgreSQL) 게스트 계정이 같아 이미 길드에 있다 — 창고 단계가 그 길드를 쓴다
        const bool bAlreadyIn = result._errorCode == 0 && result._reply._result == SocialResult::AlreadyInGuild;
        finishWithResult( result._errorCode, bAlreadyIn ? uint8{ 0 } : static_cast<uint8>( result._reply._result ) );
    }

    void LoadBot::onFriendAccepted( const OnlineResult<SocialLinkReply>& reply )
    {
        (void)reply;
    }
} // namespace sw
