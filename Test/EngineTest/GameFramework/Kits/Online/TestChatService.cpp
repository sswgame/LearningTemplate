// 채팅 로직 — 채널 들어가기 규칙 · 채널 상한, 말하기 전달 · 되울림, 채팅 금지(제재 첫 읽기를 기다린 말하기 · 줄 상한 · GM 변경 버스로 다시 읽기), 거르개 · 도배,
// 귓속말(이 서버 · 차단 · 다른 서버 버스 · 다른 서버 차단 · 접속 없음), 서버 간 채널 전달과 마지막 회원이 떠나면 구독 해지, 기록(묶어 쓰기 · 최근 것부터 · 커서 ·
// 귓속말은 두 사람만 · 저장소 실패), 보존 기간이 지난 기록 정리.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Chat/ChatProtocol.h"
#include "GameFramework/Kits/Feature/Online/Server/Chat/ChatService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 이 서버에 붙은 계정 — 시험이 채운다. */
    class ChatTestDirectory final : public IAccountDirectory
    {
    public:
        unordered_map<AccountId, string> _mapOnline{};

        bool findIdentity( AccountId accountId, AccountIdentity& outIdentity ) const override
        {
            const auto onlineIt = _mapOnline.find( accountId );
            if ( onlineIt == _mapOnline.end() )
                return false;
            outIdentity._accountId   = accountId;
            outIdentity._displayName = onlineIt->second;
            return true;
        }

        bool findIdentityByDisplayName( string_view displayName, AccountIdentity& outIdentity ) const override
        {
            (void)displayName;
            (void)outIdentity;
            return false;
        }

        bool isAccountOnline( AccountId accountId ) const override { return _mapOnline.find( accountId ) != _mapOnline.end(); }
    };

    /** @brief 차단 표 — (막은 이, 막힌 이). */
    class ChatTestPolicy final : public IChatPolicy
    {
    public:
        vector<std::pair<AccountId, AccountId>> _listBlock{};

        bool isBlocked( AccountId recipientId, AccountId senderId ) const override
        {
            for ( const std::pair<AccountId, AccountId>& block : _listBlock )
            {
                if ( block.first == recipientId && block.second == senderId )
                    return true;
            }
            return false;
        }
    };

    /** @brief 서버 하나 — 저장소 앞 · 로컬 버스 · 가짜 접속 상태 · 채팅. 버스 구독 바뀜과 메시지는 호스트 대신 시험이 처리한다. */
    struct ChatNode
    {
        MemoryServiceStore        _store;
        LocalServerBus            _bus;
        ChatTestDirectory         _directory;
        ChatTestPolicy            _policy;
        test::FakeAccountPresence _presence;
        ChatService               _service;
        vector<ChatCompletion>    _listCompletion;
        vector<ChatDelivery>      _listDelivery;

        ChatNode( MemoryServiceDatabase* pDatabase, LocalServerBusHub* pHub, uint64 serverId, const ChatSettings& settings = ChatSettings{} )
            : _store{ pDatabase }
            , _bus{ pHub, serverId }
            , _directory{}
            , _policy{}
            , _presence{}
            , _service{}
            , _listCompletion{}
            , _listDelivery{}
        {
            ChatServiceDependencies dependencies;
            dependencies._pStore     = &_store;
            dependencies._pBus       = &_bus;
            dependencies._pPresence  = &_presence;
            dependencies._pDirectory = &_directory;
            dependencies._pPolicy    = &_policy;
            dependencies._serverId   = serverId;
            SW_EXPECT_TRUE( _service.initialize( dependencies, settings ) );
        }

        ~ChatNode() { _service.shutdown(); }

        void step( int64 nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                vector<ChatBusTopicChange> listChange;
                _service.drainBusTopicChanges( listChange );
                for ( const ChatBusTopicChange& change : listChange )
                {
                    if ( change._bSubscribe == SW_TRUE )
                        _bus.subscribe( change._topic );
                    else
                        _bus.unsubscribe( change._topic );
                }
                vector<ServerBusMessage> listMessage;
                (void)_bus.pollMessages( listMessage );
                for ( const ServerBusMessage& message : listMessage )
                {
                    _service.handleBusMessage( message._topic, message._bytes );
                }
                _service.tick( nowMs );
                (void)_store.pollCompletions();
                _presence.tick();
            }
            _service.drainCompletions( _listCompletion );
            _service.drainDeliveries( _listDelivery );
        }

        int32 countDelivery( AccountId recipientId ) const
        {
            int32 count = 0;
            for ( const ChatDelivery& delivery : _listDelivery )
            {
                count += delivery._recipientId == recipientId ? 1 : 0;
            }
            return count;
        }

        ChatResult getLastResult() const { return _listCompletion.empty() ? ChatResult::Unavailable : _listCompletion.back()._reply._result; }
    };

    struct ChatServiceTestInternal
    {
        static void writeChatMute( MemoryServiceDatabase& database, AccountId accountId, int64 untilMs )
        {
            ServiceSanctionState state;
            SW_EXPECT_TRUE( ServiceSanction::readState( database, accountId, state ) == ServiceStoreResult::Ok );
            state._arrUntilMs[static_cast<int32>( ServiceSanctionKind::ChatMute )] = untilMs;
            ServiceTransaction transaction;
            ServiceSanction::stageWrite( transaction, accountId, state );
            SW_EXPECT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        static void publishSanctionChanged( LocalServerBusHub& hub, AccountId accountId )
        {
            LocalServerBus gmBus( &hub, 99 ); // GM 서버
            BitWriter      body;
            body.writeVarUint( accountId );
            gmBus.publish( ServiceSanctionBus::kChangedTopic, body.getBytes().data(), body.getByteCount() );
        }
    };
} // namespace

