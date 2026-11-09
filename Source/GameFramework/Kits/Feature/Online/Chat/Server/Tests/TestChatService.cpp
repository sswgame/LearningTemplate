// ChatServiceTest — 채팅 로직(메모리 저장소 + 프로세스 안 버스): 채널 들어가기 · 나오기 · 구독, 말하기 전달 · 되울림, 제재 첫 읽기와 대기, 도배 · 금칙어, 차단, 귓속말(이 서버 · 다른 서버),
// 버스로 온 메시지, 기록 쓰기 · 읽기 · 커서 · 보존, 신고. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatService.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Runtime/Online/Bus/Server/LocalServerBus.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Report/Server/ServiceReportStore.h"
#include "GameFramework/Runtime/Online/Sanction/Server/ServiceSanction.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID   kAlice   = 11;
    constexpr AccountID   kBob     = 12;
    constexpr AccountID   kCarol   = 13;
    constexpr int64       kNowMs   = 1700000000000ll;
    constexpr uint64      kServerA = 1;
    constexpr uint64      kServerB = 2;
    constexpr const utf8* kWorld   = "world.kr";

    /** @brief 접속한 계정을 손으로 넣는 디렉터리입니다. */
    class FakeDirectory final : public IAccountDirectory
    {
    public:
        vector<AccountID> _listOnline{};

        bool findIdentity( AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            if ( isAccountOnline( accountID ) == false )
                return false;
            outIdentity._accountID   = accountID;
            outIdentity._displayName = "user" + to_string( accountID );
            return true;
        }
        bool findIdentityByDisplayName( string_view /*displayName*/, AccountIdentity& /*outIdentity*/ ) const override { return false; }
        bool isAccountOnline( AccountID accountID ) const override { return VectorUtil::contains( _listOnline, accountID ); }
    };

    /** @brief (받는 사람, 보낸 사람) 차단 쌍입니다. */
    class FakePolicy final : public IChatPolicy
    {
    public:
        vector<pair<AccountID, AccountID>> _listBlocked{};

        bool isBlocked( AccountID recipientID, AccountID senderID ) const override { return VectorUtil::contains( _listBlocked, make_pair( recipientID, senderID ) ); }
    };

    /** @brief 찾기를 쌓아 두고 손으로 답하는 접속 상태입니다. */
    class FakePresence final : public IAccountPresence
    {
    public:
        struct Find
        {
            AccountPresenceDelegate _onFound{};
            uint64                  _requestID{ 0 };
        };

        vector<Find> _listFind{};
        uint64       _nextRequestID{ 1 };

        uint64 submitFindByDisplayName( string_view /*displayName*/, const AccountPresenceDelegate& /*onFound*/, const Subscription& /*owner*/ ) override { return 0; }
        uint64 submitFindByAccount( AccountID /*accountID*/, const AccountPresenceDelegate& onFound, const Subscription& /*owner*/ ) override
        {
            _listFind.push_back( Find{ onFound, _nextRequestID } );
            return _nextRequestID++;
        }
        bool relayRemotePush( const OnlinePushRelay& /*relay*/ ) override { return false; }

        void answer( uint64 serverID )
        {
            const Find find = _listFind.front();
            _listFind.erase( _listFind.begin() );
            AccountPresenceResult result;
            result._requestID = find._requestID;
            result._serverID  = serverID;
            find._onFound( result );
        }

    protected:
        void releaseSubscription( uint64 /*id*/ ) override {}
    };

    /** @brief 서버 하나입니다. 영속 데이터와 버스 허브는 빌립니다. */
    struct ChatFixture
    {
        MemoryServiceStore         _store;
        FakeDirectory              _directory;
        FakePolicy                 _policy;
        FakePresence               _presence;
        unique_ptr<LocalServerBus> _transport;
        unique_ptr<ServerBus>      _bus;
        ChatService                _service;
        uint64                     _nextTag{ 1 };

        ChatFixture( MemoryServiceDatabase& database, LocalServerBusHub* pHub, uint64 serverID, ChatServiceConfig settings = ChatServiceConfig{} )
            : _store{ &database }
            , _directory{}
            , _policy{}
            , _presence{}
            , _transport{}
            , _bus{}
            , _service{}
        {
            ChatServiceDependencies dependencies;
            dependencies._pStore     = &_store;
            dependencies._pDirectory = &_directory;
            dependencies._pPolicy    = &_policy;
            dependencies._serverID   = serverID;
            if ( pHub != nullptr )
            {
                _transport              = make_unique<LocalServerBus>( pHub, serverID );
                _bus                    = make_unique<ServerBus>( *_transport );
                dependencies._pBus      = _bus.get();
                dependencies._pPresence = &_presence;
            }
            settings._ignoredCharactersPath = FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Server/chat_ignored_characters.txt" );
            SW_EXPECT_TRUE( _service.initialize( dependencies, settings ) );
            _directory._listOnline = vector<AccountID>{ kAlice, kBob, kCarol };
        }

        void pump( int64 nowMs = kNowMs )
        {
            while ( true )
            {
                int32 work = _store.pollCompletions();
                if ( _transport != nullptr )
                {
                    vector<ChatBusTopicChange> listChange;
                    _service.pollBusTopicChanges( listChange );
                    for ( const ChatBusTopicChange& change : listChange )
                    {
                        if ( change._bSubscribe == SW_TRUE )
                            _transport->subscribe( change._topic );
                        else
                            _transport->unsubscribe( change._topic );
                    }
                    vector<ServerBusMessage> listMessage;
                    work += _transport->pollMessages( listMessage );
                    for ( const ServerBusMessage& message : listMessage )
                    {
                        _service.handleBusMessage( message );
                    }
                }
                if ( work == 0 )
                    break;
            }
            (void)nowMs;
        }

        /** @brief 저장 시간이 된 기록을 씁니다. */
        void flush( int64 nowMs = kNowMs )
        {
            _service.update( UnixMilliseconds{ nowMs } );
            pump();
        }

        ChatCompletion take( uint64 tag )
        {
            pump();
            vector<ChatCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            if ( listCompletion.empty() )
                return ChatCompletion{};
            SW_EXPECT_EQUAL( tag, listCompletion[0]._requestTag );
            return std::move( listCompletion[0] );
        }

        ChatOutcome join( AccountID accountID, string_view channelID = kWorld )
        {
            const uint64 tag = _nextTag++;
            _service.joinChannel( accountID, channelID, UnixMilliseconds{ kNowMs }, tag );
            return take( tag )._result;
        }

        ChatCompletion send( AccountID accountID, string_view text, string_view channelID = kWorld, int64 nowMs = kNowMs )
        {
            const uint64 tag = _nextTag++;
            _service.sendMessage( accountID, channelID, text, UnixMilliseconds{ nowMs }, tag );
            return take( tag );
        }

        ChatCompletion whisper( AccountID accountID, AccountID recipientID, string_view text )
        {
            const uint64 tag = _nextTag++;
            _service.sendWhisper( accountID, recipientID, text, UnixMilliseconds{ kNowMs }, tag );
            return take( tag );
        }

        ChatCompletion history( AccountID accountID, string_view channelID = kWorld, string_view cursor = "", int32 maxCount = 10 )
        {
            const uint64 tag = _nextTag++;
            _service.readHistory( accountID, channelID, cursor, maxCount, tag );
            return take( tag );
        }

        ChatOutcome report( AccountID accountID, const ChatMessage& message, string_view reason = "abuse", int64 nowMs = kNowMs )
        {
            ChatReportRequest request;
            request._channelID = message._channelID;
            request._reason    = string( reason );
            request._sentMs    = message._sentMs;
            request._serverID  = message._serverID;
            request._sequence  = message._sequence;
            const uint64 tag   = _nextTag++;
            _service.reportMessage( accountID, request, UnixMilliseconds{ nowMs }, tag );
            return take( tag )._result;
        }

        vector<ChatDelivery> takeDeliveries()
        {
            pump();
            vector<ChatDelivery> listDelivery;
            _service.pollDeliveries( listDelivery );
            return listDelivery;
        }
    };

    /** @brief 제재 레코드를 씁니다. */
    struct ChatTestUtil
    {
        static void muteUntil( MemoryServiceDatabase& database, AccountID accountID, int64 untilMs )
        {
            ServiceSanctionState state;
            SW_EXPECT_TRUE( ServiceSanction::readState( database, accountID, state ) == ServiceStoreResult::Ok );
            state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] = untilMs;
            ServiceTransaction transaction;
            ServiceSanction::stageWrite( transaction, accountID, state );
            SW_EXPECT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        static ChatServiceConfig makeOpenSettings()
        {
            ChatServiceConfig settings;
            settings._spam._burstCount     = 1000;
            settings._spam._maxRepeatCount = 1000;
            return settings;
        }
    };
} // namespace

