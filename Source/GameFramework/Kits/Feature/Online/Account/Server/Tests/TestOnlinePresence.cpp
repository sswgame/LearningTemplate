// OnlinePresenceTest — 서버 여러 대의 접속 상태: 한 서버가 적은 계정을 다른 서버가 id · 표시 이름(대소문자 무시)으로 찾고, 떠나면 지우되 다른 서버로 옮긴 기록은 남기며,
// 캐시가 없는 호스트의 찾기는 다음 틱에 "없음" 으로 끝나고, 놓은 주인 몫의 찾기는 부르지 않는다. 서버 둘이 메모리 캐시 · 로컬 버스를 나눈다(`OnlineHostTestUtil.h`).
// 알림 중계(`relayRemotePush`)는 거래 · 우편 끝단 테스트의 몫이다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Service/OnlinePresence.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs  = 1700000000000ll;
    constexpr AccountID kAlice  = 42;
    constexpr uint64    kServer = 1;
    constexpr uint64    kOther  = 2;

    struct FoundSink
    {
        vector<AccountPresenceResult> _listResult{};

        void onFound( const AccountPresenceResult& result ) { _listResult.push_back( result ); }
    };

    /** @brief 캐시 · 버스를 나눈 서버 둘과 서버마다의 접속 상태입니다. 접속 상태는 호스트가 살아 있을 때 내린다. */
    struct PresenceFixture
    {
        LoopbackStreamNetwork   _network{ 9u };
        MemoryServiceDatabase   _database{};
        SystemMonotonicClock    _cacheClock{};
        MemoryEphemeralDatabase _cacheDatabase{ _cacheClock };
        LocalServerBusHub       _hub{};
        test::OnlineTestServer  _serverA{ _network, &_database, &_cacheDatabase, &_hub, kServer };
        test::OnlineTestServer  _serverB{ _network, &_database, &_cacheDatabase, &_hub, kOther };
        OnlinePresence          _presenceA{};
        OnlinePresence          _presenceB{};
        FoundSink               _sink{};
        Subscription            _ownerB{};

        PresenceFixture()
        {
            _serverA.start();
            _serverB.start();
            _presenceA.attach( &_serverA._host );
            _presenceB.attach( &_serverB._host );
            _ownerB = _presenceB.openFindOwner();
        }

        ~PresenceFixture()
        {
            _ownerB.reset();
            _presenceA.shutdown();
            _presenceB.shutdown();
        }

        void poll()
        {
            for ( int32 round = 0; round < 6; ++round )
            {
                _serverA.update( kNowMs );
                _serverB.update( kNowMs );
                _presenceA.update( UnixMilliseconds{ kNowMs } );
                _presenceB.update( UnixMilliseconds{ kNowMs } );
            }
        }

        AccountPresenceDelegate makeDelegate() { return AccountPresenceDelegate::create<&FoundSink::onFound>( &_sink ); }

        /** @brief 서버 B 에서 찾고 결과 하나를 받습니다. */
        AccountPresenceResult findAccountFromB( AccountID accountID )
        {
            _sink._listResult.clear();
            const uint64 lookupID = _presenceB.submitFindByAccount( accountID, makeDelegate(), _ownerB );
            poll();
            return takeOne( lookupID );
        }

        AccountPresenceResult findNameFromB( string_view displayName )
        {
            _sink._listResult.clear();
            const uint64 lookupID = _presenceB.submitFindByDisplayName( displayName, makeDelegate(), _ownerB );
            poll();
            return takeOne( lookupID );
        }

        AccountPresenceResult takeOne( uint64 lookupID )
        {
            SW_EXPECT_TRUE( lookupID != 0 );
            SW_EXPECT_EQUAL( size_t( 1 ), _sink._listResult.size() );
            if ( _sink._listResult.empty() )
                return AccountPresenceResult{};
            SW_EXPECT_EQUAL( lookupID, _sink._listResult[0]._requestID );
            return _sink._listResult[0];
        }
    };

    struct OnlinePresenceTestUtil
    {
        static AccountIdentity makeIdentity( AccountID accountID, const utf8* pDisplayName )
        {
            AccountIdentity identity;
            identity._accountID   = accountID;
            identity._displayName = pDisplayName;
            return identity;
        }
    };
} // namespace

/** @brief [OnlinePresenceTest] 서버 A 에 접속한 계정을 서버 B 가 계정 id 와 표시 이름(대소문자 무시)으로 찾고, 접속한 서버를 받는다 */
SW_TEST_CASE( OnlinePresenceTest, OtherServerFindsTheAccountByIDAndName )
{
    PresenceFixture rig;
    SW_REQUIRE_TRUE( rig._presenceA.isEnabled() && rig._presenceB.isEnabled() );
    rig._presenceA.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alice" ) );
    rig.poll();

    const AccountPresenceResult byAccount = rig.findAccountFromB( kAlice );
    SW_EXPECT_TRUE( byAccount.isOnline() );
    SW_EXPECT_EQUAL( kServer, byAccount._serverID );
    SW_EXPECT_EQUAL( kAlice, byAccount._identity._accountID );

    const AccountPresenceResult byName = rig.findNameFromB( "ALICE" );
    SW_EXPECT_TRUE( byName.isOnline() );
    SW_EXPECT_EQUAL( kServer, byName._serverID );
    SW_EXPECT_EQUAL( kAlice, byName._identity._accountID );
    SW_EXPECT_EQUAL( string( "Alice" ), byName._identity._displayName );

    SW_EXPECT_FALSE( rig.findAccountFromB( kAlice + 1 ).isOnline() );
    SW_EXPECT_FALSE( rig.findNameFromB( "Bob" ).isOnline() );
}

