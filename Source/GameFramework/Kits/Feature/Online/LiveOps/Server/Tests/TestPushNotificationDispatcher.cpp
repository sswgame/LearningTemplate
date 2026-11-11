// PushDispatcherTest — 푸시 발송기(메모리 저장소 + 가짜 제공자): 기기마다 배달, 잘못된 토큰이면 기기 지우기, 일시 실패의 백오프와 상한, 제공자가 준 대기, 거절, 계정 도배 제한, 기기 한도와 밀어내기, 등록 · 해지의 완료, 토픽 한 번만.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs   = 1700000000000ll;
    constexpr AccountID kAccount = 42;

    /** @brief 발송기 + 메모리 저장소 + 가짜 제공자입니다. */
    struct DispatcherFixture
    {
        MemoryServiceDatabase      _database;
        MemoryServiceStore         _store;
        FakePushProvider           _provider;
        PushNotificationDispatcher _dispatcher;
        int64                      _nowMs;

        explicit DispatcherFixture( const PushDispatcherConfig& settings = PushDispatcherConfig{} )
            : _database{}
            , _store{ &_database }
            , _provider{}
            , _dispatcher{}
            , _nowMs{ kNowMs }
        {
            _dispatcher.initialize( &_store, settings );
            SW_EXPECT_TRUE( _dispatcher.registerProvider( &_provider ) );
        }

        void pump()
        {
            for ( int32 round = 0; round < 3; ++round )
            {
                while ( _store.pollCompletions() > 0 )
                {
                }
                _dispatcher.update( UnixMilliseconds{ _nowMs } );
            }
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        void addDevice( string_view token, int64 registeredMs = kNowMs, AccountID accountID = kAccount, string_view providerID = "fake" )
        {
            PushDeviceRegistration registration;
            registration._providerID   = string( providerID );
            registration._token        = string( token );
            registration._locale       = "ko-KR";
            registration._registeredMs = registeredMs;
            _dispatcher.registerDevice( accountID, registration, 0 );
            pump();
        }

        void notify()
        {
            PushNotificationMessage message;
            message._titleKey = "title.key";
            SW_EXPECT_TRUE( _dispatcher.notifyAccount( kAccount, message, UnixMilliseconds{ _nowMs } ) );
            pump();
        }

        LiveOpsServiceResult takeCompletion()
        {
            vector<PushDeviceCompletion> listCompletion;
            _dispatcher.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            return listCompletion.empty() ? LiveOpsServiceResult::fail( OnlineError::kUnavailable ) : listCompletion[0]._result;
        }
    };
} // namespace

/** @brief [PushDispatcherTest] 계정의 기기마다 한 번씩 보내고 끝난다. 이 서버에 없는 제공자의 기기는 건너뛴다 */
SW_TEST_CASE( PushDispatcherTest, DeliversToEveryDevice )
{
    DispatcherFixture rig;
    rig.addDevice( "a" );
    rig.addDevice( "b" );
    rig.addDevice( "c", kNowMs, kAccount, "other" );
    rig.addDevice( "d", kNowMs, kAccount + 1 );
    rig.notify();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._provider.getSent().size() );
    SW_EXPECT_EQUAL( uint64( 2 ), rig._dispatcher.getStats()._deliveredCount );
    SW_EXPECT_EQUAL( 0, rig._dispatcher.getInFlightCount() );
}

/** @brief [PushDispatcherTest] 잘못된 토큰이면 그 기기만 저장소에서 지워 다음 알림이 보내지 않는다 */
SW_TEST_CASE( PushDispatcherTest, InvalidTokenRemovesTheDevice )
{
    DispatcherFixture rig;
    rig.addDevice( "dead" );
    rig.addDevice( "alive" );
    rig._provider.scriptResult( "dead", PushDeliveryStatus::InvalidToken );
    rig.notify();
    SW_EXPECT_EQUAL( uint64( 1 ), rig._dispatcher.getStats()._invalidTokenCount );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._provider.getSent().size() );

    rig.notify();
    SW_REQUIRE_EQUAL( size_t( 3 ), rig._provider.getSent().size() );
    SW_EXPECT_EQUAL( string( "alive" ), rig._provider.getSent()[2]._deviceToken );
}

