// SocialServiceTest — 친구 로직(메모리 저장소 + 메모리 캐시 + 프로세스 안 버스): 관계 바꾸기와 양쪽 알림, 이름으로 신청, 이 서버 관계 메모리(차단 조회)와 버스로 다시 읽기,
// 읽는 도중 바뀐 관계, 접속 상태 쓰기 · 연장 · 조회 · 떠날 때 · 친구 역색인을 따른 전달. 규칙 표는 SocialLinkRulesTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialCounts.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"
#include "GameFramework/Runtime/Online/Bus/Server/LocalServerBus.h"
#include "GameFramework/Runtime/Online/Bus/ServerBus.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

#include <algorithm>

using namespace sw;

namespace
{
    constexpr AccountID kAlice   = 11;
    constexpr AccountID kBob     = 12;
    constexpr AccountID kCarol   = 13;
    constexpr int64     kNowMs   = 1700000000000ll;
    constexpr uint64    kServerA = 1;
    constexpr uint64    kServerB = 2;

    /** @brief "bob" 만 아는 이름 인덱스입니다. */
    class FakeNameIndex final : public IAccountNameIndex
    {
    public:
        ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& /*connection*/, string_view displayName, AccountIdentity& outIdentity ) const override
        {
            if ( displayName == "unavailable" )
                return ServiceStoreResult::Unavailable;
            if ( displayName == "alice" )
                outIdentity._accountID = kAlice;
            else if ( displayName == "bob" )
                outIdentity._accountID = kBob;
            else
                return ServiceStoreResult::NotFound;
            return ServiceStoreResult::Ok;
        }
        ServiceStoreResult readIdentity( IServiceStoreConnection& /*connection*/, AccountID /*accountID*/, AccountIdentity& /*outIdentity*/ ) const override { return ServiceStoreResult::NotFound; }
    };

    /** @brief 서버 하나입니다. 영속 · 캐시 데이터와 버스 허브는 빌립니다. */
    struct SocialFixture
    {
        MemoryServiceStore         _store;
        MemoryEphemeralStore       _cache;
        EphemeralStoreRouter       _router;
        FakeNameIndex              _nameIndex;
        unique_ptr<LocalServerBus> _transport;
        unique_ptr<ServerBus>      _bus;
        SocialService              _service;
        uint64                     _nextTag{ 1 };

        SocialFixture( MemoryServiceDatabase& database, MemoryEphemeralDatabase& cacheData, LocalServerBusHub* pHub, uint64 serverID )
            : _store{ &database }
            , _cache{ &cacheData }
            , _router{}
            , _nameIndex{}
            , _transport{}
            , _bus{}
            , _service{}
        {
            _router.initialize( &_cache );
            SocialServiceDependencies dependencies;
            dependencies._pStore     = &_store;
            dependencies._pRouter    = &_router;
            dependencies._pNameIndex = &_nameIndex;
            if ( pHub != nullptr )
            {
                _transport = make_unique<LocalServerBus>( pHub, serverID );
                _bus       = make_unique<ServerBus>( *_transport );
                _transport->subscribe( SocialBus::kLinksTopic.getName() );
                _transport->subscribe( SocialBus::kPresenceTopic.getName() );
                dependencies._pBus = _bus.get();
            }
            _service.initialize( dependencies );
        }

        ~SocialFixture()
        {
            poll();
            _service.shutdown();
            _router.shutdown();
        }

        void pollBus()
        {
            if ( _transport == nullptr )
                return;
            vector<ServerBusMessage> listMessage;
            _transport->pollMessages( listMessage );
            for ( const ServerBusMessage& message : listMessage )
            {
                _service.handleBusMessage( message );
            }
        }

        void poll()
        {
            pollBus();
            while ( _store.pollCompletions() + _router.poll() > 0 )
            {
            }
        }

        vector<SocialCompletion> takeAll()
        {
            poll();
            vector<SocialCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            return listCompletion;
        }

        SocialCompletion take()
        {
            vector<SocialCompletion> listCompletion = takeAll();
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            return listCompletion.empty() ? SocialCompletion{} : std::move( listCompletion[0] );
        }

        SocialServiceResult change( SocialLinkOperation operation, AccountID accountID, AccountID otherID )
        {
            _service.changeLink( operation, accountID, otherID, UnixMilliseconds{ kNowMs }, _nextTag++ );
            return take()._result;
        }

        vector<SocialLink> links( AccountID accountID )
        {
            _service.listLinks( accountID, _nextTag++ );
            return take()._listLink;
        }

        vector<SocialNotification> takeNotifications()
        {
            poll();
            vector<SocialNotification> listNotification;
            _service.pollNotifications( listNotification );
            return listNotification;
        }

        void befriend( AccountID first, AccountID second )
        {
            SW_EXPECT_TRUE( change( SocialLinkOperation::Request, first, second ) == SocialResult::Ok );
            SW_EXPECT_TRUE( change( SocialLinkOperation::Accept, second, first ) == SocialResult::Ok );
            (void)takeNotifications();
        }

        SocialServiceResult setPresence( AccountID accountID, SocialPresenceStatus status, string_view activity = "", int64 nowMs = kNowMs )
        {
            _service.setPresence( accountID, status, activity, UnixMilliseconds{ nowMs }, _nextTag++ );
            return take()._result;
        }

        vector<SocialPresence> queryPresence( AccountID accountID )
        {
            _service.queryFriendPresence( accountID, _nextTag++ );
            return take()._listPresence;
        }
    };

    /** @brief 시계 · 데이터와 서버 하나의 묶음입니다. */
    struct SocialWorld
    {
        ManualClock             _clock;
        MemoryServiceDatabase   _database;
        MemoryEphemeralDatabase _cacheData;
        LocalServerBusHub       _hub;
        SocialFixture           _rig;

        explicit SocialWorld( bool bBus = false )
            : _clock{ MonotonicMilliseconds{ 1000 } }
            , _database{}
            , _cacheData{ _clock }
            , _hub{}
            , _rig{ _database, _cacheData, bBus ? &_hub : nullptr, kServerA }
        {
        }
    };
} // namespace

