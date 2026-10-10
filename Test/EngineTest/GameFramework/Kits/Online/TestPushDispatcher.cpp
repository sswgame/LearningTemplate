// 푸시 발송기 — 등록한 기기 둘에 보냄(같은 토큰 다시 등록은 하나, 이 빌드에 없는 제공자는 건너뜀), 무효 토큰은 지움(다음 알림은 남은 기기만), 일시 실패는 물러났다
// 다시(1 · 2 초), 다섯 번이면 버림, 계정 도배 제한, 계정마다 기기 10 개(넘으면 가장 오래된 것), 해지 · 규칙 밖 등록.
#include "pch.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct PushNode
    {
        MemoryServiceStore           _store;
        FakePushProvider             _provider;
        PushNotificationDispatcher   _dispatcher;
        vector<PushDeviceCompletion> _listCompletion;

        explicit PushNode( MemoryServiceDatabase* pDatabase )
            : _store{ pDatabase }
            , _provider{}
            , _dispatcher{}
            , _listCompletion{}
        {
            _dispatcher.initialize( &_store, PushDispatcherSettings{} );
            SW_EXPECT_TRUE( _dispatcher.registerProvider( &_provider ) );
        }

        ~PushNode()
        {
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        void step( int64 nowMs )
        {
            for ( int32 round = 0; round < 3; ++round )
            {
                (void)_store.pollCompletions();
                _dispatcher.tick( nowMs );
            }
            _dispatcher.drainCompletions( _listCompletion );
        }

        void registerDevice( AccountID accountID, string_view providerID, string_view token, int64 nowMs )
        {
            PushDeviceRegistration registration;
            registration._providerID   = string( providerID );
            registration._token        = string( token );
            registration._locale       = "ko-KR";
            registration._registeredMs = nowMs;
            _dispatcher.registerDevice( accountID, registration, 1 );
            step( nowMs );
        }
    };

    struct PushDispatcherTestInternal
    {
        static PushNotificationMessage makeMessage()
        {
            PushNotificationMessage message;
            message._titleKey = "push.mail.title";
            message._bodyKey  = "push.mail.body";
            message._deepLink = "mailbox";
            return message;
        }
    };
} // namespace

SW_TEST_CASE( PushDispatcherTest, SendsToEveryDeviceAndDropsInvalidTokens )
{
    using Internal = PushDispatcherTestInternal;
    MemoryServiceDatabase database;
    PushNode              node( &database );
    node.registerDevice( 1, "fake", "token-phone", 0 );
    node.registerDevice( 1, "fake", "token-tablet", 0 );
    node.registerDevice( 1, "fake", "token-phone", 10 ); // 같은 토큰 — 덮음
    node.registerDevice( 1, "apns", "token-ios", 10 );   // 이 빌드에 없는 제공자 — 등록은 되지만 보내지 않는다
    SW_EXPECT_EQUAL( database.countRecords( hashed_string( "liveops_device" ) ), 3 );
    node._provider.scriptResult( "token-tablet", PushDeliveryStatus::InvalidToken );

    SW_ASSERT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 100 ) );
    node.step( 100 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( node._dispatcher.getStats()._invalidTokenCount, uint64( 1 ) );
    SW_EXPECT_EQUAL( database.countRecords( hashed_string( "liveops_device" ) ), 2 );

    SW_ASSERT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 200 ) );
    node.step( 200 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 3 ) ); // 태블릿은 지워졌다
    SW_EXPECT_STREQ( node._provider.getSent().back()._deviceToken.c_str(), "token-phone" );
    SW_EXPECT_STREQ( node._provider.getSent().back()._message._deepLink.c_str(), "mailbox" );
    SW_EXPECT_EQUAL( node._dispatcher.getStats()._deliveredCount, uint64( 2 ) );
    SW_EXPECT_EQUAL( node._dispatcher.getInFlightCount(), 0 );
}