/** @brief [PushDispatcherTest] 일시 실패는 1, 2, 4, 8 초 백오프로 다시 보내고 다섯 번 지면 버린다. 기기는 지우지 않는다 */
SW_TEST_CASE( PushDispatcherTest, TransientRetriesWithBackoffThenDrops )
{
    DispatcherFixture rig;
    rig.addDevice( "a" );
    rig._provider.scriptResult( "a", PushDeliveryStatus::Transient, 100 );
    rig.notify();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._provider.getSent().size() );

    size_t expectedSent    = 1;
    int64  timeSinceLastMs = 0;
    for ( int64 backoffMs : { 1000ll, 2000ll, 4000ll, 8000ll } )
    {
        rig._nowMs += backoffMs - 1;
        rig.pump();
        SW_EXPECT_EQUAL( expectedSent, rig._provider.getSent().size() ); // 백오프 전이다
        rig._nowMs += 1;
        rig.pump();
        ++expectedSent;
        SW_EXPECT_EQUAL( expectedSent, rig._provider.getSent().size() );
        timeSinceLastMs += backoffMs;
    }
    SW_EXPECT_EQUAL( int64( 15000 ), timeSinceLastMs );
    rig.pump();
    SW_EXPECT_EQUAL( uint64( 1 ), rig._dispatcher.getStats()._droppedCount );
    SW_EXPECT_EQUAL( 0, rig._dispatcher.getInFlightCount() );

    rig._provider.scriptResult( "a", PushDeliveryStatus::Delivered, 0 );
    rig.notify();
    SW_EXPECT_EQUAL( expectedSent + 1, rig._provider.getSent().size() ); // 기기는 남아 있다
}

/** @brief [PushDispatcherTest] 제공자가 늦추라고 한 시간이 백오프보다 길면 그 시간을 따르고, 그 뒤 성공하면 끝난다 */
SW_TEST_CASE( PushDispatcherTest, RateLimitedHonorsTheProviderDelay )
{
    DispatcherFixture rig;
    rig.addDevice( "a" );
    rig._provider.scriptResult( "a", PushDeliveryStatus::RateLimited, 1, 60000 );
    rig.notify();
    rig._nowMs += 59999;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._provider.getSent().size() );
    rig._nowMs += 1;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 2 ), rig._provider.getSent().size() );
    SW_EXPECT_EQUAL( uint64( 1 ), rig._dispatcher.getStats()._deliveredCount );
    SW_EXPECT_EQUAL( 0, rig._dispatcher.getInFlightCount() );
}

/** @brief [PushDispatcherTest] 거절은 경고를 남기고 다시 보내지 않으며 기기도 지우지 않는다 */
SW_TEST_CASE( PushDispatcherTest, RejectedIsDroppedOnce )
{
    SW_EXPECT_LOG( "PushNotificationDispatcher.PushDispatcherProviderRejectedNotification" );
    DispatcherFixture rig;
    rig.addDevice( "a" );
    rig._provider.scriptResult( "a", PushDeliveryStatus::Rejected );
    rig.notify();
    rig._nowMs += 100000;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._provider.getSent().size() );
    SW_EXPECT_EQUAL( uint64( 1 ), rig._dispatcher.getStats()._droppedCount );
    rig.notify();
    SW_EXPECT_EQUAL( size_t( 2 ), rig._provider.getSent().size() );
}

/** @brief [PushDispatcherTest] 계정마다 한꺼번에 보낼 수 있는 상한이 있고 넘으면 보내지 않고 센다 */
SW_TEST_CASE( PushDispatcherTest, AccountBurstIsLimited )
{
    PushDispatcherConfig settings;
    settings._accountBurst = 2;
    DispatcherFixture rig( settings );
    rig.addDevice( "a" );
    PushNotificationMessage message;
    SW_EXPECT_TRUE( rig._dispatcher.notifyAccount( kAccount, message, UnixMilliseconds{ rig._nowMs } ) );
    SW_EXPECT_TRUE( rig._dispatcher.notifyAccount( kAccount, message, UnixMilliseconds{ rig._nowMs } ) );
    SW_EXPECT_FALSE( rig._dispatcher.notifyAccount( kAccount, message, UnixMilliseconds{ rig._nowMs } ) );
    SW_EXPECT_TRUE( rig._dispatcher.notifyAccount( kAccount + 1, message, UnixMilliseconds{ rig._nowMs } ) ); // 다른 계정은 따로 센다
    rig.pump();
    SW_EXPECT_EQUAL( uint64( 1 ), rig._dispatcher.getStats()._rateLimitedCount );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._provider.getSent().size() );
}

