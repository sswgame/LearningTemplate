#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/Bus/EphemeralServerBus.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"

// ServerBusTest — 서버 간 버스: 주제 규칙, 프로세스 안 허브(LocalServerBus)의 구독 · 배달 · 순번, 주제 정의(ServerBusTopic · ServerBusTopicFamily)의
// 쓰기 · 읽기와 거절(다른 주제 · 의미 버전 · 깨진 바이트), 캐시 발행/구독 위 버스(EphemeralServerBus)의 봉투와 라우터 묶기.
// 본문 타입은 실제 버스 본문인 `ServiceSanctionChanged` 를 쓴다(nogpu).

namespace
{
    constexpr uint64 kServerA = 11;
    constexpr uint64 kServerB = 22;

    constexpr sw::ServerBusTopic<sw::ServiceSanctionChanged>       kTestTopic{ "test.bus.sanction" };
    constexpr sw::ServerBusTopic<sw::ServiceSanctionChanged>       kOtherTopic{ "test.bus.other" };
    constexpr sw::ServerBusTopicFamily<sw::ServiceSanctionChanged> kTestFamily{ "test.family." };

    sw::ServiceSanctionChanged makeChanged( uint64 accountID )
    {
        sw::ServiceSanctionChanged changed;
        changed._accountID = accountID;
        return changed;
    }
} // namespace

/**
 * @brief [ServerBusTest] 주제는 `[0-9a-z_.]` 1..200 B 이다 — 빈 글 · 대문자 · 공백 · 콜론 · 201 B 는 아니다
 */
SW_TEST_CASE( ServerBusTest, TopicRuleAllowsOnlyLowerDigitUnderscoreDot )
{
    SW_EXPECT_TRUE( sw::IServerBusTransport::isValidTopic( "account.revoke" ) );
    SW_EXPECT_TRUE( sw::IServerBusTransport::isValidTopic( "chat.channel.room_7" ) );
    SW_EXPECT_TRUE( sw::IServerBusTransport::isValidTopic( sw::string( 200, 'a' ) ) );
    SW_EXPECT_FALSE( sw::IServerBusTransport::isValidTopic( sw::string( 201, 'a' ) ) );
    SW_EXPECT_FALSE( sw::IServerBusTransport::isValidTopic( "" ) );
    SW_EXPECT_FALSE( sw::IServerBusTransport::isValidTopic( "Account.revoke" ) );
    SW_EXPECT_FALSE( sw::IServerBusTransport::isValidTopic( "chat channel" ) );
    SW_EXPECT_FALSE( sw::IServerBusTransport::isValidTopic( "bus:chat" ) );
    SW_EXPECT_FALSE( sw::IServerBusTransport::isValidTopic( "chat-channel" ) );
}

/**
 * @brief [ServerBusTest] 허브 위 버스 둘: 구독한 버스만 받고, 보낸 버스 자신도 받으며, 보낸 서버 id 와 1 부터 세는 순번이 실린다
 */
