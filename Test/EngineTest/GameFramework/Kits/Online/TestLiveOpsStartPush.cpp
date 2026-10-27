// 라이브 이벤트 시작 푸시(OnlineKits G5) — 방금 열린 창만 한 번 알린다(옛 창 · 푸시를 달지 않은 이벤트 · 이미 알린 창은 아니다, 반복 이벤트는 회차마다),
// 지역마다 토픽 하나(`liveops.<지역>`, 제한이 없으면 `liveops.all`), 토픽 발송은 토픽을 아는 제공자에게만 가고 같은 표(키)는 서버 여럿 중 하나만 보낸다.
#include "pch.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsTypes.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr int64 kMonday20261005 = 1791158400000ll; ///< 2026-10-05 00:00 UTC
    constexpr int64 kMinute         = 60000ll;
    constexpr int64 kHour           = 60 * kMinute;
    constexpr int64 kDay            = 24 * kHour;

    LiveEventDefinition makePushEvent( const utf8* pEventID )
    {
        LiveEventDefinition definition;
        definition._eventID = pEventID;
        definition._kind    = "xp_boost";
        definition._startMs = kMonday20261005;
        definition._endMs   = kMonday20261005 + 7 * kDay;
        definition._listParameter.push_back( LiveEventParameter{ LiveOpsPush::kTitleParameter, "push.event.title" } );
        definition._listParameter.push_back( LiveEventParameter{ LiveOpsPush::kBodyParameter, "push.event.body" } );
        definition._listParameter.push_back( LiveEventParameter{ LiveOpsPush::kLinkParameter, "event/halloween" } );
        return definition;
    }

    /** @brief 서버 하나의 라이브 운영 로직 — 저장소 앞과 로직만(버스 없음). */
    struct StartNode
    {
        MemoryServiceStore _store;
        LiveOpsService     _service;

        explicit StartNode( MemoryServiceDatabase* pDatabase )
            : _store{ pDatabase }
            , _service{}
        {
            LiveOpsDependencies dependencies;
            dependencies._pStore = &_store;
            _service.initialize( dependencies );
        }

        ~StartNode()
        {
            _service.shutdown();
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        void step( UnixMilliseconds nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                _service.update( nowMs );
                (void)_store.pollCompletions();
            }
        }

        vector<LiveEventStart> takeStarts()
        {
            vector<LiveEventStart> listStart;
            _service.takeStartedEvents( listStart );
            return listStart;
        }
    };

    /** @brief 서버 하나의 푸시 발송기 — 저장소 앞과 가짜 제공자. */
    struct PushNode
    {
        MemoryServiceStore         _store;
        FakePushProvider           _provider;
        PushNotificationDispatcher _dispatcher;

        explicit PushNode( MemoryServiceDatabase* pDatabase )
            : _store{ pDatabase }
            , _provider{}
            , _dispatcher{}
        {
            _dispatcher.initialize( &_store, PushDispatcherConfig{} );
            SW_EXPECT_TRUE( _dispatcher.registerProvider( &_provider ) );
        }

        ~PushNode()
        {
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        void step( UnixMilliseconds nowMs )
        {
            for ( int32 round = 0; round < 3; ++round )
            {
                (void)_store.pollCompletions();
                _dispatcher.update( nowMs );
            }
        }
    };

    PushNotificationMessage makeMessage()
    {
        PushNotificationMessage message;
        message._titleKey = "push.event.title";
        message._bodyKey  = "push.event.body";
        message._deepLink = "event/halloween";
        return message;
    }
} // namespace

SW_TEST_CASE( LiveOpsStartPushTest, AFreshlyOpenedWindowIsAnnouncedOnceWithItsParameters )
{
    MemoryServiceDatabase  database;
    StartNode              node( &database );
    const UnixMilliseconds nowMs{ kMonday20261005 + kMinute };
    node.step( nowMs );
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 0 ) );

    LiveEventDefinition event = makePushEvent( "halloween" );
    event._listRegion.push_back( "kr" );
    event._listRegion.push_back( "jp" );
    node._service.putEvent( event, 7, "spooky week", nowMs, 1 );
    node.step( nowMs );
    const vector<LiveEventStart> listStart = node.takeStarts();
    SW_ASSERT_EQUAL( listStart.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listStart[0]._eventID, string( "halloween" ) );
    SW_EXPECT_EQUAL( listStart[0]._message._titleKey, string( "push.event.title" ) );
    SW_EXPECT_EQUAL( listStart[0]._message._bodyKey, string( "push.event.body" ) );
    SW_EXPECT_EQUAL( listStart[0]._message._deepLink, string( "event/halloween" ) );
    SW_EXPECT_EQUAL( listStart[0]._listRegion.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( listStart[0]._windowEndMs, event._endMs );

    node.step( nowMs + 1000 ); // 같은 창은 다시 알리지 않는다
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 0 ) );
}

SW_TEST_CASE( LiveOpsStartPushTest, OldWindowsAndEventsWithoutPushAreNotAnnounced )
{
    MemoryServiceDatabase  database;
    StartNode              node( &database );
    const UnixMilliseconds nowMs{ kMonday20261005 + kHour }; // 이벤트는 한 시간 전에 열렸다 — 서버가 방금 떠서 처음 본 것이다
    node._service.putEvent( makePushEvent( "old_event" ), 7, "", nowMs, 1 );
    LiveEventDefinition quiet = makePushEvent( "quiet_event" );
    quiet._listParameter.clear(); // 시작 푸시를 달지 않았다
    quiet._startMs = ( nowMs - kMinute ).count();
    node._service.putEvent( quiet, 7, "", nowMs, 2 );
    node.step( nowMs );
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 0 ) );
}

