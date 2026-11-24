#include "pch.h"

#include "Core/Network/Message/NetMessage.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Kits/Feature/Network/NetSequence/Editor/Sample/SequenceNetSimGame.h"
#include "GameFramework/Kits/Feature/Network/NetSequence/SequenceNetSessionComponent.h"
#include "GameFramework/Kits/Feature/Network/NetSequence/SequenceReplication.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

// SequenceNetSimTest — 컷씬 재생 복제(GF_NetSequence)를 화면 없는 하니스(서버 1 + 클라이언트 N)로 본다: 서버만 판정, 전원 재생 · 늦게 들어온 클라이언트는 서버 프레임부터,
// 그 플레이어만 재생과 플레이어별 진행 플래그, 건너뛰기 요청은 소유 연결로 검증(에디터 케이스).

using namespace sw;

namespace
{
    constexpr uint32 kConnectTickLimit = 600;

    /** @brief 클라이언트 @p _worldIndex 의 디렉터 @p _pDirector 가 재생 중인지(@p _bPlaying) 묻는 조건입니다. */
    struct PlayingQuery
    {
        int32       _worldIndex{ 0 };
        const utf8* _pDirector{ nullptr };
        uint8       _bPlaying{ SW_TRUE };
    };

    struct SequenceNetSimTestUtil
    {
        static bool areClientsConnected( const NetSimHarness& harness, void* pContext )
        {
            (void)pContext;
            return harness.areAllClientsConnected();
        }

        static bool isPlaying( NetSimWorld& world, const utf8* pDirector )
        {
            const SequencePlayerComponent* pPlayer = SequenceNetSimGame::findDirector( world, pDirector );
            return pPlayer != nullptr && pPlayer->isPlaying();
        }

        static bool matchesPlaying( const NetSimHarness& harness, void* pContext )
        {
            const PlayingQuery& query  = *static_cast<const PlayingQuery*>( pContext );
            NetSimWorld*        pWorld = harness.findClient( query._worldIndex );
            return pWorld != nullptr && pWorld->isConnected() && isPlaying( *pWorld, query._pDirector ) == ( query._bPlaying == SW_TRUE );
        }

        /** @brief 서버 월드의 감시 값(`INetSimSession::readValue`)입니다. 없으면 −1 입니다. */
        static float64 readServerValue( NetSimHarness& harness, string_view key )
        {
            float64               value    = -1.0;
            const INetSimSession* pSession = harness.getServer().getSession();
            if ( pSession == nullptr || pSession->readValue( key, value ) == false )
                return -1.0;
            return value;
        }
    };

    /** @brief 클라이언트 둘이 붙은 하니스입니다(진입은 테스트가 연다). */
    struct SequenceHarnessFixture
    {
        SequenceNetSimGame _game{ SequenceNetSimSchedule::Manual };
        NetSimHarness      _harness{};
        int32              _firstClient{ 0 };
        int32              _secondClient{ 0 };

        SequenceHarnessFixture()
        {
            SceneManager& sceneManager = game::getSubsystem<SceneManager>();
            SW_EXPECT_TRUE( _harness.initialize( sceneManager.getEngineServices(), NetSimConfig{}, &_game ) );
            const expected<int32> firstClient  = _harness.addClient( NetSimLinkConditions{} );
            const expected<int32> secondClient = _harness.addClient( NetSimLinkConditions{} );
            SW_EXPECT_TRUE( firstClient.has_value() && secondClient.has_value() );
            _firstClient  = firstClient.value_or( 0 );
            _secondClient = secondClient.value_or( 0 );
            SW_EXPECT_TRUE( _harness.stepUntil( &SequenceNetSimTestUtil::areClientsConnected, nullptr, kConnectTickLimit ).has_value() );
            _harness.stepTicks( 10 );
        }

        [[nodiscard]] bool waitPlaying( int32 worldIndex, const utf8* pDirector, bool bPlaying, uint32 maxTicks )
        {
            PlayingQuery query{ worldIndex, pDirector, static_cast<uint8>( bPlaying ? SW_TRUE : SW_FALSE ) };
            return _harness.stepUntil( &SequenceNetSimTestUtil::matchesPlaying, &query, maxTicks ).has_value();
        }
    };
} // namespace

