// 친구 로직 — 신청 → 수락 → 목록, 서로 신청은 친구 하나, 같은 판을 읽은 두 커밋의 경합은 다시 읽어 친구로(충돌 다시 하기), 이름으로 신청(정식 계정만 — 없는 이름 NotFound),
// 막기는 차단 조회에 바로 보이고 막힌 사람의 신청은 알림 없는 Ok, 접속 상태가 다른 서버의 친구에게 알려지고 질의는 캐시를 읽는다, 저장소 실패는 Unavailable 이고 아무것도 쓰지 않음.
#include "pch.h"

#include "EngineTest/GameFramework/Online/OnlineMessageTestUtil.h"
#include "EngineTest/GameFramework/Online/ServerBusTestUtil.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Identity/AccountNameIndex.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialLinkRules.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 테스트 이름 색인 — 이름(소문자) → 신원. 게스트는 넣지 않는다(실제 색인과 같다). */
    class FakeNameIndex final : public IAccountNameIndex
    {
    public:
        FakeNameIndex()
            : _listIdentity{}
        {
        }

        void add( AccountID accountID, string_view displayName ) { _listIdentity.push_back( AccountIdentity{ string( displayName ), accountID, SW_FALSE } ); }

        ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity ) const override
        {
            (void)connection;
            for ( const AccountIdentity& identity : _listIdentity )
            {
                if ( identity._displayName == displayName )
                {
                    outIdentity = identity;
                    return ServiceStoreResult::Ok;
                }
            }
            return ServiceStoreResult::NotFound;
        }

        ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            (void)connection;
            for ( const AccountIdentity& identity : _listIdentity )
            {
                if ( identity._accountID == accountID )
                {
                    outIdentity = identity;
                    return ServiceStoreResult::Ok;
                }
            }
            return ServiceStoreResult::NotFound;
        }

    private:
        vector<AccountIdentity> _listIdentity;
    };

    /** @brief 서버 프로세스 하나의 친구 로직 — 저장소 앞 · 캐시 앞 · 라우터 · 로컬 버스. 버스 메시지는 호스트 대신 테스트가 비워 넘긴다. */
    struct SocialNode
    {
        MemoryServiceStore         _store;
        MemoryEphemeralStore       _cache;
        EphemeralStoreRouter       _router;
        LocalServerBus             _bus;
        ServerBus                  _busFront;
        SocialService              _service;
        vector<SocialCompletion>   _listCompletion;
        vector<SocialNotification> _listNotification;
        IServiceStore*             _pFront; ///< 서비스가 쓰는 저장소 앞(테스트가 끼워 넣는 앞일 수 있다)

        SocialNode( MemoryServiceDatabase* pDatabase, MemoryEphemeralDatabase* pCacheDatabase, LocalServerBusHub* pHub, uint64 serverID,
                    const IAccountNameIndex* pNameIndex = nullptr, IServiceStore* pStore = nullptr )
            : _store{ pDatabase }
            , _cache{ pCacheDatabase }
            , _router{}
            , _bus{ pHub, serverID }
            , _busFront{ _bus }
            , _service{}
            , _listCompletion{}
            , _listNotification{}
            , _pFront{ pStore != nullptr ? pStore : &_store }
        {
            _router.initialize( &_cache );
            _bus.subscribe( SocialBus::kLinksTopic.getName() );
            _bus.subscribe( SocialBus::kPresenceTopic.getName() );
            SocialServiceDependencies dependencies;
            dependencies._pStore     = _pFront;
            dependencies._pRouter    = &_router;
            dependencies._pBus       = &_busFront;
            dependencies._pNameIndex = pNameIndex;
            _service.initialize( dependencies );
        }

        void step( UnixMilliseconds nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                vector<ServerBusMessage> listMessage;
                (void)_bus.pollMessages( listMessage );
                for ( const ServerBusMessage& message : listMessage )
                {
                    if ( message.getOriginServerID() != _bus.getServerID() )
                        _service.handleBusMessage( message );
                }
                _service.update( nowMs );
                (void)_pFront->pollCompletions();
                (void)_router.pump();
            }
            _service.drainCompletions( _listCompletion );
            _service.drainNotifications( _listNotification );
        }

        SocialResult getLastResult() const { return _listCompletion.empty() ? SocialResult::Count : _listCompletion.back()._result; }

        /** @brief @p accountID 의 관계 목록을 읽어 마지막 완료로 둡니다. */
        const vector<SocialLink>& readLinks( AccountID accountID, uint64 requestTag, UnixMilliseconds nowMs )
        {
            _service.listLinks( accountID, requestTag );
            step( nowMs );
            return _listCompletion.back()._listLink;
        }
    };

    /** @brief 맡은 일의 첫 커밋 직전에 다른 서비스의 요청을 끼워 넣는 연결 — 같은 판을 읽은 두 커밋의 경합을 결정적으로 만든다. */
    class InterleavingConnection final : public IServiceStoreConnection
    {
    public:
        InterleavingConnection( MemoryServiceDatabase* pDatabase, SocialService* pRival, AccountID rivalID, AccountID targetID )
            : _pDatabase{ pDatabase }
            , _pRival{ pRival }
            , _rivalID{ rivalID }
            , _targetID{ targetID }
            , _bInterleaved{ SW_FALSE }
        {
        }

        ServiceStoreResult readRecord( const hashed_string& table, string_view key, ServiceRecord& outRecord ) override
        {
            return _pDatabase->readRecord( table, key, outRecord );
        }

        ServiceStoreResult listRecords( const hashed_string& table, string_view keyPrefix, string_view cursorKey, int32 maxCount, bool bDescending,
                                        vector<ServiceRecord>& outListRecord ) override
        {
            return _pDatabase->listRecords( table, keyPrefix, cursorKey, maxCount, bDescending, outListRecord );
        }

        ServiceStoreResult commit( const ServiceTransaction& transaction, ServiceCommitInfo* pOutInfo ) override
        {
            if ( _bInterleaved == SW_FALSE )
            {
                _bInterleaved = SW_TRUE;
                _pRival->changeLink( SocialLinkOperation::Request, _rivalID, _targetID, UnixMilliseconds{ 0 }, 99 ); // 메모리 앞은 그 자리에서 돈다 — 상대의 신청이 먼저 들어간다
            }
            return _pDatabase->commit( transaction, pOutInfo );
        }

    private:
        MemoryServiceDatabase* _pDatabase;
        SocialService*         _pRival;
        AccountID              _rivalID;
        AccountID              _targetID;
        uint8                  _bInterleaved;
    };

    /** @brief 일을 끼워 넣는 연결로 돌리는 저장소 앞입니다. */
    class InterleavingStore final : public IServiceStore
    {
    public:
        explicit InterleavingStore( InterleavingConnection* pConnection )
            : _listCompleted{}
            , _pConnection{ pConnection }
        {
        }

        void submit( unique_ptr<IServiceStoreWork> work ) override
        {
            work->run( *_pConnection );
            _listCompleted.push_back( std::move( work ) );
        }

        int32 pollCompletions() override
        {
            vector<unique_ptr<IServiceStoreWork>> listWork = std::move( _listCompleted );
            _listCompleted.clear();
            for ( unique_ptr<IServiceStoreWork>& work : listWork )
            {
                work->complete();
            }
            return static_cast<int32>( listWork.size() );
        }

        int32 getPendingCount() const override { return static_cast<int32>( _listCompleted.size() ); }
        void  shutdown() override {}

    private:
        vector<unique_ptr<IServiceStoreWork>> _listCompleted;
        InterleavingConnection*               _pConnection;
    };
} // namespace