SW_TEST_CASE( PushDispatcherTest, TransientFailuresBackOffThenGiveUp )
{
    using Internal = PushDispatcherTestInternal;
    MemoryServiceDatabase database;
    PushNode              node( &database );
    node.registerDevice( 1, "fake", "token-a", 0 );
    node._provider.scriptResult( "token-a", PushDeliveryStatus::Transient, 2 );
    SW_ASSERT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 0 ) );
    node.step( 0 ); // 1 차 실패 → 1 초 뒤
    node.step( 999 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 1 ) );
    node.step( 1000 ); // 2 차 실패 → 2 초 뒤
    node.step( 2999 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 2 ) );
    node.step( 3000 ); // 3 차 성공
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( node._dispatcher.getStats()._deliveredCount, uint64( 1 ) );

    node._provider.scriptResult( "token-a", PushDeliveryStatus::RateLimited, 1, 5000 ); // 제공자가 늦추라 한 시간이 물러남보다 길면 그만큼
    SW_ASSERT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 4000 ) );
    node.step( 4000 );
    node.step( 8999 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 4 ) );
    node.step( 9000 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 5 ) );

    node._provider.scriptResult( "token-a", PushDeliveryStatus::Transient, 99 );
    SW_ASSERT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 10000 ) );
    for ( int64 nowMs = 10000; nowMs <= 60000; nowMs += 1000 )
    {
        node.step( nowMs );
    }
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 5 + PushLimit::kMaxRetry ) );
    SW_EXPECT_EQUAL( node._dispatcher.getStats()._droppedCount, uint64( 1 ) );
    SW_EXPECT_EQUAL( node._dispatcher.getInFlightCount(), 0 );
}

SW_TEST_CASE( PushDispatcherTest, AccountRateLimit )
{
    using Internal = PushDispatcherTestInternal;
    MemoryServiceDatabase database;
    PushNode              node( &database );
    node.registerDevice( 1, "fake", "token-a", 0 );
    for ( int32 index = 0; index < 6; ++index )
    {
        SW_EXPECT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 0 ) );
    }
    SW_EXPECT_FALSE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), 0 ) );
    SW_EXPECT_EQUAL( node._dispatcher.getStats()._rateLimitedCount, uint64( 1 ) );
    SW_EXPECT_TRUE( node._dispatcher.notifyAccount( 2, Internal::makeMessage(), 0 ) ); // 계정마다 따로
    SW_EXPECT_TRUE( node._dispatcher.notifyAccount( 1, Internal::makeMessage(), PushDispatcherSettings{}._accountRefillIntervalMs ) );
    node.step( 0 );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 7 ) ); // 계정 2 는 기기가 없다
}

SW_TEST_CASE( PushDispatcherTest, DeviceLimitEvictsTheOldestAndUnregister )
{
    MemoryServiceDatabase database;
    PushNode              node( &database );
    const hashed_string   table( "liveops_device" );
    for ( int32 index = 0; index < PushLimit::kMaxDevicePerAccount; ++index )
    {
        node.registerDevice( 1, "fake", "token-" + std::to_string( index ), 100 + index );
    }
    SW_EXPECT_EQUAL( database.countRecords( table ), PushLimit::kMaxDevicePerAccount );
    node.registerDevice( 1, "fake", "token-new", 500 ); // 한도 — 가장 오래된 token-0 을 밀어낸다
    SW_EXPECT_EQUAL( database.countRecords( table ), PushLimit::kMaxDevicePerAccount );
    ServiceRecord record;
    SW_EXPECT_TRUE( database.readRecord( table, PushNotificationDispatcher::makeDeviceKey( 1, "fake", "token-0" ), record ) == ServiceStoreResult::NotFound );
    SW_EXPECT_TRUE( database.readRecord( table, PushNotificationDispatcher::makeDeviceKey( 1, "fake", "token-new" ), record ) == ServiceStoreResult::Ok );

    node._listCompletion.clear();
    node._dispatcher.unregisterDevice( 1, "fake", "token-new", 7 );
    node._dispatcher.unregisterDevice( 1, "fake", "token-new", 8 ); // 이미 없다
    PushDeviceRegistration broken;
    broken._providerID = "Bad Provider";
    broken._token      = "x";
    node._dispatcher.registerDevice( 1, broken, 9 );
    node.step( 600 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 3 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._result == LiveOpsResult::Invalid ); // 규칙 밖은 맡기기 전에
    SW_EXPECT_EQUAL( node._listCompletion[0]._requestTag, uint64( 9 ) );
    SW_EXPECT_TRUE( node._listCompletion[1]._result == LiveOpsResult::Ok );
    SW_EXPECT_TRUE( node._listCompletion[2]._result == LiveOpsResult::NotFound );
    SW_EXPECT_EQUAL( database.countRecords( table ), PushLimit::kMaxDevicePerAccount - 1 );
    SW_EXPECT_EQUAL( node._dispatcher.getPendingWorkCount(), 0 );
}
