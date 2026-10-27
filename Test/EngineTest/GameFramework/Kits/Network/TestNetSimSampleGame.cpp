#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"
#include "GameFramework/PlugIn/Editor/NetSimulation/Sample/NetSimSampleGame.h"

#include "TestFramework/TestFramework.h"

// 한 프로세스 재생의 본보기 게임(ServerDebug S8) — 입력 초점이 있는 클라이언트의 아바타만 서버에서 움직이고, 모든 월드가 그 위치를 따라 보인다.

using namespace sw;

namespace
{
    float32 s_moveRight{ 0.0f };
    float32 s_moveFront{ 0.0f };

    /** @brief 테스트가 정한 이동 입력입니다. 초점 거르기는 세션이 한다(초점 없는 월드는 이 함수를 부르지 않고 0 을 보낸다). */
    float2 readTestMove( const NetSimWorld& world )
    {
        (void)world;
        return float2{ s_moveRight, s_moveFront };
    }

    NetSimSampleSettings makeTestSettings()
    {
        NetSimSampleSettings settings;
        settings._pfnMoveSource = &readTestMove;
        return settings;
    }

    [[nodiscard]] bool findAvatarPosition( NetSimWorld& world, uint32 entityID, float3& outPosition )
    {
        GameObject* pAvatar = NetSimSampleGame::findAvatar( world, entityID );
        if ( pAvatar == nullptr || pAvatar->getPrimarySceneComponent() == nullptr )
            return false;
        outPosition = pAvatar->getPrimarySceneComponent()->getWorldPosition();
        return true;
    }

    /** @brief 서버 하나와 클라이언트 둘이 연결된 본보기 게임입니다. 서버가 연결마다 아바타 하나를 만든다(엔티티 id = 연결 id + 1, 여기서는 1 과 2). */
    struct SampleWorlds
    {
        NetSimSampleGame _game{ makeTestSettings() };
        NetSimHarness    _harness{};
        int32            _clientA{ -1 };
        int32            _clientB{ -1 };

        [[nodiscard]] bool start()
        {
            s_moveRight = 0.0f;
            s_moveFront = 0.0f;
            if ( _harness.initialize( SceneEngineServices::makeFromBoundEngine(), NetSimConfig{}, &_game ) == false )
                return false;
            _clientA = _harness.addClient( {} );
            _clientB = _harness.addClient( {} );
            _harness.stepTicks( 120 ); // 연결 · 첫 스냅샷
            return _clientA > 0 && _clientB > 0 && _harness.areAllClientsConnected();
        }
    };
} // namespace

/**
 * @brief [NetSimSampleGameTest] 입력 초점이 있는 클라이언트의 아바타만 서버에서 움직이고, 두 클라이언트 월드가 그 위치를 따라 그린다 — 초점을 옮기면 다른 아바타가 움직인다
 * @details 변이: 초점이 없는 월드도 입력을 보내거나, 서버가 입력을 연결과 맞추지 않으면 두 아바타가 함께 움직여 진다.
 */