/** @brief [SequenceNetSimTest] 서버가 튼 전원 재생은 모든 클라이언트가 틀고, 클라이언트 월드의 트리거는 판정하지 않으며, 늦게 들어온 클라이언트는 서버의 지금 프레임부터 튼다 */
SW_TEST_CASE( SequenceNetSimTest, EveryonePlaysForAllAndLateJoinFollowsTheServerFrame, ._timeoutSeconds = 120 )
{
    SequenceHarnessFixture fixture;
    NetSimHarness&         harness = fixture._harness;
    SW_REQUIRE_TRUE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kEveryoneTrigger, 1 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._firstClient, SequenceNetSimGame::kEveryoneDirector, true, 60 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._secondClient, SequenceNetSimGame::kEveryoneDirector, true, 60 ) );
    SW_EXPECT_EQUAL( 1.0, SequenceNetSimTestUtil::readServerValue( harness, "EveryoneSeen" ) ); // 진행은 서버의 판 상태에

    // 클라이언트 월드의 진입은 판정하지 않는다(서버 권한) — 그 클라이언트에서도 틀지 않는다.
    NetSimWorld* pFirst = harness.findClient( fixture._firstClient );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_FALSE( SequenceNetSimGame::activateTrigger( *pFirst, SequenceNetSimGame::kPlayerTrigger, fixture._firstClient ) );
    SW_EXPECT_FALSE( SequenceNetSimTestUtil::isPlaying( *pFirst, SequenceNetSimGame::kPlayerDirector ) );

    // 늦은 참가 — 서버가 2 초쯤 튼 뒤 들어온 클라이언트는 처음이 아니라 서버의 지금 프레임부터 튼다.
    harness.stepTicks( 120 );
    const expected<int32> lateClient = harness.addClient( NetSimLinkConditions{} );
    SW_REQUIRE_TRUE( lateClient.has_value() );
    SW_EXPECT_TRUE( fixture.waitPlaying( lateClient.value_or( 0 ), SequenceNetSimGame::kEveryoneDirector, true, kConnectTickLimit ) );
    NetSimWorld* pLate = harness.findClient( lateClient.value_or( 0 ) );
    SW_REQUIRE_NOT_NULL( pLate );
    const SequencePlayerComponent* pLatePlayer = SequenceNetSimGame::findDirector( *pLate, SequenceNetSimGame::kEveryoneDirector );
    SW_REQUIRE_NOT_NULL( pLatePlayer );
    SW_EXPECT_TRUE( pLatePlayer->getCurrentFrame() >= 30 );
    SW_EXPECT_EQUAL( 1u, pLatePlayer->getHistory().countKind( SequenceHistoryKind::Started ) );

    // 서버 재생이 끝나면 모두 끝난다.
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._firstClient, SequenceNetSimGame::kEveryoneDirector, false, 700 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( lateClient.value_or( 0 ), SequenceNetSimGame::kEveryoneDirector, false, 30 ) );
    const SequenceNetSessionComponent* pServerNet = SequenceNetSimGame::findNetSession( harness.getServer() );
    SW_REQUIRE_NOT_NULL( pServerNet );
    SW_EXPECT_EQUAL( 0u, pServerNet->getServer().getActivePlayCount() );
}

