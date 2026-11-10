#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Sample/NetSimSampleGame.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

// NetSimSampleGameTest — 본보기 게임을 화면 없는 하니스(월드가 씬을 가진다)로 돌린다: 연결마다 아바타 하나, 초점 월드의 입력만 움직임, 아레나 벽, 클라이언트 수렴, 이탈의 뒷정리(에디터 케이스).

using namespace sw;

namespace
{
    constexpr uint32 kConnectTickLimit = 600;
    constexpr uint32 kFirstEntityID    = 1; ///< 첫 연결(0)의 아바타(엔티티 id = 연결 id + 1)
    constexpr uint32 kSecondEntityID   = 2;

    struct NetSimSampleGameTestUtil
    {
        /** @brief 오른쪽으로 계속 가는 입력입니다. 입력 초점이 있는 월드에서만 불립니다. */
        static float2 moveRight( const NetSimWorld& world )
        {
            (void)world;
            return float2{ 1.0f, 0.0f };
        }

        static bool areClientsConnected( const NetSimHarness& harness, void* pContext )
        {
            (void)pContext;
            return harness.areAllClientsConnected();
        }

        [[nodiscard]] static bool findAvatarX( NetSimWorld& world, uint32 entityID, float32& outX )
        {
            const GameObject* pAvatar = NetSimSampleGame::findAvatar( world, entityID );
            if ( pAvatar == nullptr || pAvatar->getPrimarySceneComponent() == nullptr )
                return false;
            outX = pAvatar->getPrimarySceneComponent()->getWorldPosition()._x;
            return true;
        }
    };

    /** @brief 클라이언트 둘이 붙은 하니스입니다. 아바타가 서는 데 몇 틱 더 돌립니다. */
    struct SampleHarnessFixture
    {
        NetSimSampleGame _game;
        NetSimHarness    _harness{};
        int32            _firstClient{};
        int32            _secondClient{};

        SampleHarnessFixture()
            : _game{
                  NetSimSampleSettings{ 8.0f, 5.0f, &NetSimSampleGameTestUtil::moveRight }
        }
        {
            SceneManager& sceneManager = game::getSubsystem<SceneManager>();
            const bool    bStarted     = _harness.initialize( sceneManager.getEngineServices(), NetSimConfig{}, &_game );
            SW_EXPECT_TRUE( bStarted );
            const expected<int32> firstClient  = _harness.addClient( NetSimLinkConditions{} );
            const expected<int32> secondClient = _harness.addClient( NetSimLinkConditions{} );
            SW_EXPECT_TRUE( firstClient.has_value() && secondClient.has_value() );
            _firstClient  = firstClient.value();
            _secondClient = secondClient.value();
            SW_EXPECT_TRUE( _harness.stepUntil( &NetSimSampleGameTestUtil::areClientsConnected, nullptr, kConnectTickLimit ) >= 0 );
            _harness.stepTicks( 10 );
        }
    };
} // namespace

SW_TEST_CASE( NetSimSampleGameTest, EveryConnectionGetsAnAvatarInEveryWorld, ._timeoutSeconds = 60 )
{
    SampleHarnessFixture fixture;
    vector<NetSimWorld*> listClient;
    fixture._harness.collectClients( listClient );
    SW_REQUIRE_EQUAL( size_t( 2 ), listClient.size() );

    float32 x = 0.0f;
    SW_EXPECT_TRUE( NetSimSampleGameTestUtil::findAvatarX( fixture._harness.getServer(), kFirstEntityID, x ) );
    SW_EXPECT_TRUE( NetSimSampleGameTestUtil::findAvatarX( fixture._harness.getServer(), kSecondEntityID, x ) );
    for ( NetSimWorld* pClient : listClient )
    {
        SW_EXPECT_TRUE( NetSimSampleGameTestUtil::findAvatarX( *pClient, kFirstEntityID, x ) );
        SW_EXPECT_TRUE( NetSimSampleGameTestUtil::findAvatarX( *pClient, kSecondEntityID, x ) );
    }
    SW_EXPECT_NULL( NetSimSampleGame::findAvatar( fixture._harness.getServer(), 3 ) ); // 서버 자신의 아바타는 없다
}

