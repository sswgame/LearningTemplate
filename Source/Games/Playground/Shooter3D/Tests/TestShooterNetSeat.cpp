// ShooterNetSeatTest — 매치 서버의 자리: 접속마다 몸이 하나고, 자리가 다 차면 더 앉히지 않으며, 떠난 사람의 몸은 거두고 같은 연결 id 의 다른 계정은 앞사람의 몸을 물려받지 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Games/Playground/Shooter3D/Tests/ShooterServerTestWorld.h"

using namespace sw;

/** @brief [ShooterNetSeatTest] 첫 연결은 씬의 몸, 다음 연결은 씬 몸의 설정을 옮긴 새 몸에 앉고, 자리가 다 차면 더 앉히지 않는다 */
SW_TEST_CASE( ShooterNetSeatTest, EachConnectionGetsItsOwnBody )
{
    SW_EXPECT_LOG( "ShooterPlayer.NoWeaponCatalogGameService" ); // 무기 표 · 효과 프리팹이 없다(에셋 없는 테스트)
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    ShooterServerTestWorld world{ ShooterServerTestWorld::WeaponTable::Absent };
    game::ScopedGameScope  scopedScope( world._scope );
    world._manager.beginPlay();
    world.advance();
    SW_REQUIRE_TRUE( world._pNetPawn->isServer() );
    SW_EXPECT_FALSE( world._pNetPawn->hasController( *world._pPlayer ) ); // 아무도 없다 — 판을 두는 것은 씬의 몸 하나다
    SW_EXPECT_TRUE( world.collectPlayers() == vector<GameObjectHandle>{ world._pPlayer->getHandle() } );

    world._peers.connectInWorld( 3, 11 );
    world.advance();
    SW_EXPECT_TRUE( world._pNetPawn->hasController( *world._pPlayer ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, world.collectPlayers().size() );

    world._peers.connectInWorld( 5, 22 );
    world.advance();
    const vector<GameObjectHandle> listSeated = world.collectPlayers();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSeated.size() );
    SW_EXPECT_TRUE( listSeated[0] == world._pPlayer->getHandle() );
    GameObject* pGuest = world._manager.resolveGameObject( listSeated[1] );
    SW_REQUIRE_NOT_NULL( pGuest );
    const PawnComponent*                pGuestPawn = pGuest->getComponent<PawnComponent>();
    const ShooterBodyMovementComponent* pGuestBody = pGuest->getComponent<ShooterBodyMovementComponent>();
    SW_REQUIRE_TRUE( pGuestPawn != nullptr && pGuestBody != nullptr );
    SW_EXPECT_TRUE( pGuestPawn->getButtonNames() == world._pPlayer->getComponent<PawnComponent>()->getButtonNames() ); // 버튼 비트는 스키마의 차례다
    SW_EXPECT_TRUE( pGuestBody->isNetDriven() );
    SW_EXPECT_NEAR( 4.0f, pGuestBody->getFeetPosition()._value._x, 1.0e-3f );
    SW_EXPECT_NEAR( -16.0f, pGuestBody->getFeetPosition()._value._z, 1.0e-3f );
    SW_EXPECT_TRUE( world._pNetPawn->hasController( *pGuest ) );

    world._peers.connectInWorld( 7, 33 );
    world.advance();
    SW_EXPECT_TRUE( world.collectPlayers() == listSeated ); // 자리가 다 찼다
}

/** @brief [ShooterNetSeatTest] 떠난 손님의 몸은 거두고 빈 자리에는 새 몸을 세운다. 같은 연결 id 로 다른 계정이 와도 앞사람의 몸을 물려받지 않고, 씬의 몸에 앉은 사람이 떠나면 남은 사람만 판을 둔다 */
SW_TEST_CASE( ShooterNetSeatTest, LeftSeatsAreCleanedAndNotInherited )
{
    SW_EXPECT_LOG( "ShooterPlayer.NoWeaponCatalogGameService" );
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    ShooterServerTestWorld world{ ShooterServerTestWorld::WeaponTable::Absent };
    game::ScopedGameScope  scopedScope( world._scope );
    world._manager.beginPlay();
    world._peers.connectInWorld( 3, 11 );
    world._peers.connectInWorld( 5, 22 );
    world.advance();
    vector<GameObjectHandle> listSeated = world.collectPlayers();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSeated.size() );
    const GameObjectHandle firstGuest = listSeated[1];

    // 끊긴 것을 서버가 보기 전에 같은 id 로 다른 계정이 붙었다
    world._peers.drop( 5 );
    world._peers.connectInWorld( 5, 44 );
    world.advance();
    listSeated = world.collectPlayers();
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSeated.size() );
    SW_EXPECT_TRUE( listSeated[1] != firstGuest );
    SW_EXPECT_TRUE( world._manager.resolveGameObject( firstGuest ) == nullptr );

    // 씬의 몸에 앉은 사람이 떠난다 — 씬의 몸은 남지만 판을 두는 사람은 아니다
    world._peers.drop( 3 );
    world.advance();
    SW_EXPECT_FALSE( world._pNetPawn->hasController( *world._pPlayer ) );
    SW_EXPECT_TRUE( world.collectPlayers() == vector<GameObjectHandle>{ listSeated[1] } );

    // 다음 사람은 빈 씬의 몸에 앉는다
    world._peers.connectInWorld( 9, 55 );
    world.advance();
    SW_EXPECT_TRUE( world._pNetPawn->hasController( *world._pPlayer ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, world.collectPlayers().size() );

    world._peers.drop( 5 );
    world._peers.drop( 9 );
    world.advance();
    SW_EXPECT_TRUE( world.collectPlayers() == vector<GameObjectHandle>{ world._pPlayer->getHandle() } );
}
