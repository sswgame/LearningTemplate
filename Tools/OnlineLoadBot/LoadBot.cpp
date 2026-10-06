#include "pch.h"

#include "OnlineLoadBot/LoadBot.h"

#include "Core/Common/Defines.h"
#include "Core/Common/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/formatString.h"

#include "OnlineLoadBot/LoadBotRunner.h"

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

            static string makeNumberText( int64 value )
            {
                utf8 arrBuffer[constant::kMaxBuffer32];
                formatstring( arrBuffer, constant::kMaxBuffer32, "%#", value );
                return string( arrBuffer );
            }

            static void replaceAll( string& inoutText, string_view pattern, const string& replacement )
            {
                size_t position = inoutText.find( pattern.data(), 0, pattern.size() );
                while ( position != string::npos )
                {
                    inoutText.replace( position, pattern.size(), replacement );
                    position = inoutText.find( pattern.data(), position + replacement.size(), pattern.size() );
                }
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
        , _random{ static_cast<uint32>( LoadBotInternal::makeBotKey( pRunner->getScenario()._seed, botIndex ) ) }
        , _listFrame{}
        , _listAccountReply{}
        , _listChatMessage{}
        , _listSocialNotification{}
        , _listMatchAssignment{}
        , _listPartySnapshot{}
        , _listPartyInvite{}
        , _listLobbySnapshot{}
        , _pRunner{ pRunner }
        , _connection{}
        , _accountId{ kInvalidAccountId }
        , _waitUntilMs{ 0 }
        , _requestStartUs{ 0 }
        , _botIndex{ botIndex }
        , _sendSequence{ 0 }
        , _pendingAccountOperation{ AccountClientOperation::Login }
        , _pendingAction{ LoadBotAction::Wait }
        , _awaitKind{ AwaitKind::None }
        , _bPasswordLogin{ SW_FALSE }
        , _bConnecting{ SW_FALSE }
        , _bConnectionOpen{ SW_FALSE }
        , _bFinished{ SW_FALSE }
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
        const bool bRegistered = _client.registerClientService( &_accountClient ) && _client.registerClientService( &_directoryClient ) &&
                                 _client.registerClientService( &_chatClient ) && _client.registerClientService( &_socialClient ) &&
                                 _client.registerClientService( &_leaderboardClient ) && _client.registerClientService( &_matchmakingClient ) &&
                                 _client.registerClientService( &_liveOpsClient );
        if ( bRegistered == false )
            SW_LOG_ERROR( "Load bot %# could not register its kit clients - two kits claim one method range", botIndex );
        _listFrame.push_back( Frame{ &pRunner->getScenario()._listStep, 0, 1 } );
    }

    LoadBot::~LoadBot() { shutdown(); }

    void LoadBot::start()
    {
        _connection  = _pRunner->connectBot( _botIndex );
        _bConnecting = _connection.isValid() ? SW_TRUE : SW_FALSE;
        if ( _connection.isValid() == false )
            _bFinished = SW_TRUE;
    }

    void LoadBot::shutdown()
    {
        _bFinished = SW_TRUE; // 내리는 동안 끝나는 요청은 지표에 적지 않는다
        _client.shutdown();
    }

    void LoadBot::stop()
    {
        _bFinished = SW_TRUE;
        if ( _bConnectionOpen == SW_TRUE )
            _pRunner->closeBot( _connection );
    }

    void LoadBot::handleOpened( StreamMessageEndpoint* pEndpoint, NetRequestClient* pRequestClient, StreamConnectionHandle handle )
    {
        _bConnecting     = SW_FALSE;
        _bConnectionOpen = SW_TRUE;
        OnlineServiceClientSettings settings;
        settings._gameBuild = LoadBotInternal::kGameBuild;
        if ( _client.initializeOnSharedEndpoint( pEndpoint, pRequestClient, handle, settings ) == false )
            stop();
    }

    void LoadBot::handleFrame( StreamFrameKind kind, const uint8* pBody, int32 bodySize ) { _client.handleFrame( kind, pBody, bodySize ); }

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

    bool LoadBot::tick( int64 nowMs )
    {
        if ( _bFinished == SW_TRUE )
            return false;
        if ( _bConnecting == SW_TRUE )
            return true; // 열림 · 닫힘을 기다린다
        if ( _bConnectionOpen == SW_TRUE )
            _client.tick( nowMs );
        drainNotifications();
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
            if ( ++frame._stepIndex < static_cast<int32>( frame._pListStep->size() ) )
                return;
            if ( --frame._repeatRemaining > 0 )
            {
                frame._stepIndex = 0; // 반복 — 처음부터
                return;
            }
            _listFrame.pop_back(); // 이 반복이 끝났다 — 고리가 바깥 프레임의 번호를 올린다(맨 바깥이면 비어 다 돈 것이다)
        }
    }

    void LoadBot::startStep( const LoadBotStep& step, int64 nowMs )
    {
        _pendingAction  = step._action;
        _requestStartUs = _pRunner->getNowUs();
        switch ( step._action )
        {
            case LoadBotAction::Wait:
            {
                const int32 waitMs = _random.nextInt( static_cast<int32>( step._minMs ), static_cast<int32>( step._maxMs ) );
                _waitUntilMs       = nowMs + waitMs;
                advance();
                return;
            }
            case LoadBotAction::Repeat:
            {
                _listFrame.push_back( Frame{ &step._listStep, 0, step._count } );
                return;
            }
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
            case LoadBotAction::Logout:
            {
                _awaitKind               = AwaitKind::Response;
                _pendingAccountOperation = AccountClientOperation::Logout;
                (void)_accountClient.logout();
                return;
            }
            case LoadBotAction::SocialFriendRandom:
            {
                const LoadBot* pOther = _pRunner->findBot( _random.nextInt( 0, _pRunner->getBotCount() - 1 ) );
                if ( pOther == nullptr || pOther == this || pOther->getAccountId() == kInvalidAccountId )
                {
                    advance(); // 고를 상대가 아직 없다 — 건너뛴다(지표에 적지 않는다)
                    return;
                }
                _awaitKind = AwaitKind::Response;
                (void)_socialClient.requestFriend( pOther->getAccountId(), SocialReplyDelegate::create<&LoadBot::onSocialReply>( this ) );
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
            _pendingAccountOperation = AccountClientOperation::Register; // 처음이면 가입 — 이름이 있으면 그대로 로그인
            const string loginName   = "bot" + LoadBotInternal::makeNumberText( _pRunner->getScenario()._seed ) + "_" + LoadBotInternal::makeNumberText( _botIndex );
            (void)_accountClient.registerAccount( loginName, LoadBotInternal::kPassword );
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
        _pendingAccountOperation = AccountClientOperation::GuestLogin;
        (void)_accountClient.guestLogin( arrSecret );
    }

    bool LoadBot::startRemoteStep( const LoadBotStep& step )
    {
        const string& region = _pRunner->getScenario()._region;
        switch ( step._action )
        {
            case LoadBotAction::DirectoryStatus:
            {
                (void)_directoryClient.requestStatus( ServerDirectoryClient::StatusDelegate::create<&LoadBot::onDirectoryStatus>( this ) );
                return true;
            }
            case LoadBotAction::DirectoryAssign:
            {
                ServerAssignmentRequest request;
                request._kind   = step._text.empty() ? string( LoadBotInternal::kDefaultKind ) : step._text;
                request._region = region;
                (void)_directoryClient.requestAssignment( request, ServerDirectoryClient::AssignmentDelegate::create<&LoadBot::onDirectoryAssignment>( this ) );
                return true;
            }
            case LoadBotAction::ChatJoin:
            {
                (void)_chatClient.join( step._text, ChatClient::ReplyDelegate::create<&LoadBot::onChatReply>( this ) );
                return true;
            }
            case LoadBotAction::ChatSend:
            {
                (void)_chatClient.send( step._text, expandText( step._secondText ), ChatClient::ReplyDelegate::create<&LoadBot::onChatReply>( this ) );
                return true;
            }
            case LoadBotAction::ChatHistory:
            {
                (void)_chatClient.requestHistory( step._text, string_view{}, step._count, ChatClient::ReplyDelegate::create<&LoadBot::onChatReply>( this ) );
                return true;
            }
            case LoadBotAction::SocialPresence:
            {
                (void)_socialClient.setPresence( LoadBotInternal::findStatus( step._text ), step._secondText,
                                                 SocialReplyDelegate::create<&LoadBot::onSocialReply>( this ) );
                return true;
            }
            case LoadBotAction::LeaderboardTop:
            {
                (void)_leaderboardClient.requestTop( step._text, 0, step._count, LeaderboardReplyDelegate::create<&LoadBot::onLeaderboardReply>( this ) );
                return true;
            }
            case LoadBotAction::LeaderboardAround:
            {
                (void)_leaderboardClient.requestAround( step._text, step._count, LeaderboardReplyDelegate::create<&LoadBot::onLeaderboardReply>( this ) );
                return true;
            }
            case LoadBotAction::MatchmakingQueue:
            {
                (void)_matchmakingClient.joinQueue( step._text, region, MatchmakingClient::ReplyDelegate::create<&LoadBot::onMatchmakingReply>( this ) );
                return true;
            }
            case LoadBotAction::PartyCreate:
            {
                // 요청 id 는 쓰지 않는다 — 결과는 onMatchmakingReply 로 온다
                (void)_matchmakingClient.createParty( MatchmakingClient::ReplyDelegate::create<&LoadBot::onMatchmakingReply>( this ) );
                return true;
            }
            case LoadBotAction::LiveOpsState:
            {
                (void)_liveOpsClient.requestLiveState( region, LoadBotInternal::kLiveOpsBuild, LiveOpsClient::ReplyDelegate::create<&LoadBot::onLiveOpsReply>( this ) );
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    void LoadBot::finishStep( string_view errorKey )
    {
        _pRunner->getMutableMetrics().recordLatency( _pendingAction, _pRunner->getNowUs() - _requestStartUs, errorKey );
        _awaitKind = AwaitKind::None;
        advance();
    }

    void LoadBot::finishWithResult( uint16 errorCode, uint8 result )
    {
        if ( _bFinished == SW_TRUE || _awaitKind != AwaitKind::Response )
            return; // 내리는 중 · 늦게 온 응답
        if ( errorCode != 0 )
        {
            finishStep( "code" + LoadBotInternal::makeNumberText( errorCode ) );
            return;
        }
        if ( result != 0 )
        {
            finishStep( string( toString( _pendingAction ) ) + ".result" + LoadBotInternal::makeNumberText( result ) );
            return;
        }
        finishStep( string_view{} );
    }

    string LoadBot::expandText( const string& text )
    {
        string expanded = text;
        LoadBotInternal::replaceAll( expanded, LoadBotInternal::kBotPlaceholder, LoadBotInternal::makeNumberText( _botIndex ) );
        LoadBotInternal::replaceAll( expanded, LoadBotInternal::kSequencePlaceholder, LoadBotInternal::makeNumberText( _sendSequence ) );
        ++_sendSequence;
        return expanded;
    }

    void LoadBot::drainNotifications()
    {
        _listChatMessage.clear();
        _chatClient.drainMessages( _listChatMessage );
        _listPartySnapshot.clear();
        _matchmakingClient.drainPartyUpdates( _listPartySnapshot );
        _listPartyInvite.clear();
        _matchmakingClient.drainInvites( _listPartyInvite );
        _listLobbySnapshot.clear();
        _matchmakingClient.drainLobbyUpdates( _listLobbySnapshot );
        _listSocialNotification.clear();
        _socialClient.drainNotifications( _listSocialNotification );
        for ( const SocialNotification& notification : _listSocialNotification )
        {
            if ( notification._kind == SocialNotificationKind::FriendRequested ) // 받은 신청은 바로 수락한다(재지 않는다)
                (void)_socialClient.respondFriend( notification._otherId, true, SocialReplyDelegate::create<&LoadBot::onFriendAccepted>( this ) );
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
                    finishWithResult( reply._errorCode, static_cast<uint8>( reply._result ) );
                }
                continue;
            }
            if ( _awaitKind != AwaitKind::Response || reply._operation != _pendingAccountOperation )
                continue;
            if ( reply._operation == AccountClientOperation::Register )
            {
                _pendingAccountOperation = AccountClientOperation::Login; // 가입 결과(이미 있음 포함)와 상관없이 로그인
                const string loginName   = "bot" + LoadBotInternal::makeNumberText( _pRunner->getScenario()._seed ) + "_" + LoadBotInternal::makeNumberText( _botIndex );
                (void)_accountClient.login( loginName, LoadBotInternal::kPassword );
                continue;
            }
            const bool bLoggedIn = reply._errorCode == 0 && reply._result == LoginResult::Ok &&
                                   ( reply._operation == AccountClientOperation::Login || reply._operation == AccountClientOperation::GuestLogin );
            if ( bLoggedIn )
                _accountId = reply._grant._identity._accountId;
            finishWithResult( reply._errorCode, static_cast<uint8>( reply._result ) );
        }
    }

    void LoadBot::pollMatchResult( int64 nowMs )
    {
        _listMatchAssignment.clear();
        _matchmakingClient.drainMatchResults( _listMatchAssignment );
        if ( _listMatchAssignment.empty() == false )
        {
            const MatchAssignment& assignment = _listMatchAssignment.front();
            if ( assignment._outcome == MatchQueueOutcome::Found )
            {
                _pRunner->getMutableMetrics().recordMatch( assignment._matchId );
                finishStep( string_view{} );
                return;
            }
            finishStep( "wait_match.outcome" + LoadBotInternal::makeNumberText( static_cast<int64>( assignment._outcome ) ) );
            return;
        }
        if ( nowMs >= _waitUntilMs )
            finishStep( "wait_match.timeout" );
    }

    void LoadBot::onDirectoryStatus( uint16 errorCode, const ServerDirectoryStatus& status )
    {
        (void)status;
        finishWithResult( errorCode, 0 );
    }

    void LoadBot::onDirectoryAssignment( uint16 errorCode, const ServerAssignment& assignment )
    {
        (void)assignment;
        finishWithResult( errorCode, 0 );
    }

    void LoadBot::onFriendAccepted( const SocialClientReply& reply ) { (void)reply; }
} // namespace sw