/** @brief [PushDispatcherTest] 기기 한도를 넘으면 가장 오래 등록된 기기를 밀어내고, 같은 토큰을 다시 등록하면 덮어쓴다 */
SW_TEST_CASE( PushDispatcherTest, DeviceLimitEvictsTheOldest )
{
    DispatcherFixture rig;
    for ( int32 index = 0; index < PushLimit::kMaxDevicePerAccount; ++index )
    {
        rig.addDevice( "t" + to_string( index ), kNowMs + index );
    }
    rig.addDevice( "t5", kNowMs + 5 ); // 덮어쓰기 — 밀어내지 않는다
    rig.addDevice( "new", kNowMs + 100 );
    rig.notify();
    const vector<FakePushProvider::SentRecord>& listSent = rig._provider.getSent();
    SW_EXPECT_EQUAL( static_cast<size_t>( PushLimit::kMaxDevicePerAccount ), listSent.size() );
    for ( const FakePushProvider::SentRecord& sent : listSent )
    {
        SW_EXPECT_NOT_EQUAL( string( "t0" ), sent._deviceToken );
    }
}

/** @brief [PushDispatcherTest] 등록 · 해지의 완료: 형식이 틀리면 Invalid, 없는 기기 해지는 NotFound, 맞으면 Ok 이고 해지한 기기에는 보내지 않는다. 태그 0 은 완료가 없다 */
SW_TEST_CASE( PushDispatcherTest, RegistrationCompletions )
{
    DispatcherFixture      rig;
    PushDeviceRegistration registration;
    registration._providerID = "fake";
    registration._token      = "a";
    rig._dispatcher.registerDevice( kAccount, registration, 1 );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeCompletion() == LiveOpsResult::Ok );

    PushDeviceRegistration noToken = registration;
    noToken._token.clear();
    rig._dispatcher.registerDevice( kAccount, noToken, 2 );
    SW_EXPECT_TRUE( rig.takeCompletion().hasError( OnlineError::kInvalidRequest ) );
    PushDeviceRegistration badProvider = registration;
    badProvider._providerID            = "Bad Name";
    rig._dispatcher.registerDevice( kAccount, badProvider, 3 );
    SW_EXPECT_TRUE( rig.takeCompletion().hasError( OnlineError::kInvalidRequest ) );
    rig._dispatcher.registerDevice( kInvalidAccountID, registration, 4 );
    SW_EXPECT_TRUE( rig.takeCompletion().hasError( OnlineError::kInvalidRequest ) );

    rig._dispatcher.unregisterDevice( kAccount, "fake", "none", 5 );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeCompletion() == LiveOpsResult::NotFound );
    rig._dispatcher.unregisterDevice( kAccount, "fake", "a", 6 );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeCompletion() == LiveOpsResult::Ok );

    rig.notify();
    SW_EXPECT_TRUE( rig._provider.getSent().empty() );
}

/** @brief [PushDispatcherTest] 저장소를 읽지 못한 알림은 버린다(배달은 최대 한 번) — 멈추거나 빈 목록으로 기기를 지우지 않는다 */
SW_TEST_CASE( PushDispatcherTest, StoreFailureDropsTheNotification )
{
    DispatcherFixture rig;
    rig.addDevice( "a" );
    rig._database.armFault( ServiceStoreFault::RejectRead );
    rig.notify();
    SW_EXPECT_TRUE( rig._provider.getSent().empty() );
    SW_EXPECT_EQUAL( 0, rig._dispatcher.getPendingJobCount() );
    rig.notify();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._provider.getSent().size() );
}

/** @brief [PushDispatcherTest] 토픽: 같은 표로는 한 번만 보내고, 잘못된 표 키는 보내지 않고 센다. 토픽의 일시 실패도 다시 보내며 기기를 건드리지 않는다 */
SW_TEST_CASE( PushDispatcherTest, TopicClaimSendsOnce )
{
    DispatcherFixture             rig;
    const PushNotificationMessage message;
    rig._dispatcher.notifyTopicOnce( "event.1", "news", message, UnixMilliseconds{ rig._nowMs } );
    rig._dispatcher.notifyTopicOnce( "event.1", "news", message, UnixMilliseconds{ rig._nowMs } );
    rig._dispatcher.notifyTopicOnce( "BAD KEY", "news", message, UnixMilliseconds{ rig._nowMs } );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._provider.getSent().size() );
    SW_EXPECT_EQUAL( string( "news" ), rig._provider.getSent()[0]._topic );
    SW_EXPECT_EQUAL( uint64( 1 ), rig._dispatcher.getStats()._topicClaimedCount );
    SW_EXPECT_EQUAL( uint64( 2 ), rig._dispatcher.getStats()._topicSkippedCount );

    rig._provider.scriptResult( "again", PushDeliveryStatus::Transient );
    SW_EXPECT_EQUAL( 1, rig._dispatcher.notifyTopic( "again", message, UnixMilliseconds{ rig._nowMs } ) );
    rig.pump();
    rig._nowMs += 1000;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 3 ), rig._provider.getSent().size() );
    SW_EXPECT_EQUAL( 0, rig._dispatcher.getInFlightCount() );
}