SW_TEST_CASE( NetSimSampleGameTest, OnlyTheFocusedClientsAvatarMovesAndEveryWorldFollows )
{
    SampleWorlds worlds;
    SW_ASSERT_TRUE( worlds.start() );
    NetSimWorld& server = worlds._harness.getServer();
    float3       startOne;
    float3       startTwo;
    SW_ASSERT_TRUE( findAvatarPosition( server, 1, startOne ) );
    SW_ASSERT_TRUE( findAvatarPosition( server, 2, startTwo ) );

    // 입력 초점이 없으면 입력이 있어도 아무도 움직이지 않는다.
    SW_ASSERT_TRUE( worlds._harness.setInputFocus( -1 ) );
    s_moveRight = 1.0f;
    worlds._harness.stepTicks( 60 );
    float3 position;
    SW_ASSERT_TRUE( findAvatarPosition( server, 1, position ) );
    SW_EXPECT_NEAR_EQUAL( startOne._x, position._x, 1.0e-4f );
    SW_ASSERT_TRUE( findAvatarPosition( server, 2, position ) );
    SW_EXPECT_NEAR_EQUAL( startTwo._x, position._x, 1.0e-4f );

    // 클라이언트 A 에 초점 — 한 아바타만 오른쪽으로 간다. 멈춘 뒤 세 월드가 같은 위치를 보인다.
    SW_ASSERT_TRUE( worlds._harness.setInputFocus( worlds._clientA ) );
    worlds._harness.stepTicks( 60 );
    s_moveRight = 0.0f;
    worlds._harness.stepTicks( 60 );
    float3 endOne;
    float3 endTwo;
    SW_ASSERT_TRUE( findAvatarPosition( server, 1, endOne ) );
    SW_ASSERT_TRUE( findAvatarPosition( server, 2, endTwo ) );
    const bool bOneMoved = endOne._x - startOne._x > 2.5f;
    const bool bTwoMoved = endTwo._x - startTwo._x > 2.5f;
    SW_ASSERT_TRUE( bOneMoved != bTwoMoved ); // 정확히 하나
    SW_EXPECT_TRUE( bOneMoved );              // 먼저 붙인 클라이언트(월드 1)의 연결 id 가 0 이라 아바타 1 이다 — 에디터 시나리오(`netsim`)가 이 대응을 쓴다
    const uint32 movedEntity = bOneMoved ? 1u : 2u;
    const uint32 stillEntity = bOneMoved ? 2u : 1u;
    const float3 movedEnd    = bOneMoved ? endOne : endTwo;
    const float3 stillStart  = bOneMoved ? startTwo : startOne;
    const float3 stillEnd    = bOneMoved ? endTwo : endOne;
    SW_EXPECT_NEAR_EQUAL( stillStart._x, stillEnd._x, 1.0e-4f );
    for ( const int32 clientIndex : { worlds._clientA, worlds._clientB } )
    {
        NetSimWorld* pClient = worlds._harness.findClient( clientIndex );
        SW_ASSERT_NOT_NULL( pClient );
        float3 shown;
        SW_ASSERT_TRUE( findAvatarPosition( *pClient, movedEntity, shown ) );
        SW_EXPECT_NEAR_EQUAL( movedEnd._x, shown._x, 0.1f );
        SW_EXPECT_NEAR_EQUAL( movedEnd._z, shown._z, 0.1f );
        SW_ASSERT_TRUE( findAvatarPosition( *pClient, stillEntity, shown ) );
        SW_EXPECT_NEAR_EQUAL( stillEnd._x, shown._x, 0.1f );
    }

    // 초점을 클라이언트 B 로 옮기면 다른 아바타가 앞(+z)으로 간다. 앞에서 움직인 아바타는 서 있다.
    SW_ASSERT_TRUE( worlds._harness.setInputFocus( worlds._clientB ) );
    s_moveFront = 1.0f;
    worlds._harness.stepTicks( 60 );
    s_moveFront = 0.0f;
    worlds._harness.stepTicks( 30 );
    float3 movedAfter;
    float3 stillAfter;
    SW_ASSERT_TRUE( findAvatarPosition( server, movedEntity, movedAfter ) );
    SW_ASSERT_TRUE( findAvatarPosition( server, stillEntity, stillAfter ) );
    SW_EXPECT_NEAR_EQUAL( movedEnd._z, movedAfter._z, 1.0e-4f );
    SW_EXPECT_TRUE( stillAfter._z - stillEnd._z > 2.5f );
}

/**
 * @brief [NetSimSampleGameTest] 아바타는 아레나 벽 안에 머문다 — 끝까지 가도 아바타 반크기만큼 안쪽에서 선다
 */