SW_TEST_CASE( ChatServiceTest, JoinRulesChannelLimitAndDeliveryWithEcho )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatNode              node( &database, &hub, 1 );
    node._directory._mapOnline[10] = "alice";
    node._directory._mapOnline[11] = "bob";

    node._service.joinChannel( 10, "world.kr", 0, 1 );
    node._service.joinChannel( 11, "world.kr", 0, 2 );
    node._service.joinChannel( 10, "guild.0000000000000005", 0, 3 ); // 클라이언트는 길드에 못 들어간다
    node._service.joinChannel( 10, "World.KR", 0, 4 );               // 규칙 밖
    node.step( 0 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 4 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._reply._result == ChatResult::Ok );
    SW_EXPECT_TRUE( node._listCompletion[2]._reply._result == ChatResult::NotJoinable );
    SW_EXPECT_TRUE( node._listCompletion[3]._reply._result == ChatResult::Invalid );
    SW_EXPECT_TRUE( node._service.addMember( 10, "guild.0000000000000005", 0 ) == ChatResult::Ok ); // 서버 시스템은 넣는다

    node._listCompletion.clear();
    node._service.sendMessage( 10, "world.kr", "hello", 100, 5 );
    node.step( 100 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._reply._result == ChatResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion[0]._method, ChatMethod::kSend );
    SW_EXPECT_STREQ( node._listCompletion[0]._reply._message._senderName.c_str(), "alice" );
    SW_EXPECT_EQUAL( node.countDelivery( 11 ), 1 );
    SW_EXPECT_EQUAL( node.countDelivery( 10 ), 1 ); // 보낸 이도 받는다(다른 창)

    node._listCompletion.clear();
    node._service.sendMessage( 10, "custom.other", "hi", 200, 6 ); // 들어가지 않은 채널
    node._service.leaveChannel( 11, "world.kr", 7 );
    node._service.sendMessage( 11, "world.kr", "bye", 200, 8 );
    node.step( 200 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 3 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._reply._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( node._listCompletion[2]._reply._result == ChatResult::NotMember );

    for ( int32 index = 0; index < ChatLimit::kMaxChannelPerMember; ++index ) // bob 은 world 를 나갔다 — 열여섯이 차면 다음은 거절
    {
        (void)node._service.addMember( 11, string( "custom.c" ) + static_cast<utf8>( 'a' + index ), 300 );
    }
    SW_EXPECT_TRUE( node._service.addMember( 11, "custom.zz", 300 ) == ChatResult::TooManyChannels );
    SW_EXPECT_TRUE( node._service.addMember( 10, "custom.zz", 300 ) == ChatResult::Ok );
    SW_EXPECT_TRUE( node._service.addMember( 10, "custom.yy", 300 ) == ChatResult::Ok );
    SW_EXPECT_TRUE( node._service.addMember( 99, "custom.zz", 300 ) == ChatResult::TargetOffline ); // 이 서버에 없는 계정
}