/** @brief [ChatServiceTest] 시작: 저장소 · 디렉터리가 없거나 건너뛸 글자 파일 · 금칙어 파일을 못 읽으면 서지 않는다 — 거르지 않은 채 열리지 않는다 */
SW_TEST_CASE( ChatServiceTest, InitializeNeedsDependenciesAndFiles )
{
    SW_EXPECT_LOG( "ChatService.ChatServiceNeedsServiceStore" );
    SW_EXPECT_LOG( "ChatService.ChatServiceBannedWordFile" );
    SW_EXPECT_LOG( "File.NotFound" );
    MemoryServiceDatabase database;
    MemoryServiceStore    store( &database );
    FakeDirectory         directory;
    ChatService           service;
    SW_EXPECT_FALSE( service.initialize( ChatServiceDependencies{}, ChatServiceConfig{} ) );

    ChatServiceDependencies dependencies;
    dependencies._pStore     = &store;
    dependencies._pDirectory = &directory;
    ChatServiceConfig settings;
    settings._ignoredCharactersPath = FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Server/chat_ignored_characters.txt" );
    settings._bannedWordsPath       = test::makeScratchPath( "missing_banned.txt" );
    SW_EXPECT_FALSE( service.initialize( dependencies, settings ) );
}

/** @brief [ChatServiceTest] 채널: 규칙 밖 id 는 Invalid, 길드 · 파티는 클라이언트가 못 들어간다. 접속하지 않은 계정은 들어갈 수 없고, 16 개까지이며, 나가면 수가 준다 */
SW_TEST_CASE( ChatServiceTest, JoinAndLeaveRules )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA );

    SW_EXPECT_TRUE( rig.join( kAlice, "Bad Channel" ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.join( kAlice, "nope.kr" ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.join( kAlice, "guild.1" ) == ChatResult::NotJoinable );
    SW_EXPECT_TRUE( rig.join( kAlice, "party.1" ) == ChatResult::NotJoinable );
    SW_EXPECT_TRUE( rig.join( 99 ) == ChatResult::TargetOffline );

    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok ); // 두 번 들어와도 한 번이다
    SW_EXPECT_EQUAL( 1, rig._service.getLocalMemberCount( kWorld ) );

    for ( int32 index = 1; index < ChatLimit::kMaxChannelPerMember; ++index )
    {
        SW_REQUIRE_TRUE( rig.join( kAlice, "custom.room" + to_string( index ) ) == ChatResult::Ok );
    }
    SW_EXPECT_TRUE( rig.join( kAlice, "custom.over" ) == ChatResult::TooManyChannels );
    SW_EXPECT_TRUE( rig._service.addMember( kAlice, "whisper.x", UnixMilliseconds{ kNowMs } ).hasError( OnlineError::kInvalidRequest ) ); // 서버 시스템도 귓속말 채널은 못 넣는다

    rig._service.leaveChannel( kAlice, "guild.1", 50 );
    SW_EXPECT_TRUE( rig.take( 50 )._result == ChatResult::NotJoinable );
    rig._service.leaveChannel( kAlice, kWorld, 51 );
    SW_EXPECT_TRUE( rig.take( 51 )._result == ChatResult::Ok );
    SW_EXPECT_EQUAL( 0, rig._service.getLocalMemberCount( kWorld ) );
    SW_EXPECT_TRUE( rig.join( kAlice, "custom.over" ) == ChatResult::Ok );
}