/** @brief [SocialServiceTest] 신청 · 수락: 양쪽 줄이 맞고 받는 쪽이 알림을 받으며, 수락하면 둘 다 FriendAdded 를 받는다. 자신 · 0 은 Invalid 다 */
SW_TEST_CASE( SocialServiceTest, RequestAndAcceptNotifyBothSides )
{
    SocialWorld    world;
    SocialFixture& rig = world._rig;
    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Request, kAlice, kAlice ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Request, kAlice, kInvalidAccountID ).hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Request, kAlice, kBob ) == SocialResult::Ok );
    vector<SocialNotification> listNotification = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._kind == SocialNotificationKind::FriendRequested );
    SW_EXPECT_EQUAL( kBob, listNotification[0]._recipientID );
    SW_EXPECT_EQUAL( kAlice, listNotification[0]._otherID );

    vector<SocialLink> listAlice = rig.links( kAlice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listAlice.size() );
    SW_EXPECT_TRUE( listAlice[0]._state == SocialLinkState::Outgoing );
    SW_EXPECT_EQUAL( kNowMs, listAlice[0]._sinceMs );
    vector<SocialLink> listBob = rig.links( kBob );
    SW_REQUIRE_EQUAL( size_t( 1 ), listBob.size() );
    SW_EXPECT_TRUE( listBob[0]._state == SocialLinkState::Incoming );

    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Accept, kBob, kAlice ) == SocialResult::Ok );
    listNotification = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 2 ), listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._kind == SocialNotificationKind::FriendAdded );
    SW_EXPECT_TRUE( listNotification[1]._kind == SocialNotificationKind::FriendAdded );
    SW_EXPECT_TRUE( rig.links( kAlice )[0]._state == SocialLinkState::Friend );

    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Remove, kAlice, kBob ) == SocialResult::Ok );
    listNotification = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._kind == SocialNotificationKind::FriendRemoved );
    SW_EXPECT_TRUE( rig.links( kBob ).empty() );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [SocialServiceTest] 이름으로 신청: 찾으면 그 계정에 신청하고 완료에 계정을 싣는다. 없으면 NotFound, 자신은 Invalid, 빈 이름은 Invalid, 저장소 실패는 Unavailable 이다 */