SW_TEST_CASE( ChatServiceTest, MuteIsReadBeforeTheFirstSendAndRefreshedByTheBus )
{
    MemoryServiceDatabase database;
    ChatServiceTestInternal::writeChatMute( database, 10, 5000 ); // 계정 10 — 채팅 금지 5 초까지
    LocalServerBusHub hub;
    ChatNode          node( &database, &hub, 1 );
    node._directory._mapOnline[10] = "alice";
    node._service.joinChannel( 10, "world.kr", 0, 1 );
    for ( int32 index = 0; index < 5; ++index ) // 제재 첫 읽기를 기다려 넷이 줄을 서고, 다섯째는 바로 거절
    {
        node._service.sendMessage( 10, "world.kr", string( "can you hear me " ) + static_cast<utf8>( '0' + index ), 100, static_cast<uint64>( 2 + index ) );
    }
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 0 ) );
    node.step( 100 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 6 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._reply._result == ChatResult::Ok ); // join
    SW_EXPECT_TRUE( node._listCompletion[1]._reply._result == ChatResult::RateLimited );
    SW_EXPECT_EQUAL( node._listCompletion[1]._requestTag, uint64( 6 ) );
    for ( size_t index = 2; index < 6; ++index )
    {
        SW_EXPECT_TRUE( node._listCompletion[index]._reply._result == ChatResult::Muted );
        SW_EXPECT_EQUAL( node._listCompletion[index]._reply._retryAfterMs, int64( 4900 ) );
    }
    SW_EXPECT_EQUAL( node.countDelivery( 10 ), 0 );

    ChatServiceTestInternal::writeChatMute( database, 10, 0 ); // GM 이 풀었다 — 묵힌 값(60 초) 안이라 아직 금지
    node._listCompletion.clear();
    node._service.sendMessage( 10, "world.kr", "still muted?", 200, 20 );
    node.step( 200 );
    SW_EXPECT_TRUE( node.getLastResult() == ChatResult::Muted );

    ChatServiceTestInternal::publishSanctionChanged( hub, 10 ); // GM 서버가 알렸다 — 다시 읽는다
    node._listCompletion.clear();
    node.step( 300 );
    node._service.sendMessage( 10, "world.kr", "free at last", 300, 21 );
    node.step( 300 );
    SW_EXPECT_TRUE( node.getLastResult() == ChatResult::Ok );
    SW_EXPECT_EQUAL( node.countDelivery( 10 ), 1 );
}

SW_TEST_CASE( ChatServiceTest, FilterAndSpamApplyToChannelMessages )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatSettings          settings;
    settings._bannedWordsPath = FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), "Config/Server/chat_banned_words.txt" );
    ChatNode node( &database, &hub, 1, settings );
    node._directory._mapOnline[10] = "alice";
    node._service.joinChannel( 10, "world.kr", 0, 1 );
    node.step( 0 );

    node._listCompletion.clear();
    node._service.sendMessage( 10, "world.kr", "you BADWORD", 0, 2 );
    node.step( 0 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node._listCompletion[0]._reply._message._text.c_str(), "you *******" );

    for ( int32 index = 0; index < 4; ++index )
    {
        node._service.sendMessage( 10, "world.kr", string( "msg" ) + static_cast<utf8>( 'a' + index ), 10, static_cast<uint64>( 10 + index ) );
    }
    node._service.sendMessage( 10, "world.kr", "one too many", 10, 20 ); // 몰아 쓰기 5 개(앞 글 하나 포함)를 넘었다
    node.step( 10 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 6 ) );
    SW_EXPECT_TRUE( node._listCompletion[4]._reply._result == ChatResult::Ok );
    SW_EXPECT_TRUE( node.getLastResult() == ChatResult::RateLimited );
    SW_EXPECT_TRUE( node._listCompletion.back()._reply._retryAfterMs > 0 );

    node._service.sendMessage( 10, "world.kr", "", 5000, 21 ); // 빈 글
    node.step( 5000 );
    SW_EXPECT_TRUE( node.getLastResult() == ChatResult::Invalid );
}