/** @brief [ChatServiceTest] 말하기: 멤버에게 전달하고 되울림과 번호 · 이름 · 시각을 돌려준다. 멤버가 아니면 NotMember, 빈 글 · 긴 글 · 깨진 UTF-8 은 Invalid 다 */
SW_TEST_CASE( ChatServiceTest, SendDeliversToMembers )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA, ChatTestUtil::makeOpenSettings() );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.join( kBob ) == ChatResult::Ok );

    const ChatCompletion sent = rig.send( kAlice, "hello" );
    SW_EXPECT_TRUE( sent._result == ChatResult::Ok );
    SW_EXPECT_EQUAL( ChatMethod::Send::kID, sent._method );
    SW_EXPECT_EQUAL( string( "hello" ), sent._message._text );
    SW_EXPECT_EQUAL( string( "user11" ), sent._message._senderName );
    SW_EXPECT_EQUAL( kNowMs, sent._message._sentMs );
    SW_EXPECT_EQUAL( kServerA, sent._message._serverID );
    SW_EXPECT_TRUE( sent._message._kind == ChatChannelKind::World );

    const vector<ChatDelivery> listDelivery = rig.takeDeliveries();
    SW_REQUIRE_EQUAL( size_t( 2 ), listDelivery.size() ); // 보낸 사람도 받는다 — 다른 창이 같은 채널을 볼 수 있다
    SW_EXPECT_EQUAL( string( "hello" ), listDelivery[1]._message._text );

    SW_EXPECT_TRUE( rig.send( kCarol, "x" )._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( rig.send( 99, "x" )._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( rig.send( kAlice, "x", "custom.none" )._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( rig.send( kAlice, "" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.send( kAlice, string( static_cast<size_t>( ChatLimit::kMaxTextSize ) + 1, 'x' ) )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.send( kAlice, "bad\xFF" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.takeDeliveries().empty() );
}

/** @brief [ChatServiceTest] 채팅 금지: 첫 말하기는 제재를 읽은 뒤 처리해 Muted 와 남은 시간을 돌려주고, 시한이 지나면 말할 수 있다. GM 이 바꾸면 다음 말하기 전에 다시 읽는다 */
SW_TEST_CASE( ChatServiceTest, MuteBlocksSending )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA, ChatTestUtil::makeOpenSettings() );
    ChatTestUtil::muteUntil( database, kAlice, kNowMs + 5000 );
    SW_EXPECT_TRUE( rig._service.addMember( kAlice, kWorld, UnixMilliseconds{ kNowMs } ) == ChatResult::Ok );

    const ChatCompletion muted = rig.send( kAlice, "hi" );
    SW_EXPECT_TRUE( muted._result == ChatResult::Muted );
    SW_EXPECT_EQUAL( int64( 5000 ), muted._retryAfterMs );
    SW_EXPECT_TRUE( rig.takeDeliveries().empty() );
    SW_EXPECT_TRUE( rig.send( kAlice, "hi", kWorld, kNowMs + 5000 )._result == ChatResult::Ok );

    ChatTestUtil::muteUntil( database, kAlice, kNowMs + 90000 );
    SW_EXPECT_TRUE( rig.send( kAlice, "again", kWorld, kNowMs + 5001 )._result == ChatResult::Ok ); // 캐시가 아직 살아 있다
    rig._service.refreshSanction( kAlice );
    SW_EXPECT_TRUE( rig.send( kAlice, "after", kWorld, kNowMs + 5002 )._result == ChatResult::Muted );
}

/** @brief [ChatServiceTest] 제재를 읽는 도중 GM 이 바꾸면 그 읽기(이전 값)를 믿지 않고 다시 읽는다 — 금지 직후 말이 새지 않는다 */
SW_TEST_CASE( ChatServiceTest, RefreshDuringSanctionReadReadsAgain )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA, ChatTestUtil::makeOpenSettings() );
    SW_EXPECT_TRUE( rig._service.addMember( kAlice, kWorld, UnixMilliseconds{ kNowMs } ) == ChatResult::Ok ); // 읽기를 맡겼다. 메모리 저장소는 이미 읽었고 완료만 남았다
    rig._service.sendMessage( kAlice, kWorld, "leak", UnixMilliseconds{ kNowMs }, 7 );

    ChatTestUtil::muteUntil( database, kAlice, kNowMs + 5000 );
    rig._service.refreshSanction( kAlice );
    SW_EXPECT_TRUE( rig.take( 7 )._result == ChatResult::Muted );
    SW_EXPECT_TRUE( rig.takeDeliveries().empty() );
}