SW_TEST_CASE( SocialServiceTest, RequestAcceptAndList )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    SocialNode              node( &database, &cacheDatabase, &hub, 1 );
    node._service.changeLink( SocialLinkOperation::Request, 1, 2, UnixMilliseconds{ 100 }, 1 );
    node.step( UnixMilliseconds{ 100 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listNotification[0]._recipientID, AccountID( 2 ) );
    SW_EXPECT_TRUE( node._listNotification[0]._kind == SocialNotificationKind::FriendRequested );

    node._service.changeLink( SocialLinkOperation::Request, 1, 2, UnixMilliseconds{ 150 }, 2 );
    node.step( UnixMilliseconds{ 150 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::AlreadyRequested );
    node._service.changeLink( SocialLinkOperation::Accept, 2, 1, UnixMilliseconds{ 200 }, 3 );
    node.step( UnixMilliseconds{ 200 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_EXPECT_TRUE( node._listNotification.back()._kind == SocialNotificationKind::FriendAdded );

    const vector<SocialLink>& listLink = node.readLinks( 1, 4, UnixMilliseconds{ 200 } );
    SW_ASSERT_EQUAL( listLink.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLink[0]._state == SocialLinkState::Friend );
    SW_EXPECT_EQUAL( listLink[0]._otherID, AccountID( 2 ) );
    SW_EXPECT_EQUAL( listLink[0]._sinceMs, int64( 200 ) );

    node._service.changeLink( SocialLinkOperation::Request, 1, 1, UnixMilliseconds{ 300 }, 5 ); // 자기 자신
    node.step( UnixMilliseconds{ 300 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Invalid );
}

SW_TEST_CASE( SocialServiceTest, ConcurrentMutualRequestsRetryIntoOneFriendship )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    SocialNode              rival( &database, &cacheDatabase, &hub, 2 );
    InterleavingConnection  connection( &database, &rival._service, 2, 1 );
    InterleavingStore       store( &connection );
    SocialNode              node( &database, &cacheDatabase, &hub, 1, nullptr, &store );

    const uint64 commitBefore = database.getCommitCount();
    node._service.changeLink( SocialLinkOperation::Request, 1, 2, UnixMilliseconds{ 0 }, 1 ); // 첫 커밋 직전에 2 → 1 신청이 끼어든다 — 충돌 → 다시 읽으면 "받은 신청" → 친구
    node.step( UnixMilliseconds{ 0 } );
    rival.step( UnixMilliseconds{ 0 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_EXPECT_TRUE( rival.getLastResult() == SocialResult::Ok );
    SW_EXPECT_EQUAL( database.getCommitCount(), commitBefore + 2 ); // 끼어든 신청 하나 + 다시 해서 수락 하나(충돌한 커밋은 세지 않는다)

    const vector<SocialLink> listLinkOfFirst = node.readLinks( 1, 2, UnixMilliseconds{ 0 } );
    SW_ASSERT_EQUAL( listLinkOfFirst.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLinkOfFirst[0]._state == SocialLinkState::Friend );
    const vector<SocialLink> listLinkOfSecond = rival.readLinks( 2, 2, UnixMilliseconds{ 0 } );
    SW_ASSERT_EQUAL( listLinkOfSecond.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLinkOfSecond[0]._state == SocialLinkState::Friend );

    // 개수도 맞다 — 받은 · 보낸 신청이 남지 않았으니 같은 둘이 다시 친구가 되어도 상한이 새지 않는다(셋째 계정이 1 에게 신청할 수 있다)
    node._service.changeLink( SocialLinkOperation::Remove, 1, 2, UnixMilliseconds{ 10 }, 3 );
    node.step( UnixMilliseconds{ 10 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_EXPECT_EQUAL( node.readLinks( 1, 4, UnixMilliseconds{ 10 } ).size(), size_t( 0 ) );
}

SW_TEST_CASE( SocialServiceTest, RequestByNameFindsRegisteredAccountsOnly )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    FakeNameIndex           nameIndex;
    nameIndex.add( 2, "bob" );
    SocialNode node( &database, &cacheDatabase, &hub, 1, &nameIndex );

    node._service.requestFriendByName( 1, "bob", UnixMilliseconds{ 0 }, 1 );
    node.step( UnixMilliseconds{ 0 } );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion[0]._otherID, AccountID( 2 ) );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listNotification[0]._recipientID, AccountID( 2 ) );

    node._service.requestFriendByName( 1, "Guest-00abcd", UnixMilliseconds{ 0 }, 2 ); // 게스트 이름은 색인에 없다 — 계정 id 로
    node.step( UnixMilliseconds{ 0 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::NotFound );
    node._service.requestFriendByName( 2, "bob", UnixMilliseconds{ 0 }, 3 ); // 자기 자신
    node.step( UnixMilliseconds{ 0 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Invalid );
}

SW_TEST_CASE( SocialServiceTest, BlockIsVisibleLocallyAndHidesRequests )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    SocialNode              first( &database, &cacheDatabase, &hub, 1 );
    SocialNode              second( &database, &cacheDatabase, &hub, 2 );
    (void)second.readLinks( 2, 1, UnixMilliseconds{ 0 } ); // 계정 2 는 서버 2 에 붙어 있고 관계를 읽어 두었다
    second._service.changeLink( SocialLinkOperation::Block, 2, 1, UnixMilliseconds{ 100 }, 2 );
    second.step( UnixMilliseconds{ 100 } );
    SW_EXPECT_TRUE( second.getLastResult() == SocialResult::Ok );
    SW_EXPECT_TRUE( second._service.isBlockedLocal( 2, 1 ) );
    SW_EXPECT_FALSE( second._service.isBlockedLocal( 1, 2 ) );

    first._service.changeLink( SocialLinkOperation::Request, 1, 2, UnixMilliseconds{ 200 }, 1 ); // 막힌 사람의 신청 — 조용히 Ok, 아무 데도 알림 없음
    first.step( UnixMilliseconds{ 200 } );
    second.step( UnixMilliseconds{ 200 } );
    SW_EXPECT_TRUE( first.getLastResult() == SocialResult::Ok );
    SW_EXPECT_EQUAL( first._listNotification.size(), size_t( 0 ) );
    SW_EXPECT_EQUAL( first.readLinks( 1, 2, UnixMilliseconds{ 200 } ).size(), size_t( 0 ) );

    second._service.changeLink( SocialLinkOperation::Request, 2, 1, UnixMilliseconds{ 250 }, 3 ); // 내가 막은 사람에게
    second.step( UnixMilliseconds{ 250 } );
    SW_EXPECT_TRUE( second.getLastResult() == SocialResult::YouBlocked );

    second._service.changeLink( SocialLinkOperation::Unblock, 2, 1, UnixMilliseconds{ 300 }, 4 );
    second.step( UnixMilliseconds{ 300 } );
    SW_EXPECT_FALSE( second._service.isBlockedLocal( 2, 1 ) );
}

SW_TEST_CASE( SocialServiceTest, PresenceReachesFriendsOnOtherServersAndQueryReadsCache )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    SocialNode              first( &database, &cacheDatabase, &hub, 1 );
    SocialNode              second( &database, &cacheDatabase, &hub, 2 );
    first._service.changeLink( SocialLinkOperation::Request, 1, 2, UnixMilliseconds{ 0 }, 1 );
    first.step( UnixMilliseconds{ 0 } );
    second._service.changeLink( SocialLinkOperation::Accept, 2, 1, UnixMilliseconds{ 0 }, 1 );
    second.step( UnixMilliseconds{ 0 } );
    (void)first.readLinks( 1, 2, UnixMilliseconds{ 0 } );  // 1 은 서버 1
    (void)second.readLinks( 2, 2, UnixMilliseconds{ 0 } ); // 2 는 서버 2

    first._listNotification.clear(); // 앞 단계의 신청 · 친구 알림
    second._service.setPresence( 2, SocialPresenceStatus::InGame, "dungeon 3", UnixMilliseconds{ 100 }, 3 );
    second.step( UnixMilliseconds{ 100 } );
    first.step( UnixMilliseconds{ 100 } );
    SW_ASSERT_EQUAL( first._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( first._listNotification[0]._recipientID, AccountID( 1 ) );
    SW_EXPECT_TRUE( first._listNotification[0]._kind == SocialNotificationKind::PresenceChanged );
    SW_EXPECT_STREQ( first._listNotification[0]._presence._activity.c_str(), "dungeon 3" );

    first._service.queryFriendPresence( 1, 4 ); // 2 는 다른 서버 — 캐시를 읽는다
    first.step( UnixMilliseconds{ 100 } );
    SW_ASSERT_EQUAL( first._listCompletion.back()._listPresence.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( first._listCompletion.back()._listPresence[0]._status == SocialPresenceStatus::InGame );

    second._service.removeAccount( 2 ); // 떠남 — 오프라인 알림 · 캐시 지움
    second.step( UnixMilliseconds{ 200 } );
    first.step( UnixMilliseconds{ 200 } );
    SW_EXPECT_TRUE( first._listNotification.back()._presence._status == SocialPresenceStatus::Offline );
    first._service.queryFriendPresence( 1, 5 );
    first.step( UnixMilliseconds{ 200 } );
    SW_ASSERT_EQUAL( first._listCompletion.back()._listPresence.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( first._listCompletion.back()._listPresence[0]._status == SocialPresenceStatus::Offline );

    second._service.setPresence( 2, SocialPresenceStatus::Offline, "", UnixMilliseconds{ 300 }, 6 ); // 오프라인은 떠나기로만
    second.step( UnixMilliseconds{ 300 } );
    SW_EXPECT_TRUE( second.getLastResult() == SocialResult::Invalid );
}

SW_TEST_CASE( SocialServiceTest, StoreFailureWritesNothing )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    SocialNode              node( &database, &cacheDatabase, &hub, 1 );
    const uint64            hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    node._service.changeLink( SocialLinkOperation::Request, 1, 2, UnixMilliseconds{ 0 }, 1 );
    node.step( UnixMilliseconds{ 0 } );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
    SW_EXPECT_EQUAL( node._listNotification.size(), size_t( 0 ) );
}

/**
 * @brief [SocialProtocolTest] 친구 메시지, 알림, 버스 본문은 상한까지 왕복하고, 접속 상태 캐시 값은 자기 기술형으로 왕복하며, 범위 밖 값은 쓰지 않는다
 */
SW_TEST_CASE( SocialProtocolTest, MessagesRoundTripAndRefuseOutOfRange )
{
    using Messages = SocialProtocol::MessageTypes;
    using Util     = test::OnlineMessageTestUtil;
    SocialReply reply;
    reply._result = SocialResult::StorageNotEmpty;
    reply._listLink.assign( 1000, SocialLink{ 9, 123, SocialLinkState::Blocked } );
    reply._listPresence.assign( 200, SocialPresence{ string( 64, 'x' ), 9, SocialPresenceStatus::InGame } );
    reply._listStorage.assign( 128, GuildStorageItem{ string( 48, 'g' ), -3 } );
    reply._guild._name   = string( 24, 'n' );
    reply._guild._notice = string( 256, 'o' );
    reply._guild._listMember.assign( 100, GuildMember{ 5, 6, GuildRole::Master } );
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( reply ) );
    SocialNotification notification;
    notification._kind     = SocialNotificationKind::PresenceChanged;
    notification._presence = SocialPresence{ "dungeon", 4, SocialPresenceStatus::Away };
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( notification ) );
    SW_EXPECT_TRUE( test::ServerBusTestUtil::roundTrips( SocialBus::kLinksTopic, SocialLinkChanged{ 1, 2 } ) );
    SocialPresence decoded;
    vector<uint8>  presenceBytes;
    SW_ASSERT_TRUE( SocialProtocol::encodePresenceRecord( SocialPresence{ "lobby", 8, SocialPresenceStatus::Busy }, presenceBytes ) );
    SW_ASSERT_TRUE( SocialProtocol::decodePresenceRecord( presenceBytes, decoded ) );
    SW_EXPECT_TRUE( decoded._activity == "lobby" && decoded._accountID == 8 && decoded._status == SocialPresenceStatus::Busy );
    const SocialPresence tooLong{ string( 65, 'x' ), 8, SocialPresenceStatus::Busy };
    {
        SW_TEST_DEFENSIVE_SCOPE( "an out-of-range presence is not written" );
        SW_EXPECT_FALSE( SocialProtocol::encodePresenceRecord( tooLong, presenceBytes ) );
    }
    SW_ASSERT_TRUE( test::OnlineMessageTestUtil::writeUncheckedRecord( 1, "sw::SocialPresence", tooLong, presenceBytes ) ); // 다른 빌드가 쓴 값을 흉내 낸다
    SW_EXPECT_FALSE( SocialProtocol::decodePresenceRecord( presenceBytes, decoded ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "out-of-range values are not written" );
        SocialRequest request;
        request._text = string( 257, 't' );
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( request ) );
        SocialRequest badRole;
        badRole._role = GuildRole::Count;
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( badRole ) );
    }
}

/**
 * @brief [SocialProtocolTest] 다른 서버로 가는 알림 중계는 받는 서버가 자기 빌드의 번호와 B 로 다시 쓰고, 모르는 알림 · 다른 본문 타입은 버린다
 * @details 봉투는 알림 번호 · B 대신 알림 열거자 이름과 본문 A 를 싣는다(docs/09_Decisions.md 5-9 서버 버스 push 중계). 소셜 키트가 `Social.Notification` 항목을 등록한다.
 */
SW_TEST_CASE( SocialProtocolTest, RelayedPushIsRewrittenByTheReceivingBuild )
{
    SocialNotification notification;
    notification._otherID = 7;
    notification._kind    = SocialNotificationKind::FriendAdded;
    OnlinePushRelay relay;
    SW_ASSERT_TRUE( OnlinePushRelayRegistry::makeRelay( AccountID( 3 ), SocialPushName::Notification, notification, relay ) );
    SW_EXPECT_EQUAL( AccountID( 3 ), relay._accountID );
    SW_EXPECT_TRUE( relay._pushName == "Notification" );

    uint16    kind = 0;
    BitWriter body;
    SW_ASSERT_TRUE( OnlinePushRelayRegistry::convert( relay, kind, body ) );
    SW_EXPECT_EQUAL( SocialMethod::kPushNotification, kind );
    BitReader          reader( body.getBytes().data(), body.getByteCount() );
    SocialNotification decoded;
    SW_ASSERT_TRUE( SocialProtocol::MessageTypes::read( reader, decoded ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( AccountID( 7 ), decoded._otherID );
    SW_EXPECT_TRUE( decoded._kind == SocialNotificationKind::FriendAdded );

    const uint64              rejectedAt = OnlinePushRelayRegistry::getRejectedCount();
    test::ScopedLogSuppressor suppressor; // 버린 중계 오류(알림과 이유마다 한 번)
    OnlinePushRelay           unknownPush = relay;
    unknownPush._pushName                 = "NotificationAddedLater";
    BitWriter unknownBody;
    SW_EXPECT_FALSE( OnlinePushRelayRegistry::convert( unknownPush, kind, unknownBody ) );
    OnlinePushRelay otherBody = relay;
    otherBody._bodyType       = "sw::SocialPresence";
    BitWriter otherBodyWriter;
    SW_EXPECT_FALSE( OnlinePushRelayRegistry::convert( otherBody, kind, otherBodyWriter ) );
    SW_EXPECT_EQUAL( rejectedAt + 2, OnlinePushRelayRegistry::getRejectedCount() );
}
