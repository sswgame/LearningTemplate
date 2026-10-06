// 친구 로직 — 신청 → 수락 → 목록, 서로 신청은 친구 하나, 같은 판을 읽은 두 커밋의 경합은 다시 읽어 친구로(충돌 다시 하기), 이름으로 신청(정식 계정만 — 없는 이름 NotFound),
// 막기는 차단 조회에 바로 보이고 막힌 사람의 신청은 알림 없는 Ok, 접속 상태가 다른 서버의 친구에게 알려지고 질의는 캐시를 읽는다, 저장소 실패는 Unavailable 이고 아무것도 쓰지 않음.
#include "pch.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Identity/AccountNameIndex.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Social/SocialService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 시험 이름 색인 — 이름(소문자) → 신원. 게스트는 넣지 않는다(실제 색인과 같다). */
    class FakeNameIndex final : public IAccountNameIndex
    {
    public:
        FakeNameIndex()
            : _listIdentity{}
        {
        }

        void add( AccountId accountId, string_view displayName ) { _listIdentity.push_back( AccountIdentity{ string( displayName ), accountId, SW_FALSE } ); }

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

        ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountId accountId, AccountIdentity& outIdentity ) const override
        {
            (void)connection;
            for ( const AccountIdentity& identity : _listIdentity )
            {
                if ( identity._accountId == accountId )
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

    /** @brief 서버 프로세스 하나의 친구 로직 — 저장소 앞 · 캐시 앞 · 라우터 · 로컬 버스. 버스 메시지는 호스트 대신 시험이 비워 넘긴다. */
    struct SocialNode
    {
        MemoryServiceStore         _store;
        MemoryEphemeralStore       _cache;
        EphemeralStoreRouter       _router;
        LocalServerBus             _bus;
        SocialService              _service;
        vector<SocialCompletion>   _listCompletion;
        vector<SocialNotification> _listNotification;
        IServiceStore*             _pFront; ///< 서비스가 쓰는 저장소 앞(시험이 끼워 넣는 앞일 수 있다)

        SocialNode( MemoryServiceDatabase* pDatabase, MemoryEphemeralDatabase* pCacheDatabase, LocalServerBusHub* pHub, uint64 serverId,
                    const IAccountNameIndex* pNameIndex = nullptr, IServiceStore* pStore = nullptr )
            : _store{ pDatabase }
            , _cache{ pCacheDatabase }
            , _router{}
            , _bus{ pHub, serverId }
            , _service{}
            , _listCompletion{}
            , _listNotification{}
            , _pFront{ pStore != nullptr ? pStore : &_store }
        {
            _router.initialize( &_cache );
            _bus.subscribe( SocialBus::kLinksTopic );
            _bus.subscribe( SocialBus::kPresenceTopic );
            SocialServiceDependencies dependencies;
            dependencies._pStore     = _pFront;
            dependencies._pRouter    = &_router;
            dependencies._pBus       = &_bus;
            dependencies._pNameIndex = pNameIndex;
            _service.initialize( dependencies );
        }

        void step( int64 nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                vector<ServerBusMessage> listMessage;
                (void)_bus.pollMessages( listMessage );
                for ( const ServerBusMessage& message : listMessage )
                {
                    if ( message._originServerId != _bus.getServerId() )
                        _service.handleBusMessage( message._topic, message._bytes );
                }
                _service.tick( nowMs );
                (void)_pFront->pollCompletions();
                (void)_router.pump();
            }
            _service.drainCompletions( _listCompletion );
            _service.drainNotifications( _listNotification );
        }

        SocialResult getLastResult() const { return _listCompletion.empty() ? SocialResult::Count : _listCompletion.back()._result; }

        /** @brief @p accountId 의 관계 목록을 읽어 마지막 완료로 둡니다. */
        const vector<SocialLink>& readLinks( AccountId accountId, uint64 requestTag, int64 nowMs )
        {
            _service.listLinks( accountId, requestTag );
            step( nowMs );
            return _listCompletion.back()._listLink;
        }
    };

    /** @brief 맡은 일의 첫 커밋 직전에 다른 서비스의 요청을 끼워 넣는 연결 — 같은 판을 읽은 두 커밋의 경합을 결정적으로 만든다. */
    class InterleavingConnection final : public IServiceStoreConnection
    {
    public:
        InterleavingConnection( MemoryServiceDatabase* pDatabase, SocialService* pRival, AccountId rivalId, AccountId targetId )
            : _pDatabase{ pDatabase }
            , _pRival{ pRival }
            , _rivalId{ rivalId }
            , _targetId{ targetId }
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
                _pRival->changeLink( SocialLinkOperation::Request, _rivalId, _targetId, 0, 99 ); // 메모리 앞은 그 자리에서 돈다 — 상대의 신청이 먼저 들어간다
            }
            return _pDatabase->commit( transaction, pOutInfo );
        }

    private:
        MemoryServiceDatabase* _pDatabase;
        SocialService*         _pRival;
        AccountId              _rivalId;
        AccountId              _targetId;
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
                work->complete();
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
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    SocialNode              node( &database, &cacheDatabase, &hub, 1 );
    node._service.changeLink( SocialLinkOperation::Request, 1, 2, 100, 1 );
    node.step( 100 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listNotification[0]._recipientId, AccountId( 2 ) );
    SW_EXPECT_TRUE( node._listNotification[0]._kind == SocialNotificationKind::FriendRequested );

    node._service.changeLink( SocialLinkOperation::Request, 1, 2, 150, 2 );
    node.step( 150 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::AlreadyRequested );
    node._service.changeLink( SocialLinkOperation::Accept, 2, 1, 200, 3 );
    node.step( 200 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_EXPECT_TRUE( node._listNotification.back()._kind == SocialNotificationKind::FriendAdded );

    const vector<SocialLink>& listLink = node.readLinks( 1, 4, 200 );
    SW_ASSERT_EQUAL( listLink.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLink[0]._state == SocialLinkState::Friend );
    SW_EXPECT_EQUAL( listLink[0]._otherId, AccountId( 2 ) );
    SW_EXPECT_EQUAL( listLink[0]._sinceMs, int64( 200 ) );

    node._service.changeLink( SocialLinkOperation::Request, 1, 1, 300, 5 ); // 자기 자신
    node.step( 300 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Invalid );
}