SW_TEST_CASE( ChatServiceTest, WhisperLocalBlockedAndAcrossServers )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatNode              first( &database, &hub, 1 );
    ChatNode              second( &database, &hub, 2 );
    first._directory._mapOnline[10]  = "alice";
    first._directory._mapOnline[11]  = "bob";
    second._directory._mapOnline[20] = "carol";
    first._presence.setOnline( AccountIdentity{ "carol", 20, SW_FALSE }, 2 ); // 계정 서버(K14)가 쓴 접속 상태 — carol 은 서버 2
    first.step( 0 );
    second.step( 0 );

    first._service.sendWhisper( 10, 11, "psst", 100, 1 );
    first.step( 100 );
    SW_EXPECT_EQUAL( first.countDelivery( 11 ), 1 );
    SW_EXPECT_TRUE( first.getLastResult() == ChatResult::Ok );
    SW_EXPECT_STREQ( first._listCompletion.back()._reply._message._channelId.c_str(), ChatChannelId::makeWhisper( 10, 11 ).c_str() );

    first._policy._listBlock.push_back( { 11, 10 } ); // bob 이 alice 를 막았다
    first._service.sendWhisper( 10, 11, "psst again", 200, 2 );
    first.step( 200 );
    SW_EXPECT_EQUAL( first.countDelivery( 11 ), 1 );           // 더 받지 않는다
    SW_EXPECT_TRUE( first.getLastResult() == ChatResult::Ok ); // 보낸 이는 모른다

    first._service.sendWhisper( 10, 20, "hello carol", 300, 3 ); // 다른 서버
    first.step( 300 );
    second.step( 300 );
    SW_EXPECT_TRUE( first.getLastResult() == ChatResult::Ok );
    SW_EXPECT_EQUAL( second.countDelivery( 20 ), 1 );
    SW_ASSERT_EQUAL( second._listDelivery.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( second._listDelivery[0]._message._senderName.c_str(), "alice" );

    second._policy._listBlock.push_back( { 20, 10 } ); // carol 이 alice 를 막았다 — 받는 서버가 버린다
    first._service.sendWhisper( 10, 20, "are you there", 400, 4 );
    first.step( 400 );
    second.step( 400 );
    SW_EXPECT_TRUE( first.getLastResult() == ChatResult::Ok );
    SW_EXPECT_EQUAL( second.countDelivery( 20 ), 1 );

    first._service.sendWhisper( 10, 99, "nobody", 500, 5 ); // 접속 기록이 없다
    first._service.sendWhisper( 10, 10, "myself", 500, 6 ); // 자기에게
    first.step( 500 );
    SW_ASSERT_TRUE( first._listCompletion.size() >= 2 );
    SW_EXPECT_TRUE( first._listCompletion[first._listCompletion.size() - 2]._reply._result == ChatResult::Invalid );
    SW_EXPECT_TRUE( first.getLastResult() == ChatResult::TargetOffline );
    SW_EXPECT_EQUAL( first._service.getPendingCount(), 0 );
}

SW_TEST_CASE( ChatServiceTest, ChannelMessagesCrossServersAndLeavingUnsubscribes )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatNode              first( &database, &hub, 1 );
    ChatNode              second( &database, &hub, 2 );
    first._directory._mapOnline[10]  = "alice";
    second._directory._mapOnline[20] = "carol";
    first._service.joinChannel( 10, "world.kr", 0, 1 );
    second._service.joinChannel( 20, "world.kr", 0, 1 );
    first.step( 0 );
    second.step( 0 );

    first._service.sendMessage( 10, "world.kr", "hi all", 100, 2 );
    first.step( 100 );
    second.step( 100 );
    first.step( 100 ); // 자기 서버가 낸 버스 메시지가 돌아와도 두 번 전하지 않는다
    SW_EXPECT_EQUAL( second.countDelivery( 20 ), 1 );
    SW_EXPECT_EQUAL( first.countDelivery( 10 ), 1 );

    second._service.removeAccount( 20 ); // carol 이 떠났다 — 서버 2 는 채널 주제를 푼다
    second.step( 200 );
    SW_EXPECT_EQUAL( second._service.getLocalMemberCount( "world.kr" ), 0 );
    first._service.sendMessage( 10, "world.kr", "anyone?", 300, 3 );
    first.step( 300 );
    second._directory._mapOnline[21] = "dave"; // 같은 서버의 다른 계정은 채널에 없다
    second.step( 300 );
    SW_EXPECT_EQUAL( second.countDelivery( 20 ), 1 ); // 더 받지 않는다
    SW_EXPECT_EQUAL( second.countDelivery( 21 ), 0 );
}