/** @brief [ChatServiceTest] 제재를 읽는 동안의 말하기는 상한까지 기다리고, 넘으면 RateLimited 다. 읽기가 실패하면 기다리던 말은 Unavailable 이고, 떠나도 끝을 알린다 */
SW_TEST_CASE( ChatServiceTest, PendingSendsAreBoundedAndFinished )
{
    MemoryServiceDatabase database;
    ChatServiceConfig     settings    = ChatTestUtil::makeOpenSettings();
    settings._maxPendingSendPerMember = 2;
    ChatFixture rig( database, nullptr, kServerA, settings );
    SW_EXPECT_TRUE( rig._service.addMember( kAlice, kWorld, UnixMilliseconds{ kNowMs } ) == ChatResult::Ok );
    rig._service.sendMessage( kAlice, kWorld, "a", UnixMilliseconds{ kNowMs }, 1 );
    rig._service.sendMessage( kAlice, kWorld, "b", UnixMilliseconds{ kNowMs }, 2 );
    rig._service.sendMessage( kAlice, kWorld, "c", UnixMilliseconds{ kNowMs }, 3 );
    vector<ChatCompletion> listCompletion;
    rig._service.takeCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 1 ), listCompletion.size() );
    SW_EXPECT_EQUAL( uint64( 3 ), listCompletion[0]._requestTag );
    SW_EXPECT_TRUE( listCompletion[0]._result == ChatResult::RateLimited );
    SW_EXPECT_EQUAL( settings._pendingSendRetryAfterMs, listCompletion[0]._retryAfterMs );
    rig.pump();
    listCompletion.clear();
    rig._service.takeCompletions( listCompletion );
    SW_EXPECT_EQUAL( size_t( 2 ), listCompletion.size() );

    database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_TRUE( rig._service.addMember( kBob, kWorld, UnixMilliseconds{ kNowMs } ) == ChatResult::Ok ); // 읽기가 진다
    rig._service.sendMessage( kBob, kWorld, "x", UnixMilliseconds{ kNowMs }, 4 );
    SW_EXPECT_TRUE( rig.take( 4 )._result.hasError( OnlineError::kUnavailable ) ); // 막을 수 있는지 모르면 내보내지 않는다

    SW_EXPECT_TRUE( rig._service.addMember( kCarol, kWorld, UnixMilliseconds{ kNowMs } ) == ChatResult::Ok );
    rig._service.sendMessage( kCarol, kWorld, "y", UnixMilliseconds{ kNowMs }, 5 );
    rig._service.removeAccount( kCarol );
    SW_EXPECT_TRUE( rig.take( 5 )._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( 2, rig._service.getLocalMemberCount( kWorld ) );
}