SW_TEST_CASE( LiveOpsStartPushTest, ARecurringEventIsAnnouncedAtEveryOccurrence )
{
    MemoryServiceDatabase database;
    StartNode             node( &database );
    LiveEventDefinition   evening = makePushEvent( "evening_drop" );
    evening._endMs                = kMonday20261005 + 3 * kDay;
    evening._recurrence           = LiveEventRecurrence::Daily;
    evening._activeMinuteOfDay    = 20 * 60;
    evening._activeDurationMs     = kHour;
    const int64 eveningMs         = kMonday20261005 + 20 * kHour;
    node._service.putEvent( evening, 0, "", UnixMilliseconds{ eveningMs - 2 * kMinute }, 1 );
    node.step( UnixMilliseconds{ eveningMs - kMinute } ); // 19:59 — 닫혀 있다
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 0 ) );
    node.step( UnixMilliseconds{ eveningMs } ); // 20:00 — 열림
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 1 ) );
    node.step( UnixMilliseconds{ eveningMs + kHour / 2 } );
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 0 ) );
    node.step( UnixMilliseconds{ eveningMs + kHour } ); // 닫힘
    node.step( UnixMilliseconds{ eveningMs + kDay } );  // 다음 날 20:00 — 다시 열림
    SW_EXPECT_EQUAL( node.takeStarts().size(), size_t( 1 ) );
}

SW_TEST_CASE( LiveOpsStartPushTest, TopicNamesFollowTheRegionConvention )
{
    SW_EXPECT_EQUAL( LiveOpsPush::makeRegionTopic( "kr" ), string( "liveops.kr" ) );
    SW_EXPECT_EQUAL( string( LiveOpsPush::kAllTopic ), string( "liveops.all" ) );
}

SW_TEST_CASE( PushDispatcherTopicTest, TopicGoesOnlyToProvidersThatKnowTopicsAndRetriesLikeADevice )
{
    MemoryServiceDatabase database;
    PushNode              node( &database );
    SW_EXPECT_EQUAL( node._dispatcher.notifyTopic( "liveops.kr", makeMessage(), UnixMilliseconds{ 0 } ), 1 );
    node.step( UnixMilliseconds{ 0 } );
    SW_ASSERT_EQUAL( node._provider.getSent().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._provider.getSent()[0]._topic, string( "liveops.kr" ) );
    SW_EXPECT_TRUE( node._provider.getSent()[0]._deviceToken.empty() );
    SW_EXPECT_EQUAL( node._dispatcher.getStats()._deliveredCount, uint64( 1 ) );

    node._provider.scriptResult( "liveops.jp", PushDeliveryStatus::Transient, 1 );
    SW_EXPECT_EQUAL( node._dispatcher.notifyTopic( "liveops.jp", makeMessage(), UnixMilliseconds{ 1000 } ), 1 );
    node.step( UnixMilliseconds{ 1000 } ); // 1 차 실패 → 1 초 뒤
    node.step( UnixMilliseconds{ 1999 } );
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 2 ) );
    node.step( UnixMilliseconds{ 2000 } ); // 다시 보내 성공
    SW_EXPECT_EQUAL( node._provider.getSent().size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( node._dispatcher.getInFlightCount(), 0 );

    // 토픽에는 기기가 없다 — 무효 토큰 결과도 기기 해지를 부르지 않고 버려진다.
    node._provider.scriptResult( "liveops.us", PushDeliveryStatus::InvalidToken, 1 );
    SW_EXPECT_EQUAL( node._dispatcher.notifyTopic( "liveops.us", makeMessage(), UnixMilliseconds{ 3000 } ), 1 );
    node.step( UnixMilliseconds{ 3000 } );
    SW_EXPECT_EQUAL( node._dispatcher.getInFlightCount(), 0 );
    SW_EXPECT_EQUAL( node._dispatcher.getPendingWorkCount(), 0 );
}

SW_TEST_CASE( PushDispatcherTopicTest, TheSameClaimKeySendsOnceAcrossServers )
{
    MemoryServiceDatabase database;
    PushNode              first( &database );
    PushNode              second( &database );
    first._dispatcher.notifyTopicOnce( "halloween/00000000/0001", "liveops.kr", makeMessage(), UnixMilliseconds{ 0 } );
    second._dispatcher.notifyTopicOnce( "halloween/00000000/0001", "liveops.kr", makeMessage(), UnixMilliseconds{ 0 } );
    first.step( UnixMilliseconds{ 0 } );
    second.step( UnixMilliseconds{ 0 } );
    SW_EXPECT_EQUAL( first._provider.getSent().size() + second._provider.getSent().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( first._dispatcher.getStats()._topicClaimedCount + second._dispatcher.getStats()._topicClaimedCount, uint64( 1 ) );
    SW_EXPECT_EQUAL( first._dispatcher.getStats()._topicSkippedCount + second._dispatcher.getStats()._topicSkippedCount, uint64( 1 ) );
    SW_EXPECT_EQUAL( database.countRecords( hashed_string( "liveops_push" ) ), 1 );

    first._dispatcher.notifyTopicOnce( "halloween/00000000/0002", "liveops.kr", makeMessage(), UnixMilliseconds{ 100 } ); // 다른 창(다른 키)은 다시 보낸다
    first.step( UnixMilliseconds{ 100 } );
    SW_EXPECT_EQUAL( first._provider.getSent().size() + second._provider.getSent().size(), size_t( 2 ) );

    first._dispatcher.notifyTopicOnce( "Bad Key", "liveops.kr", makeMessage(), UnixMilliseconds{ 200 } ); // 키 규칙 밖은 보내지 않고 센다
    first.step( UnixMilliseconds{ 200 } );
    SW_EXPECT_EQUAL( first._provider.getSent().size() + second._provider.getSent().size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( first._dispatcher.getPendingWorkCount(), 0 );
}