SW_TEST_CASE( ChatServiceTest, HistoryIsWrittenInBatchesAndReadNewestFirst )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatNode              node( &database, &hub, 1 );
    node._directory._mapOnline[10] = "alice";
    node._directory._mapOnline[11] = "bob";
    node._directory._mapOnline[12] = "eve";
    node._service.joinChannel( 10, "world.kr", 0, 1 );
    node.step( 0 );
    for ( int32 index = 0; index < 5; ++index )
    {
        node._service.sendMessage( 10, "world.kr", string( "line " ) + static_cast<utf8>( '0' + index ), 1000 * ( index + 1 ), static_cast<uint64>( 10 + index ) );
    }
    node._service.sendWhisper( 10, 11, "secret", 5500, 16 );
    node.step( 6000 );
    SW_EXPECT_EQUAL( node._service.getQueuedHistoryCount(), 0 );

    node._listCompletion.clear();
    node._service.readHistory( 10, "world.kr", "", 3, 50 );
    node.step( 6000 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    const ChatCompletion firstPage = node._listCompletion[0];
    SW_ASSERT_EQUAL( firstPage._reply._listHistory.size(), size_t( 3 ) );
    SW_EXPECT_STREQ( firstPage._reply._listHistory[0]._text.c_str(), "line 4" );
    SW_EXPECT_STREQ( firstPage._reply._listHistory[2]._text.c_str(), "line 2" );
    SW_EXPECT_FALSE( firstPage._reply._nextCursor.empty() );

    node._listCompletion.clear();
    node._service.readHistory( 10, "world.kr", firstPage._reply._nextCursor, 3, 51 );
    node.step( 6000 );
    SW_ASSERT_EQUAL( node._listCompletion[0]._reply._listHistory.size(), size_t( 2 ) );
    SW_EXPECT_STREQ( node._listCompletion[0]._reply._listHistory[1]._text.c_str(), "line 0" );
    SW_EXPECT_TRUE( node._listCompletion[0]._reply._nextCursor.empty() );

    const string whisperKey = ChatChannelId::makeWhisper( 10, 11 );
    node._listCompletion.clear();
    node._service.readHistory( 99, "world.kr", "", 3, 52 ); // 회원이 아니다
    node._service.readHistory( 12, whisperKey, "", 3, 53 ); // 두 사람이 아니다
    node._service.readHistory( 11, whisperKey, "", 3, 54 ); // 받은 사람
    node._service.readHistory( 10, "world.kr", "", 0, 55 ); // 개수 0
    node.step( 6000 );                                      // 바로 거절한 셋이 먼저, 저장소 읽기가 뒤에
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 4 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._reply._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( node._listCompletion[1]._reply._result == ChatResult::NotMember );
    SW_EXPECT_TRUE( node._listCompletion[2]._reply._result == ChatResult::Invalid );
    SW_ASSERT_EQUAL( node._listCompletion[3]._reply._listHistory.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node._listCompletion[3]._reply._listHistory[0]._text.c_str(), "secret" );

    database.armFault( ServiceStoreFault::RejectRead );
    node._listCompletion.clear();
    node._service.readHistory( 10, "world.kr", "", 3, 56 );
    node.step( 6000 );
    SW_EXPECT_TRUE( node.getLastResult() == ChatResult::Unavailable );
}