/** @brief [ChatServiceTest] 도배 · 반복은 기다릴 시간과 함께 거절하고, 금칙어는 가려서 나간다. 거절 방식이면 Rejected 다 */
SW_TEST_CASE( ChatServiceTest, SpamAndFilterApplyToSends )
{
    const string bannedPath = test::makeScratchPath( "banned.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( bannedPath, "badword\n" ) );

    MemoryServiceDatabase database;
    ChatServiceConfig     settings;
    settings._spam._burstCount       = 3;
    settings._spam._refillIntervalMs = 1000;
    settings._spam._maxRepeatCount   = 1;
    settings._bannedWordsPath        = bannedPath;
    ChatFixture rig( database, nullptr, kServerA, settings );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );

    const ChatCompletion masked = rig.send( kAlice, "a badword b" );
    SW_EXPECT_TRUE( masked._result == ChatResult::Ok );
    SW_EXPECT_EQUAL( string( "a ******* b" ), masked._message._text );

    SW_EXPECT_TRUE( rig.send( kAlice, "one" )._result == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.send( kAlice, "one" )._result == ChatResult::Repeated );
    SW_EXPECT_TRUE( rig.send( kAlice, "two" )._result == ChatResult::Ok ); // 반복 거절은 몫을 쓰지 않는다
    const ChatCompletion limited = rig.send( kAlice, "three" );
    SW_EXPECT_TRUE( limited._result == ChatResult::RateLimited );
    SW_EXPECT_TRUE( limited._retryAfterMs > 0 );

    MemoryServiceDatabase rejectDatabase;
    settings._filterMode = ChatFilterMode::Reject;
    ChatFixture rejecting( rejectDatabase, nullptr, kServerA, settings );
    SW_EXPECT_TRUE( rejecting.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rejecting.send( kAlice, "a badword b" )._result == ChatResult::Rejected );
    SW_EXPECT_TRUE( rejecting.takeDeliveries().empty() );
}

/** @brief [ChatServiceTest] 차단: 막은 사람의 채널 말은 받는 쪽에게 안 가고, 귓속말은 보낸 사람에게 Ok 로 보이면서 버려진다 — 막았는지 드러내지 않는다 */
SW_TEST_CASE( ChatServiceTest, BlockedSenderIsSilentlyDropped )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA, ChatTestUtil::makeOpenSettings() );
    rig._policy._listBlocked.push_back( make_pair( kBob, kAlice ) );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.join( kBob ) == ChatResult::Ok );

    SW_EXPECT_TRUE( rig.send( kAlice, "hi" )._result == ChatResult::Ok );
    const vector<ChatDelivery> listChannel = rig.takeDeliveries();
    SW_REQUIRE_EQUAL( size_t( 1 ), listChannel.size() );
    SW_EXPECT_EQUAL( kAlice, listChannel[0]._recipientID );

    const ChatCompletion whispered = rig.whisper( kAlice, kBob, "psst" );
    SW_EXPECT_TRUE( whispered._result == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.takeDeliveries().empty() );
}