SW_TEST_CASE( SocialServiceTest, RequestByName )
{
    SocialWorld    world;
    SocialFixture& rig = world._rig;
    rig._service.requestFriendByName( kAlice, "bob", UnixMilliseconds{ kNowMs }, 1 );
    const SocialCompletion found = rig.take();
    SW_EXPECT_TRUE( found._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( kBob, found._otherID );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.links( kBob ).size() );

    rig._service.requestFriendByName( kAlice, "nobody", UnixMilliseconds{ kNowMs }, 2 );
    SW_EXPECT_TRUE( rig.take()._result == SocialResult::NotFound );
    rig._service.requestFriendByName( kAlice, "alice", UnixMilliseconds{ kNowMs }, 3 );
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kInvalidRequest ) );
    rig._service.requestFriendByName( kAlice, "", UnixMilliseconds{ kNowMs }, 4 );
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kInvalidRequest ) );
    rig._service.requestFriendByName( kAlice, "unavailable", UnixMilliseconds{ kNowMs }, 5 );
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kUnavailable ) );
}

/** @brief [SocialServiceTest] 차단 조회: 관계를 바꾸면 이 서버 메모리가 따라가고, 막은 쪽에서만 true 다 */
SW_TEST_CASE( SocialServiceTest, BlockedLookupFollowsChanges )
{
    SocialWorld    world;
    SocialFixture& rig = world._rig;
    (void)rig.links( kAlice );
    SW_EXPECT_FALSE( rig._service.isBlockedLocal( kAlice, kBob ) );
    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Block, kAlice, kBob ) == SocialResult::Ok );
    SW_EXPECT_TRUE( rig._service.isBlockedLocal( kAlice, kBob ) );
    SW_EXPECT_FALSE( rig._service.isBlockedLocal( kBob, kAlice ) );
    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Unblock, kAlice, kBob ) == SocialResult::Ok );
    SW_EXPECT_FALSE( rig._service.isBlockedLocal( kAlice, kBob ) );
}

/** @brief [SocialServiceTest] 다른 서버가 관계를 바꾸면 버스 알림으로 다시 읽는다. 읽는 도중 알림이 와도 끝난 뒤 한 번 더 읽어 새 값이 된다 — 막음이 빠지면 차단이 샌다 */
SW_TEST_CASE( SocialServiceTest, BusChangeReloadsEvenDuringALoad )
{
    SocialWorld    world( true );
    SocialFixture  rigB( world._database, world._cacheData, &world._hub, kServerB );
    SocialFixture& rigA = world._rig;
    (void)rigA.links( kAlice );
    SW_EXPECT_FALSE( rigA._service.isBlockedLocal( kAlice, kBob ) );

    // A 가 읽기를 맡겨(이미 이전 값을 읽었다) 완료를 기다리는 동안 B 에서 막음을 쓴다
    rigA._service.listLinks( kAlice, 7 );
    SW_EXPECT_TRUE( rigB.change( SocialLinkOperation::Block, kAlice, kBob ) == SocialResult::Ok );
    rigA.pollBus();
    (void)rigA.takeAll();
    SW_EXPECT_TRUE( rigA._service.isBlockedLocal( kAlice, kBob ) );
    SW_EXPECT_EQUAL( 0, rigA._service.getPendingCount() );

    SW_EXPECT_TRUE( rigB.change( SocialLinkOperation::Unblock, kAlice, kBob ) == SocialResult::Ok ); // 평소의 길
    rigA.poll();
    SW_EXPECT_FALSE( rigA._service.isBlockedLocal( kAlice, kBob ) );
}