/** @brief [SequenceNetSimTest] 그 플레이어만 재생은 활성자 폰을 가진 클라이언트만 틀고, 진행은 그 플레이어의 상태에 적혀 다른 플레이어는 따로 한 번 튼다 */
SW_TEST_CASE( SequenceNetSimTest, ActivatorPlaysOnlyForThatPlayerAndRecordsPerPlayer, ._timeoutSeconds = 120 )
{
    SequenceHarnessFixture fixture;
    NetSimHarness&         harness = fixture._harness;
    SW_REQUIRE_TRUE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 2 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._secondClient, SequenceNetSimGame::kPlayerDirector, true, 60 ) );
    harness.stepTicks( 10 ); // 첫 클라이언트에는 오지 않는다(일어나지 않음 — 시간이 내용)
    NetSimWorld* pFirst = harness.findClient( fixture._firstClient );
    SW_REQUIRE_NOT_NULL( pFirst );
    const SequencePlayerComponent* pFirstPlayer = SequenceNetSimGame::findDirector( *pFirst, SequenceNetSimGame::kPlayerDirector );
    SW_REQUIRE_NOT_NULL( pFirstPlayer );
    SW_EXPECT_FALSE( pFirstPlayer->isPlaying() );
    SW_EXPECT_EQUAL( 0u, pFirstPlayer->getHistory().countKind( SequenceHistoryKind::Started ) );
    SW_EXPECT_EQUAL( 1.0, SequenceNetSimTestUtil::readServerValue( harness, "PlayerSeen2" ) );
    SW_EXPECT_EQUAL( 0.0, SequenceNetSimTestUtil::readServerValue( harness, "PlayerSeen1" ) );
    SW_EXPECT_EQUAL( 0.0, SequenceNetSimTestUtil::readServerValue( harness, "EveryoneSeen" ) );

    // 끝난 뒤 — 같은 플레이어는 다시 틀지 않고(플레이어별 한 번), 다른 플레이어는 자기 몫으로 한 번 튼다.
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._secondClient, SequenceNetSimGame::kPlayerDirector, false, 200 ) );
    SW_EXPECT_FALSE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 2 ) );
    SW_EXPECT_TRUE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 1 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._firstClient, SequenceNetSimGame::kPlayerDirector, true, 60 ) );
    SW_EXPECT_EQUAL( 1.0, SequenceNetSimTestUtil::readServerValue( harness, "PlayerSeen1" ) );
}

/** @brief [SequenceNetSimTest] 다른 플레이어만의 재생이 도는 동안 들어온 플레이어도 자기 컷씬을 튼다 — 한쪽의 건너뛰기는 그 재생만 끝낸다(예전에는 디렉터 하나를 나눠 둘째 진입이 막혔다) */
SW_TEST_CASE( SequenceNetSimTest, ActivatorPlaysRunSideBySide, ._timeoutSeconds = 120 )
{
    SequenceHarnessFixture fixture;
    NetSimHarness&         harness = fixture._harness;
    SW_REQUIRE_TRUE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 1 ) );
    SW_REQUIRE_TRUE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 2 ) ); // 같은 갱신 — 둘 다 보낸다
    SW_EXPECT_FALSE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 1 ) ); // 자기 재생 중 재진입
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._firstClient, SequenceNetSimGame::kPlayerDirector, true, 60 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._secondClient, SequenceNetSimGame::kPlayerDirector, true, 60 ) );
    const SequenceNetSessionComponent* pServerNet = SequenceNetSimGame::findNetSession( harness.getServer() );
    SW_REQUIRE_NOT_NULL( pServerNet );
    SW_EXPECT_EQUAL( 2u, pServerNet->getServer().getActivePlayCount() );

    NetSimWorld* pSecond = harness.findClient( fixture._secondClient );
    SW_REQUIRE_NOT_NULL( pSecond );
    SequencePlayerComponent* pSecondPlayer = SequenceNetSimGame::findDirector( *pSecond, SequenceNetSimGame::kPlayerDirector );
    SW_REQUIRE_NOT_NULL( pSecondPlayer );
    SW_EXPECT_TRUE( pSecondPlayer->skipToEnd() );
    for ( uint32 tick = 0; tick < 30 && pServerNet->getServer().getActivePlayCount() > 1u; ++tick )
    {
        harness.step();
    }
    SW_EXPECT_EQUAL( 1u, pServerNet->getServer().getActivePlayCount() ); // 둘째의 재생만 끝났다
    NetSimWorld* pFirst = harness.findClient( fixture._firstClient );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( SequenceNetSimTestUtil::isPlaying( *pFirst, SequenceNetSimGame::kPlayerDirector ) );
}

