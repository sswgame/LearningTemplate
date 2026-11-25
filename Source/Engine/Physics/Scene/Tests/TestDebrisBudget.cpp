// DebrisBudget — 씬의 파편 바디 장부: 상한을 넘으면 씬 전체에서 작은 것 · 오래된 것부터 고르고, 주인은 자기 몫만 꺼낸다(감사 S-11).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Physics/Scene/DebrisBudget.h"

using namespace sw;

/**
 * @brief [DebrisBudgetTest] 씬 상한을 넘으면 어느 오브젝트가 먼저 틱하든 씬 전체에서 가장 오래된 작은 파편이 사라진다 — 예전에는 먼저 틱한 오브젝트가
 *        자기 (더 젊은) 파편을 지워 값을 치렀다. 큰 덩어리는 작은 것을 모두 고른 뒤에 고른다.
 */
SW_TEST_CASE( DebrisBudgetTest, GlobalBudgetEvictsOldestAcrossObjects )
{
    DebrisBudget        budget;
    const DebrisOwnerID oldObject   = budget.addOwner();
    const DebrisOwnerID youngObject = budget.addOwner();
    budget.addBody( oldObject, 1, false );   // 가장 오래된 작은 파편
    budget.addBody( oldObject, 2, true );    // 큰 덩어리
    budget.addBody( youngObject, 7, false ); // 더 젊은 작은 파편
    budget.addBody( youngObject, 8, false );

    budget.planRemovals( 2 );
    vector<uint32> listYoung;
    budget.takeRemovals( youngObject, listYoung ); // 젊은 쪽이 먼저 꺼내도
    vector<uint32> listOld;
    budget.takeRemovals( oldObject, listOld );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listOld.size() );
    SW_EXPECT_EQUAL( 1u, listOld[0] ); // 오래된 쪽의 작은 파편
    SW_REQUIRE_EQUAL( size_t{ 1 }, listYoung.size() );
    SW_EXPECT_EQUAL( 7u, listYoung[0] ); // 큰 덩어리(2)보다 작은 파편이 먼저다

    // 넘긴 몫은 지워질 것으로 센다 — 주인이 지우기 전에 다시 골라도 더 고르지 않는다.
    budget.planRemovals( 2 );
    listOld.clear();
    budget.takeRemovals( oldObject, listOld );
    SW_EXPECT_EQUAL( size_t{ 1 }, listOld.size() );

    budget.removeBody( oldObject, 1 );
    budget.removeBody( youngObject, 7 );
    SW_EXPECT_EQUAL( 2u, budget.getBodyCount() );
    budget.planRemovals( 2 );
    listOld.clear();
    budget.takeRemovals( oldObject, listOld );
    SW_EXPECT_TRUE( listOld.empty() ); // 상한 안이다

    budget.removeOwner( oldObject );
    SW_EXPECT_EQUAL( 1u, budget.getBodyCount() );
}