/** @brief [SocialServiceTest] 저장소 실패: 쓰기는 Unavailable 이고 알림이 없다. 읽기 실패는 메모리를 비우지 않는다 */
SW_TEST_CASE( SocialServiceTest, StoreFailures )
{
    SocialWorld    world;
    SocialFixture& rig = world._rig;
    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Block, kAlice, kBob ) == SocialResult::Ok );
    (void)rig.takeNotifications();

    world._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Request, kAlice, kCarol ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig.takeNotifications().empty() );

    world._database.armFault( ServiceStoreFault::RejectRead );
    rig._service.listLinks( kAlice, 9 );
    SW_EXPECT_TRUE( rig.take()._result.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig._service.isBlockedLocal( kAlice, kBob ) );
}

/** @brief [SocialServiceTest] 접속 상태: 친구에게 알리고, 친구가 아니면 안 알린다. 조회는 이 서버 계정은 메모리, 다른 서버 계정은 캐시에서 읽는다. 틀린 값은 Invalid 다 */
SW_TEST_CASE( SocialServiceTest, PresenceReachesFriends )
{
    SocialWorld    world( true );
    SocialFixture  rigB( world._database, world._cacheData, &world._hub, kServerB );
    SocialFixture& rigA = world._rig;
    rigA.befriend( kAlice, kBob );
    (void)rigA.links( kAlice );
    rigB.poll();
    (void)rigB.links( kBob );
    (void)rigA.takeNotifications();
    (void)rigB.takeNotifications();

    SW_EXPECT_TRUE( rigA.setPresence( kAlice, SocialPresenceStatus::Offline ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rigA.setPresence( kAlice, SocialPresenceStatus::Online, string( static_cast<size_t>( SocialLimit::kMaxActivitySize ) + 1, 'x' ) ).hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_TRUE( rigA.setPresence( kAlice, SocialPresenceStatus::Online, "dungeon" ) == SocialResult::Ok );
    const vector<SocialNotification> listB = rigB.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listB.size() ); // 다른 서버의 친구에게 버스로 간다
    SW_EXPECT_EQUAL( kBob, listB[0]._recipientID );
    SW_EXPECT_TRUE( listB[0]._kind == SocialNotificationKind::PresenceChanged );
    SW_EXPECT_EQUAL( string( "dungeon" ), listB[0]._presence._activity );

    const vector<SocialPresence> listFromCache = rigB.queryPresence( kBob );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFromCache.size() );
    SW_EXPECT_EQUAL( kAlice, listFromCache[0]._accountID );
    SW_EXPECT_TRUE( listFromCache[0]._status == SocialPresenceStatus::Online );
    SW_EXPECT_EQUAL( string( "dungeon" ), listFromCache[0]._activity );

    SW_EXPECT_TRUE( rigA.setPresence( kBob, SocialPresenceStatus::Online, "same" ) == SocialResult::Ok ); // Bob 이 A 에도 있는 경우 메모리에서 읽는다
    (void)rigA.links( kAlice );
    const vector<SocialPresence> listFromMemory = rigA.queryPresence( kAlice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFromMemory.size() );
    SW_EXPECT_EQUAL( string( "same" ), listFromMemory[0]._activity );
}