/** @brief [SequenceNetSimTest] 건너뛰기 요청은 그 재생의 폰을 가진 연결에서 온 것만 서버 재생을 끝내고, 다른 연결이 보낸 같은 요청은 버린다 */
SW_TEST_CASE( SequenceNetSimTest, SkipRequestIsCheckedAgainstTheOwningConnection, ._timeoutSeconds = 120 )
{
    SequenceHarnessFixture fixture;
    NetSimHarness&         harness = fixture._harness;
    SW_REQUIRE_TRUE( SequenceNetSimGame::activateTrigger( harness.getServer(), SequenceNetSimGame::kPlayerTrigger, 1 ) );
    SW_EXPECT_TRUE( fixture.waitPlaying( fixture._firstClient, SequenceNetSimGame::kPlayerDirector, true, 60 ) );

    // 둘째 클라이언트가 첫 재생의 건너뛰기를 위조한다 — 서버는 버리고 재생을 잇는다.
    NetSimWorld* pSecond = harness.findClient( fixture._secondClient );
    SW_REQUIRE_NOT_NULL( pSecond );
    NetMessageWriter           writer;
    SequenceNetSkipRequestBody request;
    request._playID = 1; // 이 씬의 첫 재생
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetSequenceMessage::Table, NetSequenceMessage::SkipRequest>( request ) ) );
    SW_REQUIRE_TRUE( writer.sendMessage( pSecond->getPeers(), 0 ) );
    // 소유 연결이라도 앞 씬에서 보낸 늦은 요청은 라우터가 거른다 — 번호가 같은 이 씬의 재생에 닿지 않는다.
    NetSimWorld* pFirst = harness.findClient( fixture._firstClient );
    SW_REQUIRE_NOT_NULL( pFirst );
    INetPeers&  firstPeers      = pFirst->getPeers();
    const uint8 worldGeneration = firstPeers.getWorldGeneration();
    SW_EXPECT_NOT_EQUAL( 0u, static_cast<uint32>( worldGeneration ) );
    firstPeers.setWorldGeneration( static_cast<uint8>( worldGeneration + 1u ) ); // 다른 월드에서 보낸 것처럼
    SW_REQUIRE_TRUE( ( writer.writeMessage<NetSequenceMessage::Table, NetSequenceMessage::SkipRequest>( request ) ) );
    SW_REQUIRE_TRUE( writer.sendMessage( firstPeers, 0 ) );
    firstPeers.setWorldGeneration( worldGeneration );
    harness.stepTicks( 5 );
    SW_EXPECT_EQUAL( 1.0, SequenceNetSimTestUtil::readServerValue( harness, "SkipRejected" ) );
    SW_EXPECT_TRUE( SequenceNetSimGame::isTriggerPlaying( harness.getServer(), SequenceNetSimGame::kPlayerTrigger ) );
    SW_EXPECT_FALSE( SequenceNetSimTestUtil::isPlaying( harness.getServer(), SequenceNetSimGame::kPlayerDirector ) ); // 원격 플레이어만의 재생은 서버의 씬 디렉터를 쓰지 않는다

    // 그 플레이어의 로컬 건너뛰기 — 클라이언트가 요청하고 서버가 소유 연결을 확인해 끝낸다.
    SequencePlayerComponent* pFirstPlayer = SequenceNetSimGame::findDirector( *pFirst, SequenceNetSimGame::kPlayerDirector );
    SW_REQUIRE_NOT_NULL( pFirstPlayer );
    SW_EXPECT_TRUE( pFirstPlayer->skipToEnd() );
    for ( uint32 tick = 0; tick < 30 && SequenceNetSimGame::isTriggerPlaying( harness.getServer(), SequenceNetSimGame::kPlayerTrigger ); ++tick )
    {
        harness.step();
    }
    SW_EXPECT_FALSE( SequenceNetSimGame::isTriggerPlaying( harness.getServer(), SequenceNetSimGame::kPlayerTrigger ) );
    SW_EXPECT_EQUAL( 1.0, SequenceNetSimTestUtil::readServerValue( harness, "SkipAccepted" ) );
    SW_EXPECT_EQUAL( 1.0, SequenceNetSimTestUtil::readServerValue( harness, "SkipRejected" ) );
}