/** @brief [OnlinePresenceTest] 떠나면 기록을 지우지만, 그사이 다른 서버에 새로 접속했으면 그쪽 기록을 남긴다(내 것일 때만 지운다) */
SW_TEST_CASE( OnlinePresenceTest, LeavingErasesOnlyItsOwnRecord )
{
    PresenceFixture rig;
    rig._presenceA.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alice" ) );
    rig.poll();
    rig._presenceA.recordOffline( kAlice );
    rig.poll();
    SW_EXPECT_FALSE( rig.findAccountFromB( kAlice ).isOnline() );
    SW_EXPECT_FALSE( rig.findNameFromB( "Alice" ).isOnline() );

    rig._presenceA.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alice" ) );
    rig.poll();
    rig._presenceB.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alice" ) ); // 새 로그인이 이긴다
    rig.poll();
    rig._presenceA.recordOffline( kAlice ); // A 의 늦은 정리가 B 의 기록을 지우면 안 된다
    rig.poll();
    const AccountPresenceResult moved = rig.findAccountFromB( kAlice );
    SW_EXPECT_TRUE( moved.isOnline() );
    SW_EXPECT_EQUAL( kOther, moved._serverID );
}

/** @brief [OnlinePresenceTest] 표시 이름이 바뀌면 이전 이름으로는 찾지 못하고 새 이름으로 찾는다 */
SW_TEST_CASE( OnlinePresenceTest, RenameMovesTheNameRecord )
{
    PresenceFixture rig;
    rig._presenceA.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alice" ) );
    rig.poll();
    rig._presenceA.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alicia" ) );
    rig.poll();
    SW_EXPECT_FALSE( rig.findNameFromB( "Alice" ).isOnline() );
    const AccountPresenceResult renamed = rig.findNameFromB( "alicia" );
    SW_EXPECT_TRUE( renamed.isOnline() );
    SW_EXPECT_EQUAL( kAlice, renamed._identity._accountID );
}

/** @brief [OnlinePresenceTest] 캐시 · 버스가 없는(붙지 않은) 접속 상태의 찾기는 맡긴 함수 안이 아니라 다음 틱에 "없음" 으로 한 번 끝난다 */
SW_TEST_CASE( OnlinePresenceTest, DetachedFindEndsNotFoundOnTheNextTick )
{
    OnlinePresence presence;
    presence.attach( nullptr );
    SW_EXPECT_FALSE( presence.isEnabled() );
    Subscription owner = presence.openFindOwner();
    FoundSink    sink;
    const uint64 lookupID = presence.submitFindByAccount( kAlice, AccountPresenceDelegate::create<&FoundSink::onFound>( &sink ), owner );
    SW_EXPECT_TRUE( lookupID != 0 );
    SW_EXPECT_TRUE( sink._listResult.empty() );
    presence.update( UnixMilliseconds{ kNowMs } );
    SW_REQUIRE_EQUAL( size_t( 1 ), sink._listResult.size() );
    SW_EXPECT_EQUAL( lookupID, sink._listResult[0]._requestID );
    SW_EXPECT_FALSE( sink._listResult[0].isOnline() );
    presence.update( UnixMilliseconds{ kNowMs } );
    SW_EXPECT_EQUAL( size_t( 1 ), sink._listResult.size() );
    owner.reset();
}

/** @brief [OnlinePresenceTest] 주인 몫을 놓은 찾기는 답이 와도 부르지 않고, 살아 있지 않은 몫으로는 맡기지 않는다(오류) */
SW_TEST_CASE( OnlinePresenceTest, ReleasedOwnerIsNotCalled )
{
    PresenceFixture rig;
    rig._presenceA.recordOnline( OnlinePresenceTestUtil::makeIdentity( kAlice, "Alice" ) );
    rig.poll();
    Subscription owner = rig._presenceB.openFindOwner();
    (void)rig._presenceB.submitFindByAccount( kAlice, rig.makeDelegate(), owner );
    owner.reset();
    rig.poll();
    SW_EXPECT_TRUE( rig._sink._listResult.empty() );

    SW_EXPECT_LOG( "OnlinePresence.OnlinePresenceFindWithoutLive" );
    SW_EXPECT_EQUAL( uint64( 0 ), rig._presenceB.submitFindByAccount( kAlice, rig.makeDelegate(), owner ) );
}