/** @brief [SocialServiceTest] 접속 상태 전달은 친구 역색인을 따른다 — 관계를 다시 읽거나(끊음) 계정이 떠나면 받는 사람에서 빠지고, 다시 오면 돌아온다. 친구가 아닌 계정과 자신은 받지 않는다 */
SW_TEST_CASE( SocialServiceTest, PresenceFanoutFollowsFriendIndex )
{
    constexpr AccountID kDave = 14; // 이 서버에 있지만 친구가 아니다
    SocialWorld         world;
    SocialFixture&      rig = world._rig;
    rig.befriend( kAlice, kBob );
    rig.befriend( kAlice, kCarol );
    (void)rig.links( kAlice );
    (void)rig.links( kBob );
    (void)rig.links( kCarol );
    (void)rig.links( kDave );
    (void)rig.takeNotifications();

    const auto collectRecipients = [&rig]( SocialPresenceStatus status )
    {
        SW_EXPECT_TRUE( rig.setPresence( kAlice, status ) == SocialResult::Ok );
        vector<AccountID> listRecipient;
        for ( const SocialNotification& notification : rig.takeNotifications() )
        {
            if ( notification._kind == SocialNotificationKind::PresenceChanged && notification._otherID == kAlice )
                listRecipient.push_back( notification._recipientID );
        }
        std::sort( listRecipient.begin(), listRecipient.end() );
        return listRecipient;
    };

    vector<AccountID> listRecipient = collectRecipients( SocialPresenceStatus::Online );
    SW_REQUIRE_EQUAL( size_t( 2 ), listRecipient.size() );
    SW_EXPECT_EQUAL( kBob, listRecipient[0] );
    SW_EXPECT_EQUAL( kCarol, listRecipient[1] );

    SW_EXPECT_TRUE( rig.change( SocialLinkOperation::Remove, kBob, kAlice ) == SocialResult::Ok ); // 두 사람 모두 다시 읽는다
    (void)rig.takeNotifications();
    listRecipient = collectRecipients( SocialPresenceStatus::Away );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRecipient.size() );
    SW_EXPECT_EQUAL( kCarol, listRecipient[0] );

    rig._service.removeAccount( kCarol ); // 접속 상태를 쓰지 않았으니 조용히 떠난다
    SW_EXPECT_TRUE( collectRecipients( SocialPresenceStatus::Busy ).empty() );

    (void)rig.links( kCarol ); // 다시 접속하면 역색인에 돌아온다
    listRecipient = collectRecipients( SocialPresenceStatus::Online );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRecipient.size() );
    SW_EXPECT_EQUAL( kCarol, listRecipient[0] );

    (void)rig.links( kCarol ); // 같은 목록을 다시 읽어도 두 번 받지 않는다
    SW_EXPECT_EQUAL( size_t( 1 ), collectRecipients( SocialPresenceStatus::InGame ).size() );
}

/** @brief [SocialServiceTest] 떠나면 친구에게 Offline 을 알리고 캐시 값을 지워 조회가 Offline 이다. 접속 상태를 쓰지 않은 계정은 조용히 떠난다 */
SW_TEST_CASE( SocialServiceTest, LeavingGoesOffline )
{
    SocialWorld    world;
    SocialFixture& rig = world._rig;
    rig.befriend( kAlice, kBob );
    (void)rig.links( kAlice );
    (void)rig.links( kBob );
    SW_EXPECT_TRUE( rig.setPresence( kAlice, SocialPresenceStatus::Online ) == SocialResult::Ok );
    (void)rig.takeNotifications();

    rig._service.removeAccount( kCarol ); // 모르는 계정
    rig._service.removeAccount( kAlice );
    const vector<SocialNotification> listNotification = rig.takeNotifications();
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotification.size() );
    SW_EXPECT_TRUE( listNotification[0]._presence._status == SocialPresenceStatus::Offline );

    const vector<SocialPresence> listPresence = rig.queryPresence( kBob );
    SW_REQUIRE_EQUAL( size_t( 1 ), listPresence.size() );
    SW_EXPECT_TRUE( listPresence[0]._status == SocialPresenceStatus::Offline );

    rig._service.removeAccount( kBob ); // 쓰지 않았으니 알림이 없다
    SW_EXPECT_TRUE( rig.takeNotifications().empty() );
}