SW_TEST_CASE( NetSimSampleGameTest, AvatarStopsAtTheArenaWall )
{
    SampleWorlds worlds;
    SW_ASSERT_TRUE( worlds.start() );
    SW_ASSERT_TRUE( worlds._harness.setInputFocus( worlds._clientA ) );
    s_moveRight = 1.0f;
    worlds._harness.stepTicks( 400 ); // 5 m/s 로 6.7 초 — 어느 시작 위치에서도 벽에 닿고도 남는다
    NetSimWorld& server = worlds._harness.getServer();
    float3       one;
    float3       two;
    SW_ASSERT_TRUE( findAvatarPosition( server, 1, one ) );
    SW_ASSERT_TRUE( findAvatarPosition( server, 2, two ) );
    const NetSimSampleSettings settings;
    const float32              limit = settings._arenaHalfSize - 0.5f;
    SW_EXPECT_TRUE( one._x <= limit + 1.0e-4f && two._x <= limit + 1.0e-4f );
    SW_EXPECT_TRUE( one._x >= limit - 1.0e-3f || two._x >= limit - 1.0e-3f ); // 움직인 쪽이 벽에 닿았다
}

/**
 * @brief [NetSimSampleGameTest] 클라이언트가 떠나면 서버가 그 아바타를 지우고, 남은 클라이언트 월드에서도 사라진다
 */
SW_TEST_CASE( NetSimSampleGameTest, LeavingClientTakesItsAvatarAway )
{
    SampleWorlds worlds;
    SW_ASSERT_TRUE( worlds.start() );
    NetSimWorld& server   = worlds._harness.getServer();
    NetSimWorld* pClientB = worlds._harness.findClient( worlds._clientB );
    SW_ASSERT_NOT_NULL( pClientB );
    float3 position;
    SW_ASSERT_TRUE( findAvatarPosition( *pClientB, 1, position ) );
    SW_ASSERT_TRUE( findAvatarPosition( *pClientB, 2, position ) );

    worlds._harness.removeClient( worlds._clientA );
    worlds._harness.stepTicks( 120 );
    const bool bServerHasOne = findAvatarPosition( server, 1, position );
    const bool bServerHasTwo = findAvatarPosition( server, 2, position );
    SW_EXPECT_TRUE( bServerHasOne != bServerHasTwo ); // 하나만 남았다
    const bool bClientHasOne = findAvatarPosition( *pClientB, 1, position );
    const bool bClientHasTwo = findAvatarPosition( *pClientB, 2, position );
    SW_EXPECT_TRUE( bClientHasOne == bServerHasOne && bClientHasTwo == bServerHasTwo );
}

/**
 * @brief [NetSimSampleGameTest] 게임이 알리는 카메라와 바닥은 아레나를 덮고, 목록에 등록한 생성 함수가 같은 게임을 만든다
 */
SW_TEST_CASE( NetSimSampleGameTest, RegistryCreatesTheGameAndItDescribesTheFraming )
{
    const NetSimGameEntry entry{ NetSimSampleGame::kName, &NetSimSampleGame::create };
    SW_ASSERT_TRUE( NetSimGameRegistry::registerGame( entry ) );
    const NetSimGameEntry* pEntry = NetSimGameRegistry::findGame( NetSimSampleGame::kName );
    SW_ASSERT_NOT_NULL( pEntry );
    unique_ptr<INetSimGame> pGame = pEntry->_pfnCreate();
    SW_ASSERT_NOT_NULL( pGame.get() );
    NetSimPreviewFraming framing;
    SW_ASSERT_TRUE( pGame->describePreviewFraming( framing ) );
    const NetSimSampleSettings settings;
    SW_EXPECT_TRUE( framing._floorSize > settings._arenaHalfSize * 2.0f );
    SW_EXPECT_TRUE( framing._distance > settings._arenaHalfSize );
    SW_EXPECT_TRUE( NetSimGameRegistry::unregisterGame( entry ) );
}