SW_TEST_CASE( ServerBusTest, LocalHubDeliversToSubscribersWithOriginAndSequence )
{
    sw::LocalServerBusHub hub;
    sw::LocalServerBus    busA( &hub, kServerA );
    sw::LocalServerBus    busB( &hub, kServerB );
    busA.subscribe( "test.local" );
    busB.subscribe( "test.local" );
    busB.subscribe( "test.local" ); // 두 번 구독해도 한 번만 받는다

    const uint8 arrPayload[] = { 1, 2, 3 };
    busA.publish( "test.local", arrPayload, 3 );
    busA.publish( "test.unsubscribed", arrPayload, 3 ); // 아무도 구독하지 않은 주제
    busA.publish( "test.local", nullptr, 0 );           // 빈 본문도 된다

    sw::vector<sw::ServerBusMessage> listMessage;
    SW_REQUIRE_EQUAL( 2, busB.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( sw::string( "test.local" ), listMessage[0].getTopic() );
    SW_EXPECT_EQUAL( kServerA, listMessage[0].getOriginServerID() );
    SW_EXPECT_EQUAL( uint64( 1 ), listMessage[0].getSequence() );
    SW_EXPECT_EQUAL( 3, listMessage[0].getByteCount() );
    SW_EXPECT_EQUAL( uint64( 3 ), listMessage[1].getSequence() ); // 받는 이 없는 발행도 순번을 쓴다 — 빠짐 진단은 보낸 쪽 기준이다
    SW_EXPECT_EQUAL( 0, listMessage[1].getByteCount() );

    sw::vector<sw::ServerBusMessage> listSelf;
    SW_EXPECT_EQUAL( 2, busA.pollMessages( listSelf ) ); // 자기가 보낸 것도 온다(`getOriginServerID` 로 거른다)

    sw::vector<sw::ServerBusMessage> listAgain;
    SW_EXPECT_EQUAL( 0, busB.pollMessages( listAgain ) ); // 꺼낸 것은 다시 오지 않는다
}

/**
 * @brief [ServerBusTest] 구독을 풀거나 버스가 사라지면 더 받지 않는다 — 허브의 배달 수가 줄어든다
 */
SW_TEST_CASE( ServerBusTest, LocalHubStopsDeliveringAfterUnsubscribeOrDestroy )
{
    sw::LocalServerBusHub hub;
    sw::LocalServerBus    busA( &hub, kServerA );
    {
        sw::LocalServerBus busB( &hub, kServerB );
        busA.subscribe( "test.local" );
        busB.subscribe( "test.local" );
        const sw::ServerBusMessage message( {}, "test.local", kServerA, 1 );
        SW_EXPECT_EQUAL( 2, hub.deliver( message ) );

        busA.unsubscribe( "test.local" );
        SW_EXPECT_EQUAL( 1, hub.deliver( message ) );
        busA.unsubscribe( "test.never" ); // 구독하지 않은 주제를 풀어도 아무 일 없다
    }
    const sw::ServerBusMessage message( {}, "test.local", kServerA, 2 );
    SW_EXPECT_EQUAL( 0, hub.deliver( message ) ); // busB 가 사라지며 받은편지함도 지워졌다

    sw::vector<sw::ServerBusMessage> listMessage;
    SW_EXPECT_EQUAL( 0, busA.pollMessages( listMessage ) );
}

/**
 * @brief [ServerBusTest] 틀린 주제 · 크기의 발행과 틀린 주제의 구독은 경고를 남기고 버린다(순번도 쓰지 않는다)
 */
SW_TEST_CASE( ServerBusTest, LocalBusDropsInvalidPublishAndSubscription )
{
    SW_EXPECT_LOG( "ServerBus.InvalidMessage" );
    SW_EXPECT_LOG( "ServerBus.InvalidTopic" );
    sw::LocalServerBusHub hub;
    sw::LocalServerBus    bus( &hub, kServerA );
    bus.subscribe( "Bad Topic" );
    bus.subscribe( "test.ok" );

    const uint8 arrPayload[] = { 7 };
    bus.publish( "Bad Topic", arrPayload, 1 );
    bus.publish( "test.ok", arrPayload, -1 );
    bus.publish( "test.ok", nullptr, 1 ); // 크기가 있는데 바이트가 없다
    sw::vector<uint8> listOversize( static_cast<size_t>( sw::IServerBusTransport::kMaxMessageSize ) + 1, 0 );
    bus.publish( "test.ok", listOversize.data(), static_cast<int32>( listOversize.size() ) );

    sw::vector<sw::ServerBusMessage> listMessage;
    SW_EXPECT_EQUAL( 0, bus.pollMessages( listMessage ) );

    bus.publish( "test.ok", arrPayload, 1 );
    SW_REQUIRE_EQUAL( 1, bus.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( uint64( 1 ), listMessage[0].getSequence() ); // 버린 발행은 순번을 쓰지 않았다
}

/**
 * @brief [ServerBusTest] 주제 정의로 쓰고 읽으면 값이 그대로 오고, 다른 주제의 메시지는 로그 없이 OtherTopic 이며 값을 바꾸지 않는다
 */
SW_TEST_CASE( ServerBusTest, TopicPublishReadRoundTripsAndIgnoresOtherTopic )
{
    sw::LocalServerBusHub hub;
    sw::LocalServerBus    transport( &hub, kServerA );
    sw::ServerBus         bus( transport );
    SW_EXPECT_EQUAL( kServerA, bus.getServerID() );
    transport.subscribe( kTestTopic.getName() );
    transport.subscribe( kOtherTopic.getName() );

    SW_REQUIRE_TRUE( kTestTopic.publish( bus, makeChanged( 4242 ) ) );
    SW_REQUIRE_TRUE( kOtherTopic.publish( bus, makeChanged( 7 ) ) );
    sw::vector<sw::ServerBusMessage> listMessage;
    SW_REQUIRE_EQUAL( 2, transport.pollMessages( listMessage ) );

    SW_EXPECT_TRUE( kTestTopic.matches( listMessage[0] ) );
    sw::ServiceSanctionChanged read = makeChanged( 0 );
    SW_REQUIRE_TRUE( kTestTopic.read( listMessage[0], read ) == sw::ServerBusReadResult::Read );
    SW_EXPECT_EQUAL( uint64( 4242 ), read._accountID );

    const uint64 rejectedBefore = sw::ServerBusReadLog::getRejectedCount();
    SW_EXPECT_FALSE( kTestTopic.matches( listMessage[1] ) );
    SW_EXPECT_TRUE( kTestTopic.read( listMessage[1], read ) == sw::ServerBusReadResult::OtherTopic );
    SW_EXPECT_EQUAL( uint64( 4242 ), read._accountID );
    SW_EXPECT_EQUAL( rejectedBefore, sw::ServerBusReadLog::getRejectedCount() ); // 다른 주제는 거절이 아니다
}

/**
 * @brief [ServerBusTest] 의미 버전이 다르거나 바이트가 깨진 메시지는 거절하고(값 그대로) 거절 수를 올리며 오류를 남긴다
 */
SW_TEST_CASE( ServerBusTest, TopicRejectsVersionMismatchAndBrokenBytes )
{
    SW_EXPECT_LOG( "ServerBus.MessageRejected" );
    sw::vector<uint8> listNewerBytes;
    SW_REQUIRE_TRUE( sw::MixedBuildCodec::encode( sw::ServiceSanctionChanged::kBusVersion + 1, makeChanged( 99 ), listNewerBytes ) );
    const sw::ServerBusMessage newer( listNewerBytes, kTestTopic.getName(), kServerB, 1 );
    const sw::ServerBusMessage broken( { 1, 0 }, kTestTopic.getName(), kServerB, 2 ); // 버전 머리(4 B)도 다 없다

    const uint64               rejectedBefore = sw::ServerBusReadLog::getRejectedCount();
    sw::ServiceSanctionChanged read           = makeChanged( 5 );
    SW_EXPECT_TRUE( kTestTopic.read( newer, read ) == sw::ServerBusReadResult::VersionMismatch );
    SW_EXPECT_TRUE( kTestTopic.read( newer, read ) == sw::ServerBusReadResult::VersionMismatch ); // 로그는 한 번, 셈은 매번
    SW_EXPECT_TRUE( kTestTopic.read( broken, read ) == sw::ServerBusReadResult::Unreadable );
    SW_EXPECT_EQUAL( uint64( 5 ), read._accountID );
    SW_EXPECT_EQUAL( rejectedBefore + 3, sw::ServerBusReadLog::getRejectedCount() );

    // 같은 바이트를 맞는 버전으로 쓰면 읽힌다 — 거절은 버전 머리 때문이다
    sw::vector<uint8> listCurrentBytes;
    SW_REQUIRE_TRUE( sw::MixedBuildCodec::encode( sw::ServiceSanctionChanged::kBusVersion, makeChanged( 99 ), listCurrentBytes ) );
    const sw::ServerBusMessage current( listCurrentBytes, kTestTopic.getName(), kServerB, 3 );
    SW_REQUIRE_TRUE( kTestTopic.read( current, read ) == sw::ServerBusReadResult::Read );
    SW_EXPECT_EQUAL( uint64( 99 ), read._accountID );
}

/**
 * @brief [ServerBusTest] 주제 가족: 접두사 + 빈 아닌 접미사만 제 주제이고, 접미사를 돌려주며, 가족으로 쓴 것을 가족으로 읽는다
 */
SW_TEST_CASE( ServerBusTest, TopicFamilyMatchesPrefixWithNonEmptySuffix )
{
    SW_EXPECT_EQUAL( sw::string( "test.family.room1" ), kTestFamily.makeTopic( "room1" ) );
    SW_EXPECT_TRUE( kTestFamily.matchesTopic( "test.family.room1" ) );
    SW_EXPECT_FALSE( kTestFamily.matchesTopic( "test.family." ) ); // 접미사가 비었다
    SW_EXPECT_FALSE( kTestFamily.matchesTopic( "test.familyx" ) );
    SW_EXPECT_FALSE( kTestFamily.matchesTopic( "test.bus.sanction" ) );

    sw::LocalServerBusHub hub;
    sw::LocalServerBus    transport( &hub, kServerA );
    sw::ServerBus         bus( transport );
    transport.subscribe( kTestFamily.makeTopic( "room1" ) );
    transport.subscribe( kTestTopic.getName() );
    SW_REQUIRE_TRUE( kTestFamily.publish( bus, "room1", makeChanged( 31 ) ) );
    SW_REQUIRE_TRUE( kTestTopic.publish( bus, makeChanged( 32 ) ) );

    sw::vector<sw::ServerBusMessage> listMessage;
    SW_REQUIRE_EQUAL( 2, transport.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( sw::string_view( "room1" ), kTestFamily.findSuffix( listMessage[0] ) );
    SW_EXPECT_TRUE( kTestFamily.findSuffix( listMessage[1] ).empty() );

    sw::ServiceSanctionChanged read = makeChanged( 0 );
    SW_REQUIRE_TRUE( kTestFamily.read( listMessage[0], read ) == sw::ServerBusReadResult::Read );
    SW_EXPECT_EQUAL( uint64( 31 ), read._accountID );
    SW_EXPECT_TRUE( kTestFamily.read( listMessage[1], read ) == sw::ServerBusReadResult::OtherTopic );
    SW_EXPECT_EQUAL( uint64( 31 ), read._accountID );
}

/**
 * @brief [ServerBusTest] 캐시 위 버스 둘(앞 둘이 데이터 하나): 봉투를 벗겨 보낸 서버 id · 순번 · 본문을 되살리고, 봉투가 아닌 채널 메시지는 버린 수로만 센다
 */
SW_TEST_CASE( ServerBusTest, EphemeralBusCarriesEnvelopeAcrossServers )
{
    sw::ManualClock             clock( sw::MonotonicMilliseconds{ 1000 } );
    sw::MemoryEphemeralDatabase database( clock );
    sw::MemoryEphemeralStore    storeA( &database );
    sw::MemoryEphemeralStore    storeB( &database );
    sw::EphemeralStoreRouter    routerA;
    sw::EphemeralStoreRouter    routerB;
    routerA.initialize( &storeA );
    routerB.initialize( &storeB );
    sw::EphemeralServerBus busA( &routerA, kServerA );
    sw::EphemeralServerBus busB( &routerB, kServerB );
    busB.subscribe( "test.cache" );

    sw::ServerBus bus( busA );
    SW_REQUIRE_TRUE( sw::ServerBusTopic<sw::ServiceSanctionChanged>( "test.cache" ).publish( bus, makeChanged( 77 ) ) );
    (void)routerA.pump(); // 발행 답(받은 수)은 주인 없이 버린다
    SW_EXPECT_EQUAL( 0, routerA.getPendingCount() );
    (void)routerB.pump();

    sw::vector<sw::ServerBusMessage> listMessage;
    SW_REQUIRE_EQUAL( 1, busB.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( sw::string( "test.cache" ), listMessage[0].getTopic() ); // 채널 접두 `bus:` 는 벗긴다
    SW_EXPECT_EQUAL( kServerA, listMessage[0].getOriginServerID() );
    SW_EXPECT_EQUAL( uint64( 1 ), listMessage[0].getSequence() );
    sw::ServiceSanctionChanged read = makeChanged( 0 );
    SW_REQUIRE_TRUE( sw::ServerBusTopic<sw::ServiceSanctionChanged>( "test.cache" ).read( listMessage[0], read ) == sw::ServerBusReadResult::Read );
    SW_EXPECT_EQUAL( uint64( 77 ), read._accountID );

    // 같은 채널에 봉투가 아닌 바이트(짧다, 봉투 판이 다르다)가 오면 버린다
    (void)storeA.submit( sw::EphemeralRequest::makePublish( "bus:test.cache", { 1, 2, 3 } ) );
    sw::vector<uint8> listWrongVersion( 17, 0 );
    listWrongVersion[0] = 2;
    (void)storeA.submit( sw::EphemeralRequest::makePublish( "bus:test.cache", listWrongVersion ) );
    (void)routerB.pump();
    SW_EXPECT_EQUAL( uint64( 2 ), busB.getDroppedCount() );
    sw::vector<sw::ServerBusMessage> listAfterDrop;
    SW_EXPECT_EQUAL( 0, busB.pollMessages( listAfterDrop ) );

    busB.unsubscribe( "test.cache" );
    SW_REQUIRE_TRUE( sw::ServerBusTopic<sw::ServiceSanctionChanged>( "test.cache" ).publish( bus, makeChanged( 78 ) ) );
    (void)routerB.pump();
    SW_EXPECT_EQUAL( 0, busB.pollMessages( listAfterDrop ) );
}

/**
 * @brief [ServerBusTest] 라우터 없이 만든 캐시 버스: 구독 · 발행은 경고로 버리고, 구독이 없을 때만 라우터를 묶으며 묶은 뒤에는 바꾸지 않는다
 */
SW_TEST_CASE( ServerBusTest, EphemeralBusBindsRouterOnlyBeforeSubscribing )
{
    SW_EXPECT_LOG( "ServerBus.NoRouter" );
    sw::ManualClock             clock( sw::MonotonicMilliseconds{ 1000 } );
    sw::MemoryEphemeralDatabase database( clock );
    sw::MemoryEphemeralStore    store( &database );
    sw::EphemeralStoreRouter    router;
    router.initialize( &store );
    sw::EphemeralStoreRouter otherRouter; // 묶기 거절만 본다(앞에 붙이지 않는다 — 앞 하나는 라우터 하나가 꺼낸다)

    sw::EphemeralServerBus bus( nullptr, kServerA );
    bus.subscribe( "test.late" );
    const uint8 arrPayload[] = { 1 };
    bus.publish( "test.late", arrPayload, 1 );

    SW_REQUIRE_TRUE( bus.bindRouter( &router ) );
    bus.subscribe( "test.late" );
    SW_EXPECT_FALSE( bus.bindRouter( &otherRouter ) ); // 구독이 라우터에 걸려 있다
    bus.publish( "test.late", arrPayload, 1 );
    (void)router.pump();

    sw::vector<sw::ServerBusMessage> listMessage;
    SW_REQUIRE_EQUAL( 1, bus.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( uint64( 1 ), listMessage[0].getSequence() ); // 라우터 없이 버린 발행은 순번을 쓰지 않았다
}