/** @brief 초점 없는 월드의 입력은 읽지 않는다. 초점을 받은 클라이언트의 아바타 하나만 움직이고 벽에서 멈춘다. 초점을 놓으면 선다. */
SW_TEST_CASE( NetSimSampleGameTest, OnlyTheFocusedClientMovesAndStopsAtTheWall, ._timeoutSeconds = 60 )
{
    SampleHarnessFixture fixture;
    NetSimWorld&         server      = fixture._harness.getServer();
    float32              firstStart  = 0.0f;
    float32              secondStart = 0.0f;
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, kFirstEntityID, firstStart ) );
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, kSecondEntityID, secondStart ) );

    fixture._harness.stepTicks( 60 ); // 초점 없음
    float32 firstX  = 0.0f;
    float32 secondX = 0.0f;
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, kFirstEntityID, firstX ) );
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, kSecondEntityID, secondX ) );
    SW_EXPECT_EQUAL( firstStart, firstX );
    SW_EXPECT_EQUAL( secondStart, secondX );

    SW_REQUIRE_TRUE( fixture._harness.setInputFocus( fixture._firstClient ) );
    fixture._harness.stepTicks( 60 );
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, kFirstEntityID, firstX ) );
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, kSecondEntityID, secondX ) );
    const bool bFirstMoved  = firstX != firstStart;
    const bool bSecondMoved = secondX != secondStart;
    SW_EXPECT_TRUE( bFirstMoved != bSecondMoved ); // 어느 쪽이 첫 연결인지는 도착 순서다 — 하나만 움직인다

    fixture._harness.stepTicks( 600 ); // 5 m/s 로 10 초 — 벽(8 − 아바타 절반 0.5)을 넘길 시간이다
    const uint32 movedEntityID = bFirstMoved ? kFirstEntityID : kSecondEntityID;
    float32      wallX         = 0.0f;
    SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( server, movedEntityID, wallX ) );
    SW_EXPECT_NEAR( 7.5f, wallX, 0.001f );

    SW_REQUIRE_TRUE( fixture._harness.setInputFocus( -1 ) );
    fixture._harness.stepTicks( 30 );
    // 클라이언트는 받은 위치로 다가가 붙는다 — 멈춘 뒤 서버와 같은 값이다.
    vector<NetSimWorld*> listClient;
    fixture._harness.collectClients( listClient );
    for ( NetSimWorld* pClient : listClient )
    {
        float32 shownX = 0.0f;
        SW_REQUIRE_TRUE( NetSimSampleGameTestUtil::findAvatarX( *pClient, movedEntityID, shownX ) );
        SW_EXPECT_NEAR( wallX, shownX, 0.001f );
    }
}

/** @brief 떠난 클라이언트의 아바타는 서버와 남은 클라이언트에서 사라진다. */
SW_TEST_CASE( NetSimSampleGameTest, LeavingClientsAvatarIsRemoved, ._timeoutSeconds = 60 )
{
    SampleHarnessFixture fixture;
    NetSimWorld&         server      = fixture._harness.getServer();
    float32              x           = 0.0f;
    const uint32         arrEntity[] = { kFirstEntityID, kSecondEntityID };
    fixture._harness.removeClient( fixture._firstClient );
    fixture._harness.stepTicks( 30 );
    SW_EXPECT_EQUAL( 1, fixture._harness.getClientCount() );

    int32 serverAvatarCount = 0;
    for ( const uint32 entityID : arrEntity )
    {
        serverAvatarCount += NetSimSampleGameTestUtil::findAvatarX( server, entityID, x ) ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, serverAvatarCount );

    NetSimWorld* pRemaining = fixture._harness.findClient( fixture._secondClient );
    SW_REQUIRE_NOT_NULL( pRemaining );
    int32 clientAvatarCount = 0;
    for ( const uint32 entityID : arrEntity )
    {
        clientAvatarCount += NetSimSampleGameTestUtil::findAvatarX( *pRemaining, entityID, x ) ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, clientAvatarCount );
}
