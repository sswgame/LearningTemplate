#include "pch.h"

#include "GameFramework/Base/Online/Bus/EphemeralServerBus.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"

#include "TestFramework/TestFramework.h"

// 서버 간 버스 — 프로세스 안 허브 위 버스 둘과 캐시 발행/구독 위 버스 둘이 같은 약속을 지킨다:
// 구독한 쪽만 받고, 자기 메시지도 오며(보낸 서버 id · 순번), 해지 뒤에는 오지 않는다.
// 캐시 위 버스는 호스트의 라우터로 캐시 앞을 다른 서비스와 나눠 쓴다.

using namespace sw;

namespace
{
    struct TestServerBusInternal
    {
        /** @brief 캐시 위 버스는 라우터의 pump 가 메시지를 버스 큐에 넣는다(호스트 tick 과 같은 순서). 프로세스 안 버스는 라우터가 없다. */
        struct Routers
        {
            EphemeralStoreRouter* _pRouterA{ nullptr };
            EphemeralStoreRouter* _pRouterB{ nullptr };

            void pump() const
            {
                if ( _pRouterA != nullptr )
                    (void)_pRouterA->pump();
                if ( _pRouterB != nullptr )
                    (void)_pRouterB->pump();
            }
        };

        static void publishText( IServerBus& bus, string_view topic, string_view text )
        {
            bus.publish( topic, reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
        }

        static bool isText( const ServerBusMessage& message, string_view text )
        {
            return message._bytes.size() == text.size() && std::equal( message._bytes.begin(), message._bytes.end(), reinterpret_cast<const uint8*>( text.data() ) );
        }

        /** @brief 버스 둘(서버 1 · 2)로 계약을 돈다. */
        static void runContract( IServerBus& serverA, IServerBus& serverB, const Routers& routers )
        {
            serverA.subscribe( "account.revoke" );
            serverB.subscribe( "account.revoke" );
            serverB.subscribe( "config.changed" );
            publishText( serverA, "account.revoke", "acct-7" );
            publishText( serverA, "config.changed", "feature.trade" );
            publishText( serverA, "chat.channel.world", "nobody listens" );

            routers.pump();
            vector<ServerBusMessage> listMessageA;
            vector<ServerBusMessage> listMessageB;
            SW_EXPECT_EQUAL( 1, serverA.pollMessages( listMessageA ) );
            SW_EXPECT_EQUAL( 2, serverB.pollMessages( listMessageB ) );
            SW_ASSERT_EQUAL( size_t( 1 ), listMessageA.size() );
            SW_EXPECT_EQUAL( uint64( 1 ), listMessageA[0]._originServerID ); // 자기 메시지도 온다 — 보낸 서버 id 로 거른다
            SW_EXPECT_TRUE( isText( listMessageA[0], "acct-7" ) );
            SW_ASSERT_EQUAL( size_t( 2 ), listMessageB.size() );
            SW_EXPECT_TRUE( listMessageB[0]._topic == "account.revoke" );
            SW_EXPECT_TRUE( listMessageB[1]._topic == "config.changed" && isText( listMessageB[1], "feature.trade" ) );
            SW_EXPECT_EQUAL( uint64( 1 ), listMessageB[0]._sequence );
            SW_EXPECT_EQUAL( uint64( 2 ), listMessageB[1]._sequence );

            serverB.unsubscribe( "account.revoke" );
            publishText( serverA, "account.revoke", "acct-8" );
            routers.pump();
            listMessageB.clear();
            SW_EXPECT_EQUAL( 0, serverB.pollMessages( listMessageB ) );
            publishText( serverB, "Bad Topic", "dropped" ); // 틀린 주제는 버린다(경고)
            routers.pump();
            listMessageA.clear();
            SW_EXPECT_EQUAL( 1, serverA.pollMessages( listMessageA ) );
        }

        /** @brief 같은 캐시 앞을 나눠 쓰는 다른 서비스의 답입니다. 버스가 앞을 혼자 쓰던 때에는 버스가 이 답을 꺼내 버렸다. */
        struct ReplyRecorder
        {
            int32 _replyCount{ 0 };

            void onReply( const EphemeralReply& reply )
            {
                (void)reply;
                ++_replyCount;
            }
        };
    };
} // namespace

SW_TEST_CASE( ServerBusTest, LocalBusesDeliverOnlyToSubscribers )
{
    LocalServerBusHub hub;
    LocalServerBus    serverA{ &hub, 1 };
    LocalServerBus    serverB{ &hub, 2 };
    TestServerBusInternal::runContract( serverA, serverB, TestServerBusInternal::Routers{} );
}

SW_TEST_CASE( ServerBusTest, EphemeralBusesDeliverOnlyToSubscribers )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    cacheA{ &database };
    MemoryEphemeralStore    cacheB{ &database };
    EphemeralStoreRouter    routerA;
    EphemeralStoreRouter    routerB;
    routerA.initialize( &cacheA );
    routerB.initialize( &cacheB );
    {
        EphemeralServerBus serverA{ &routerA, 1 };
        EphemeralServerBus serverB{ &routerB, 2 };
        TestServerBusInternal::runContract( serverA, serverB, TestServerBusInternal::Routers{ &routerA, &routerB } );
        SW_EXPECT_EQUAL( uint64( 0 ), serverA.getDroppedCount() );
        SW_EXPECT_EQUAL( 0, routerA.getPendingCount() ); // 발행 답은 라우터가 꺼내 버린다
    }
    routerA.shutdown();
    routerB.shutdown();
}

SW_TEST_CASE( ServerBusTest, EphemeralBusSharesTheCacheFrontWithOtherServices )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    cache{ &database };
    EphemeralStoreRouter    router;
    router.initialize( &cache );
    TestServerBusInternal::ReplyRecorder recorder;
    {
        EphemeralServerBus bus{ &router, 1 };
        bus.subscribe( "account.revoke" );
        // 같은 앞으로 다른 서비스가 캐시 요청을 맡긴다. 버스가 앞을 혼자 쓰면 이 답을 버스가 가져간다.
        (void)router.submit( EphemeralRequest::makeGet( "presence:7" ),
                             SW_DELEGATE_METHOD( EphemeralStoreRouter::ReplyDelegate, &TestServerBusInternal::ReplyRecorder::onReply, &recorder ) );
        TestServerBusInternal::publishText( bus, "account.revoke", "acct-7" );
        (void)router.pump();
        vector<ServerBusMessage> listMessage;
        SW_EXPECT_EQUAL( 1, bus.pollMessages( listMessage ) );
        SW_EXPECT_EQUAL( 1, recorder._replyCount );
        SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    }
    router.shutdown();
}