/** @brief [ChatServiceTest] 귓속말: 자신 · 0 은 Invalid, 상대가 이 서버에 있으면 그 사람에게만 가고 당사자가 기록을 읽는다. 서버가 하나인데 상대가 없으면 TargetOffline 이다 */
SW_TEST_CASE( ChatServiceTest, WhisperOnOneServer )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA, ChatTestUtil::makeOpenSettings() );
    SW_EXPECT_TRUE( rig.whisper( kAlice, kAlice, "x" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.whisper( kAlice, kInvalidAccountID, "x" )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.whisper( kAlice, 99, "x" )._result == ChatResult::TargetOffline );

    const ChatCompletion whispered = rig.whisper( kAlice, kBob, "psst" );
    SW_EXPECT_TRUE( whispered._result == ChatResult::Ok );
    SW_EXPECT_EQUAL( ChatMethod::Whisper::kID, whispered._method );
    SW_EXPECT_TRUE( whispered._message._kind == ChatChannelKind::Whisper );
    const vector<ChatDelivery> listDelivery = rig.takeDeliveries();
    SW_REQUIRE_EQUAL( size_t( 1 ), listDelivery.size() );
    SW_EXPECT_EQUAL( kBob, listDelivery[0]._recipientID );

    rig.flush();
    const string channelID = ChatChannelID::makeWhisper( kAlice, kBob );
    SW_EXPECT_EQUAL( channelID, ChatChannelID::makeWhisper( kBob, kAlice ) );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.history( kBob, channelID )._listHistory.size() );
    SW_EXPECT_TRUE( rig.history( kCarol, channelID )._result == ChatResult::NotMember ); // 제삼자는 읽지 못한다
}

/** @brief [ChatServiceTest] 서버 둘: 채널 말은 버스로 그 채널 멤버가 있는 서버에 간다. 낸 서버는 다시 받지 않고, 멤버가 모두 나가면 구독을 푼다 */
SW_TEST_CASE( ChatServiceTest, ChannelMessagesCrossServers )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatFixture           rigA( database, &hub, kServerA, ChatTestUtil::makeOpenSettings() );
    ChatFixture           rigB( database, &hub, kServerB, ChatTestUtil::makeOpenSettings() );
    SW_EXPECT_TRUE( rigA.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rigB.join( kBob ) == ChatResult::Ok );
    rigA.pump();
    rigB.pump();

    SW_EXPECT_TRUE( rigA.send( kAlice, "hello" )._result == ChatResult::Ok );
    rigA.pump();
    const vector<ChatDelivery> listA = rigA.takeDeliveries();
    const vector<ChatDelivery> listB = rigB.takeDeliveries();
    SW_REQUIRE_EQUAL( size_t( 1 ), listA.size() ); // 자기 서버의 것을 두 번 받지 않는다
    SW_REQUIRE_EQUAL( size_t( 1 ), listB.size() );
    SW_EXPECT_EQUAL( kBob, listB[0]._recipientID );
    SW_EXPECT_EQUAL( string( "hello" ), listB[0]._message._text );

    rigB._service.removeAccount( kBob );
    rigB.pump();
    SW_EXPECT_TRUE( rigA.send( kAlice, "again" )._result == ChatResult::Ok );
    rigA.pump();
    SW_EXPECT_TRUE( rigB.takeDeliveries().empty() );
}