/** @brief [SocialServiceTest] 접속 상태 값은 시한이 있어 주기마다 연장한다 — 연장이 멈춘(죽은) 서버의 계정은 시한 뒤 Offline 으로 보인다 */
SW_TEST_CASE( SocialServiceTest, PresenceExpiresWithoutRefresh )
{
    SocialWorld    world;
    SocialFixture  other( world._database, world._cacheData, nullptr, kServerB );
    SocialFixture& rig = world._rig;
    rig.befriend( kAlice, kBob );
    (void)other.links( kBob );
    constexpr int64 kTTLMs = 90000; // `SocialService.h` 의 시한
    SW_EXPECT_TRUE( rig.setPresence( kAlice, SocialPresenceStatus::Online ) == SocialResult::Ok );

    world._clock.advanceMilliseconds( kTTLMs - 1000 );
    rig._service.update( UnixMilliseconds{ kNowMs + kTTLMs - 1000 } ); // 연장한다
    rig.poll();
    world._clock.advanceMilliseconds( 2000 );
    SW_REQUIRE_EQUAL( size_t( 1 ), other.queryPresence( kBob ).size() );
    SW_EXPECT_TRUE( other.queryPresence( kBob )[0]._status == SocialPresenceStatus::Online );

    world._clock.advanceMilliseconds( kTTLMs + 1 ); // 더 연장하지 않는다
    SW_EXPECT_TRUE( other.queryPresence( kBob )[0]._status == SocialPresenceStatus::Offline );
}

/** @brief [SocialServiceTest] 다시 시도할 공통 범주(일시 장애 · 경합)는 요청 층이 기억하지 않아 같은 멱등 키의 관계 바꾸기 재시도가 서비스에 다시 닿고, 끝난 결과는 기억한다 */
SW_TEST_CASE( SocialServiceTest, RetryableReplyReachesTheServiceAgain )
{
    test::ReplyRetentionFixture<SocialProtocol, SocialLinkReply> rig;
    SocialLinkReply                                              finished;
    finished._result = SocialResult::Ok;
    rig.expectFailureReachesService( SocialMethod::RequestFriend::kID, OnlineError::kUnavailable, finished );
    rig.expectFailureReachesService( SocialMethod::RequestFriend::kID, OnlineError::kBusy, finished );
}

/**
 * @brief [SocialServiceTest] 계정 삭제 → 친구 키트의 관계 표에 그 계정의 레코드 0: 친구 · 보낸 신청 · 나를 막은 사람(내 쪽 줄이 없다)까지 지워지고 상대 목록이 비며 개수가 돌아온다(감사 P0-7)
 * @details 변이: 역색인(`social_blocked_by`)을 빼면 나를 막은 사람의 줄이 남아 진다.
 */
SW_TEST_CASE( SocialServiceTest, AccountDeletionErasesLinks )
{
    constexpr AccountID kBlocker = 15;
    SocialWorld         world;
    SocialFixture&      rig = world._rig;
    SW_REQUIRE_TRUE( rig.change( SocialLinkOperation::Request, kAlice, kBob ) == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.change( SocialLinkOperation::Accept, kBob, kAlice ) == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.change( SocialLinkOperation::Request, kAlice, kCarol ) == SocialResult::Ok );
    SW_REQUIRE_TRUE( rig.change( SocialLinkOperation::Block, kBlocker, kAlice ) == SocialResult::Ok );
    (void)rig.takeNotifications();

    SW_REQUIRE_TRUE( SocialService::eraseAccount( world._database, kAlice, kNowMs ) == ServiceStoreResult::Ok );
    const string aliceHex = ServiceKeyUtil::makeHex64( kAlice );
    for ( const utf8* pTable : { "social_link", "social_count", "social_blocked_by" } )
    {
        vector<ServiceRecord> listRecord;
        SW_REQUIRE_TRUE( world._database.listRecords( hashed_string( pTable ), "", "", 100, false, listRecord ) == ServiceStoreResult::Ok );
        for ( const ServiceRecord& record : listRecord )
        {
            SW_EXPECT_TRUE( record._key.find( aliceHex ) == string::npos );
        }
    }
    SW_EXPECT_TRUE( rig.links( kBob ).empty() );
    SW_EXPECT_TRUE( rig.links( kCarol ).empty() );
    SW_EXPECT_TRUE( rig.links( kBlocker ).empty() );
    SW_EXPECT_TRUE( SocialService::eraseAccount( world._database, kAlice, kNowMs ) == ServiceStoreResult::Ok ); // 멱등
}
