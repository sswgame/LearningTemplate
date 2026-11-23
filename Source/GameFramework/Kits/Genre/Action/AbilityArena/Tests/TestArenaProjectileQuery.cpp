#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaProjectileQuery.h"

using namespace sw;

namespace
{
    /** @brief 살아 있는 Grunt 하나의 스냅샷입니다. */
    ArenaUnitView makeGrunt( int32 unitID, float32 x )
    {
        ArenaUnitView view;
        view._unitID   = unitID;
        view._position = AbsolutePosition{
            float3{ x, 0.0f, 0.0f }
        };
        view._kind   = ArenaUnitKind::Grunt;
        view._bAlive = SW_TRUE;
        return view;
    }
} // namespace

/**
 * @brief [ArenaProjectileQueryTest] 관통 탄이 기억한 대상은 유닛 번호라, 앞 유닛이 스냅샷에서 빠져도 다음 유닛을 거르지 않는다
 * @details 예전에는 스냅샷 첨자를 기억해 A 가 빠지면 C 가 B 의 첨자(1)를 받아 이미 맞힌 것으로 걸러졌다. 변이: `hasHit( view._unitID )` 를 첨자로 되돌리면 첫 단언이 진다.
 */
SW_TEST_CASE( ArenaProjectileQueryTest, PierceMemoryFollowsUnitIDNotSnapshotIndex )
{
    ProjectileState bullet;
    bullet._team = ArenaProjectileQuery::kTeamPlayer;
    bullet.rememberHit( 1 ); // 지난 프레임에 B(번호 1)를 꿰뚫었다 — 그때 스냅샷은 A B C

    const vector<ArenaUnitView> listView{ makeGrunt( 1, 2.0f ), makeGrunt( 2, 4.0f ) }; // A 가 정리되어 B C 만 남았다
    const ArenaProjectileQuery  query( listView, 0.5f, 100.0f );
    ProjectileHit               hit;
    SW_REQUIRE_TRUE( query.sweep( bullet, float3{ 0.0f, 0.0f, 0.0f }, float3{ 10.0f, 0.0f, 0.0f }, hit ) );
    SW_EXPECT_TRUE( hit._kind == ProjectileHitKind::Target );
    SW_EXPECT_EQUAL( 2, hit._other );
    const ArenaUnitView* pVictim = query.findView( hit._other );
    SW_REQUIRE_TRUE( pVictim != nullptr );
    SW_EXPECT_EQUAL( 4.0f, pVictim->_position._value._x );
    SW_EXPECT_TRUE( query.findView( 0 ) == nullptr ); // 빠진 A 는 찾지 않는다
}