SW_TEST_CASE( SocialServiceTest, ConcurrentMutualRequestsRetryIntoOneFriendship )
{
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    SocialNode              rival( &database, &cacheDatabase, &hub, 2 );
    InterleavingConnection  connection( &database, &rival._service, 2, 1 );
    InterleavingStore       store( &connection );
    SocialNode              node( &database, &cacheDatabase, &hub, 1, nullptr, &store );

    const uint64 commitBefore = database.getCommitCount();
    node._service.changeLink( SocialLinkOperation::Request, 1, 2, 0, 1 ); // 첫 커밋 직전에 2 → 1 신청이 끼어든다 — 충돌 → 다시 읽으면 "받은 신청" → 친구
    node.step( 0 );
    rival.step( 0 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_EXPECT_TRUE( rival.getLastResult() == SocialResult::Ok );
    SW_EXPECT_EQUAL( database.getCommitCount(), commitBefore + 2 ); // 끼어든 신청 하나 + 다시 해서 수락 하나(충돌한 커밋은 세지 않는다)

    const vector<SocialLink> listLinkOfFirst = node.readLinks( 1, 2, 0 );
    SW_ASSERT_EQUAL( listLinkOfFirst.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLinkOfFirst[0]._state == SocialLinkState::Friend );
    const vector<SocialLink> listLinkOfSecond = rival.readLinks( 2, 2, 0 );
    SW_ASSERT_EQUAL( listLinkOfSecond.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listLinkOfSecond[0]._state == SocialLinkState::Friend );

    // 개수도 맞다 — 받은 · 보낸 신청이 남지 않았으니 같은 둘이 다시 친구가 되어도 상한이 새지 않는다(셋째 계정이 1 에게 신청할 수 있다)
    node._service.changeLink( SocialLinkOperation::Remove, 1, 2, 10, 3 );
    node.step( 10 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Ok );
    SW_EXPECT_EQUAL( node.readLinks( 1, 4, 10 ).size(), size_t( 0 ) );
}

SW_TEST_CASE( SocialServiceTest, RequestByNameFindsRegisteredAccountsOnly )
{
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    FakeNameIndex           nameIndex;
    nameIndex.add( 2, "bob" );
    SocialNode node( &database, &cacheDatabase, &hub, 1, &nameIndex );

    node._service.requestFriendByName( 1, "bob", 0, 1 );
    node.step( 0 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._result == SocialResult::Ok );
    SW_EXPECT_EQUAL( node._listCompletion[0]._otherId, AccountId( 2 ) );
    SW_ASSERT_EQUAL( node._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listNotification[0]._recipientId, AccountId( 2 ) );

    node._service.requestFriendByName( 1, "Guest-00abcd", 0, 2 ); // 게스트 이름은 색인에 없다 — 계정 id 로
    node.step( 0 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::NotFound );
    node._service.requestFriendByName( 2, "bob", 0, 3 ); // 자기 자신
    node.step( 0 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Invalid );
}

SW_TEST_CASE( SocialServiceTest, BlockIsVisibleLocallyAndHidesRequests )
{
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    SocialNode              first( &database, &cacheDatabase, &hub, 1 );
    SocialNode              second( &database, &cacheDatabase, &hub, 2 );
    (void)second.readLinks( 2, 1, 0 ); // 계정 2 는 서버 2 에 붙어 있고 관계를 읽어 두었다
    second._service.changeLink( SocialLinkOperation::Block, 2, 1, 100, 2 );
    second.step( 100 );
    SW_EXPECT_TRUE( second.getLastResult() == SocialResult::Ok );
    SW_EXPECT_TRUE( second._service.isBlockedLocal( 2, 1 ) );
    SW_EXPECT_FALSE( second._service.isBlockedLocal( 1, 2 ) );

    first._service.changeLink( SocialLinkOperation::Request, 1, 2, 200, 1 ); // 막힌 사람의 신청 — 조용히 Ok, 아무 데도 알림 없음
    first.step( 200 );
    second.step( 200 );
    SW_EXPECT_TRUE( first.getLastResult() == SocialResult::Ok );
    SW_EXPECT_EQUAL( first._listNotification.size(), size_t( 0 ) );
    SW_EXPECT_EQUAL( first.readLinks( 1, 2, 200 ).size(), size_t( 0 ) );

    second._service.changeLink( SocialLinkOperation::Request, 2, 1, 250, 3 ); // 내가 막은 사람에게
    second.step( 250 );
    SW_EXPECT_TRUE( second.getLastResult() == SocialResult::YouBlocked );

    second._service.changeLink( SocialLinkOperation::Unblock, 2, 1, 300, 4 );
    second.step( 300 );
    SW_EXPECT_FALSE( second._service.isBlockedLocal( 2, 1 ) );
}

SW_TEST_CASE( SocialServiceTest, PresenceReachesFriendsOnOtherServersAndQueryReadsCache )
{
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    SocialNode              first( &database, &cacheDatabase, &hub, 1 );
    SocialNode              second( &database, &cacheDatabase, &hub, 2 );
    first._service.changeLink( SocialLinkOperation::Request, 1, 2, 0, 1 );
    first.step( 0 );
    second._service.changeLink( SocialLinkOperation::Accept, 2, 1, 0, 1 );
    second.step( 0 );
    (void)first.readLinks( 1, 2, 0 );  // 1 은 서버 1
    (void)second.readLinks( 2, 2, 0 ); // 2 는 서버 2

    first._listNotification.clear(); // 앞 단계의 신청 · 친구 알림
    second._service.setPresence( 2, SocialPresenceStatus::InGame, "dungeon 3", 100, 3 );
    second.step( 100 );
    first.step( 100 );
    SW_ASSERT_EQUAL( first._listNotification.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( first._listNotification[0]._recipientId, AccountId( 1 ) );
    SW_EXPECT_TRUE( first._listNotification[0]._kind == SocialNotificationKind::PresenceChanged );
    SW_EXPECT_STREQ( first._listNotification[0]._presence._activity.c_str(), "dungeon 3" );

    first._service.queryFriendPresence( 1, 4 ); // 2 는 다른 서버 — 캐시를 읽는다
    first.step( 100 );
    SW_ASSERT_EQUAL( first._listCompletion.back()._listPresence.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( first._listCompletion.back()._listPresence[0]._status == SocialPresenceStatus::InGame );

    second._service.removeAccount( 2 ); // 떠남 — 오프라인 알림 · 캐시 지움
    second.step( 200 );
    first.step( 200 );
    SW_EXPECT_TRUE( first._listNotification.back()._presence._status == SocialPresenceStatus::Offline );
    first._service.queryFriendPresence( 1, 5 );
    first.step( 200 );
    SW_ASSERT_EQUAL( first._listCompletion.back()._listPresence.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( first._listCompletion.back()._listPresence[0]._status == SocialPresenceStatus::Offline );

    second._service.setPresence( 2, SocialPresenceStatus::Offline, "", 300, 6 ); // 오프라인은 떠나기로만
    second.step( 300 );
    SW_EXPECT_TRUE( second.getLastResult() == SocialResult::Invalid );
}

SW_TEST_CASE( SocialServiceTest, StoreFailureWritesNothing )
{
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    SocialNode              node( &database, &cacheDatabase, &hub, 1 );
    const uint64            hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    node._service.changeLink( SocialLinkOperation::Request, 1, 2, 0, 1 );
    node.step( 0 );
    SW_EXPECT_TRUE( node.getLastResult() == SocialResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
    SW_EXPECT_EQUAL( node._listNotification.size(), size_t( 0 ) );
}