/** @brief [ChatServiceTest] 서버 둘의 귓속말: 접속 상태로 서버를 찾아 그 서버로 보낸다. 없거나 그사이 이 서버를 떠났으면 TargetOffline 이고, 받는 서버의 차단은 보낸 쪽에 드러나지 않는다 */
SW_TEST_CASE( ChatServiceTest, WhisperCrossesServers )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatFixture           rigA( database, &hub, kServerA, ChatTestUtil::makeOpenSettings() );
    ChatFixture           rigB( database, &hub, kServerB, ChatTestUtil::makeOpenSettings() );
    rigA._directory._listOnline = vector<AccountID>{ kAlice };
    rigB._directory._listOnline = vector<AccountID>{ kBob };
    rigA.pump();
    rigB.pump();

    rigA._service.sendWhisper( kAlice, kBob, "psst", UnixMilliseconds{ kNowMs }, 1 );
    rigA.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rigA._presence._listFind.size() );
    SW_EXPECT_EQUAL( 1, rigA._service.getPendingCount() );
    rigA._presence.answer( kServerB );
    SW_EXPECT_TRUE( rigA.take( 1 )._result == ChatResult::Ok );
    const vector<ChatDelivery> listB = rigB.takeDeliveries();
    SW_REQUIRE_EQUAL( size_t( 1 ), listB.size() );
    SW_EXPECT_EQUAL( kBob, listB[0]._recipientID );

    rigA._service.sendWhisper( kAlice, kBob, "gone", UnixMilliseconds{ kNowMs }, 2 );
    rigA.pump();
    rigA._presence.answer( 0 );
    SW_EXPECT_TRUE( rigA.take( 2 )._result == ChatResult::TargetOffline );
    rigA._service.sendWhisper( kAlice, kBob, "self", UnixMilliseconds{ kNowMs }, 3 );
    rigA.pump();
    rigA._presence.answer( kServerA );
    SW_EXPECT_TRUE( rigA.take( 3 )._result == ChatResult::TargetOffline );

    rigB._policy._listBlocked.push_back( make_pair( kBob, kAlice ) );
    rigA._service.sendWhisper( kAlice, kBob, "blocked", UnixMilliseconds{ kNowMs }, 4 );
    rigA.pump();
    rigA._presence.answer( kServerB );
    SW_EXPECT_TRUE( rigA.take( 4 )._result == ChatResult::Ok );
    SW_EXPECT_TRUE( rigB.takeDeliveries().empty() );
}

/** @brief [ChatServiceTest] 다른 서버에서 GM 이 금지하면 버스 알림으로 제재 캐시를 버린다 */
SW_TEST_CASE( ChatServiceTest, SanctionChangeOverBusRefreshes )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatFixture           rig( database, &hub, kServerA, ChatTestUtil::makeOpenSettings() );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.send( kAlice, "before" )._result == ChatResult::Ok );

    ChatTestUtil::muteUntil( database, kAlice, kNowMs + 5000 );
    LocalServerBus         gmTransport( &hub, kServerB );
    ServerBus              gmBus( gmTransport );
    ServiceSanctionChanged changed;
    changed._accountID = kAlice;
    SW_REQUIRE_TRUE( ServiceSanctionBus::kChangedTopic.publish( gmBus, changed ) );
    rig.pump();
    SW_EXPECT_TRUE( rig.send( kAlice, "after" )._result == ChatResult::Muted );
}

/** @brief [ChatServiceTest] 기록: update 가 쓰고, 최근 것부터 읽으며, 커서로 이어 읽고 끝이면 커서가 빈다. 들어가지 않은 채널 · 틀린 요청은 거절한다 */
SW_TEST_CASE( ChatServiceTest, HistoryPagesNewestFirst )
{
    MemoryServiceDatabase database;
    ChatFixture           rig( database, nullptr, kServerA, ChatTestUtil::makeOpenSettings() );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );
    for ( int32 index = 0; index < 5; ++index )
    {
        SW_REQUIRE_TRUE( rig.send( kAlice, "m" + to_string( index ), kWorld, kNowMs + index )._result == ChatResult::Ok );
    }
    SW_EXPECT_EQUAL( 5, rig._service.getQueuedHistoryCount() );
    SW_EXPECT_TRUE( rig.history( kAlice )._listHistory.empty() ); // 아직 쓰지 않았다

    rig.flush();
    SW_EXPECT_EQUAL( 0, rig._service.getQueuedHistoryCount() );
    const ChatCompletion first = rig.history( kAlice, kWorld, "", 3 );
    SW_REQUIRE_EQUAL( size_t( 3 ), first._listHistory.size() );
    SW_EXPECT_EQUAL( string( "m4" ), first._listHistory[0]._text );
    SW_EXPECT_EQUAL( string( "m2" ), first._listHistory[2]._text );
    SW_EXPECT_FALSE( first._nextCursor.empty() );

    const ChatCompletion second = rig.history( kAlice, kWorld, first._nextCursor, 3 );
    SW_REQUIRE_EQUAL( size_t( 2 ), second._listHistory.size() );
    SW_EXPECT_EQUAL( string( "m1" ), second._listHistory[0]._text );
    SW_EXPECT_TRUE( second._nextCursor.empty() );

    SW_EXPECT_TRUE( rig.history( kBob )._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( rig.history( kAlice, kWorld, "", 0 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.history( kAlice, kWorld, "", ChatLimit::kMaxHistoryPage + 1 )._result.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.history( kAlice, "Bad Channel" )._result.hasError( OnlineError::kInvalidRequest ) );
}

/** @brief [ChatServiceTest] 기록 정리: 정리 차례의 메시지가 나오면 보존 기간이 지난 기록을 지운다. 저장소가 실패하면 경고하고 버린다. 큐가 차면 오래된 것부터 버린다 */
SW_TEST_CASE( ChatServiceTest, HistoryTrimsAndDropsWhenItMust )
{
    SW_EXPECT_LOG( "ChatService.ChatServiceChatHistoryRecords" );
    SW_EXPECT_LOG( "ChatService.ChatServiceChatHistoryQueue" );
    MemoryServiceDatabase database;
    ChatServiceConfig     settings = ChatTestUtil::makeOpenSettings();
    settings._historyTrimEvery     = 2;
    settings._historyRetentionMs   = 1000;
    settings._maxQueuedHistory     = 2;
    ChatFixture rig( database, nullptr, kServerA, settings );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );

    SW_EXPECT_TRUE( rig.send( kAlice, "old" )._result == ChatResult::Ok ); // 순번 1
    rig.flush();
    const int64 laterMs = kNowMs + 2000;
    SW_EXPECT_TRUE( rig.send( kAlice, "new", kWorld, laterMs )._result == ChatResult::Ok ); // 순번 2 — 정리 차례다
    rig.flush( laterMs );
    const ChatCompletion trimmed = rig.history( kAlice );
    SW_REQUIRE_EQUAL( size_t( 1 ), trimmed._listHistory.size() );
    SW_EXPECT_EQUAL( string( "new" ), trimmed._listHistory[0]._text );

    database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.send( kAlice, "lost", kWorld, laterMs )._result == ChatResult::Ok );
    rig.flush( laterMs );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.history( kAlice )._listHistory.size() );

    for ( int32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE( rig.send( kAlice, "q" + to_string( index ), kWorld, laterMs + index )._result == ChatResult::Ok );
    }
    SW_EXPECT_EQUAL( 2, rig._service.getQueuedHistoryCount() );
}

