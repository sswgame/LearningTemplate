#include "pch.h"

#include "GameFramework/Base/Online/Bus/EphemeralServerBus.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"

#include "TestFramework/TestFramework.h"

// 서버 간 버스 — 프로세스 안 허브 위 버스 둘과 캐시 발행/구독 위 버스 둘이 같은 약속을 지킨다:
// 구독한 쪽만 받고, 자기 메시지도 오며(보낸 서버 id · 순번), 해지 뒤에는 오지 않는다.

using namespace sw;

namespace
{
    struct TestServerBusInternal
    {
        static void publishText( IServerBus& bus, string_view topic, string_view text )
        {
            bus.publish( topic, reinterpret_cast<const uint8*>( text.data() ), static_cast<int32>( text.size() ) );
        }

        static bool isText( const ServerBusMessage& message, string_view text )
        {
            return message._bytes.size() == text.size() && std::equal( message._bytes.begin(), message._bytes.end(), reinterpret_cast<const uint8*>( text.data() ) );
        }

        /** @brief 버스 둘(서버 1 · 2)로 계약을 돈다. */
        static void runContract( IServerBus& serverA, IServerBus& serverB )
        {
            serverA.subscribe( "account.revoke" );
            serverB.subscribe( "account.revoke" );
            serverB.subscribe( "config.changed" );
            publishText( serverA, "account.revoke", "acct-7" );
            publishText( serverA, "config.changed", "feature.trade" );
            publishText( serverA, "chat.channel.world", "nobody listens" );

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
            listMessageB.clear();
            SW_EXPECT_EQUAL( 0, serverB.pollMessages( listMessageB ) );
            publishText( serverB, "Bad Topic", "dropped" ); // 틀린 주제는 버린다(경고)
            listMessageA.clear();
            SW_EXPECT_EQUAL( 1, serverA.pollMessages( listMessageA ) );
        }
    };
} // namespace

SW_TEST_CASE( ServerBusTest, LocalBusesDeliverOnlyToSubscribers )
{
    LocalServerBusHub hub;
    LocalServerBus    serverA{ &hub, 1 };
    LocalServerBus    serverB{ &hub, 2 };
    TestServerBusInternal::runContract( serverA, serverB );
}

SW_TEST_CASE( ServerBusTest, EphemeralBusesDeliverOnlyToSubscribers )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    cacheA{ &database };
    MemoryEphemeralStore    cacheB{ &database };
    EphemeralServerBus      serverA{ &cacheA, 1 };
    EphemeralServerBus      serverB{ &cacheB, 2 };
    TestServerBusInternal::runContract( serverA, serverB );
    SW_EXPECT_EQUAL( uint64( 0 ), serverA.getDroppedCount() );
    SW_EXPECT_EQUAL( 0, cacheA.getPendingCount() ); // 발행 답은 버스가 거둬 버린다
}