SW_TEST_CASE( ChatServiceTest, HistoryPastRetentionIsTrimmedAndFailedWritesAreDropped )
{
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatSettings          settings;
    settings._historyRetentionMs = 1000;
    settings._historyTrimEvery   = 1; // 모든 쓰기가 그 채널을 정리한다
    ChatNode node( &database, &hub, 1, settings );
    node._directory._mapOnline[10] = "alice";
    node._service.joinChannel( 10, "world.kr", 0, 1 );
    node.step( 0 );
    node._service.sendMessage( 10, "world.kr", "old", 0, 2 );
    node.step( 0 );
    node._service.sendMessage( 10, "world.kr", "new", 5000, 3 );
    node.step( 5000 );

    node._listCompletion.clear();
    node._service.readHistory( 10, "world.kr", "", 10, 4 );
    node.step( 5000 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( node._listCompletion[0]._reply._listHistory.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( node._listCompletion[0]._reply._listHistory[0]._text.c_str(), "new" );

    SW_TEST_DEFENSIVE_SCOPE( "a rejected history commit is logged and dropped" );
    database.armFault( ServiceStoreFault::RejectCommit );
    node._service.sendMessage( 10, "world.kr", "lost", 6000, 5 );
    node.step( 6000 );
    SW_EXPECT_TRUE( node.getLastResult() == ChatResult::Ok ); // 말하기는 됐다 — 기록만 최선 노력
    SW_EXPECT_EQUAL( node._service.getQueuedHistoryCount(), 0 );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );
}

SW_TEST_CASE( ChatServiceTest, ShutdownCancelsPendingPresenceFinds )
{
    // 서비스가 접속 상태 창구보다 먼저 내려가도 된다 — 맡긴 찾기를 취소해 창구가 나중에 사라진 서비스를 부르지 않는다.
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    ChatNode              node( &database, &hub, 1 );
    node._directory._mapOnline[10] = "alice";
    node._presence.setOnline( AccountIdentity{ "carol", 20, SW_FALSE }, 2 );
    node._service.sendWhisper( 10, 20, "hello carol", 100, 1 ); // 다른 서버 — 제재를 읽은 뒤 접속 상태 찾기를 맡긴다
    (void)node._store.pollCompletions();
    SW_ASSERT_EQUAL( node._presence.getLivePendingCount(), 1 );
    node._service.shutdown();
    SW_EXPECT_EQUAL( node._presence.getLivePendingCount(), 0 );
}

SW_TEST_CASE( ChatServiceTest, ProtocolRoundTripsRepliesAndRecords )
{
    ChatMessage message;
    message._channelId   = ChatChannelId::makeWhisper( 3, 7 );
    message._kind        = ChatChannelKind::Whisper;
    message._senderId    = 3;
    message._senderName  = "alice";
    message._recipientId = 7;
    message._text        = "hello";
    message._sentMs      = 123456;
    message._serverId    = 2;
    message._sequence    = 9;

    ChatMessage decoded;
    SW_ASSERT_TRUE( ChatProtocol::decodeRecord( ChatProtocol::encodeRecord( message ), decoded ) );
    SW_EXPECT_STREQ( decoded._channelId.c_str(), message._channelId.c_str() );
    SW_EXPECT_EQUAL( decoded._recipientId, AccountId( 7 ) );
    SW_EXPECT_EQUAL( decoded._sentMs, int64( 123456 ) );
    SW_EXPECT_EQUAL( decoded._sequence, uint32( 9 ) );

    ChatReply reply;
    reply._listHistory.push_back( message );
    reply._nextCursor = "world.kr/0000000000000001.0000000000000001.0000000000000001";
    BitWriter writer;
    ChatProtocol::writeReply( writer, ChatMethod::kHistory, reply );
    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    ChatReply readBack;
    SW_ASSERT_TRUE( ChatProtocol::readReply( reader, ChatMethod::kHistory, readBack ) );
    SW_ASSERT_EQUAL( readBack._listHistory.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( readBack._nextCursor.c_str(), reply._nextCursor.c_str() );

    ChatReply muted;
    muted._result       = ChatResult::Muted;
    muted._retryAfterMs = 4900;
    BitWriter mutedWriter;
    ChatProtocol::writeReply( mutedWriter, ChatMethod::kSend, muted );
    BitReader mutedReader( mutedWriter.getBytes().data(), mutedWriter.getByteCount() );
    ChatReply mutedBack;
    SW_ASSERT_TRUE( ChatProtocol::readReply( mutedReader, ChatMethod::kSend, mutedBack ) );
    SW_EXPECT_TRUE( mutedBack._result == ChatResult::Muted );
    SW_EXPECT_EQUAL( mutedBack._retryAfterMs, int64( 4900 ) );

    vector<uint8> otherVersion = ChatProtocol::encodeRecord( message );
    otherVersion[0]            = 2;
    SW_EXPECT_FALSE( ChatProtocol::decodeRecord( otherVersion, decoded ) ); // 다른 판은 읽지 않는다
    SW_EXPECT_TRUE( ChatProtocol::fromErrorCode( OnlineError::kUnauthenticated ) == ChatResult::NotSignedIn );
    SW_EXPECT_TRUE( ChatProtocol::fromErrorCode( OnlineError::kInternal ) == ChatResult::Unavailable );
}