/** @brief [ChatServiceTest] 신고: 기록에 있는 메시지와 앞 메시지를 레코드로 남긴다. 아직 쓰지 않았으면 NotFound, 자기 메시지 · 틀린 사유는 Invalid, 들어가지 않은 채널은 NotMember, 많이 내면 RateLimited 다 */
SW_TEST_CASE( ChatServiceTest, ReportCapturesContext )
{
    MemoryServiceDatabase database;
    ChatServiceConfig     settings = ChatTestUtil::makeOpenSettings();
    settings._reportBurst          = 2;
    ChatFixture rig( database, nullptr, kServerA, settings );
    SW_EXPECT_TRUE( rig.join( kAlice ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.join( kBob ) == ChatResult::Ok );

    const ChatMessage before = rig.send( kAlice, "context", kWorld, kNowMs )._message;
    const ChatMessage target = rig.send( kAlice, "abusive", kWorld, kNowMs + 1 )._message;
    const ChatMessage mine   = rig.send( kBob, "mine", kWorld, kNowMs + 2 )._message;
    SW_EXPECT_TRUE( rig.report( kBob, target ) == ChatResult::NotFound ); // 아직 쓰지 않았다 — 도배 몫을 하나 쓴다

    rig.flush();
    SW_EXPECT_TRUE( rig.report( kCarol, target ) == ChatResult::NotMember );
    SW_EXPECT_TRUE( rig.report( kBob, target, "Bad Reason!" ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.report( kBob, target ) == ChatResult::Ok );
    SW_EXPECT_TRUE( rig.report( kBob, target ) == ChatResult::RateLimited );
    SW_EXPECT_TRUE( rig.report( kBob, mine, "abuse", kNowMs + settings._reportRefillIntervalMs ).hasError( OnlineError::kInvalidRequest ) );

    vector<ServiceReport> listReport;
    string                nextCursor;
    SW_REQUIRE_TRUE( ServiceReportStore::listReports( database, true, "", 10, listReport, nextCursor ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), listReport.size() );
    const ServiceReport& report = listReport[0];
    SW_EXPECT_EQUAL( kAlice, report._targetID );
    SW_EXPECT_EQUAL( kBob, report._reporterID );
    SW_EXPECT_EQUAL( string( kWorld ), report._context );
    SW_EXPECT_EQUAL( string( "abuse" ), report._reason );
    SW_REQUIRE_EQUAL( size_t( 2 ), report._listLine.size() );
    SW_EXPECT_EQUAL( before._text, report._listLine[0]._text );
    SW_EXPECT_EQUAL( target._text, report._listLine[1]._text ); // 마지막 줄이 신고 대상이다
}
