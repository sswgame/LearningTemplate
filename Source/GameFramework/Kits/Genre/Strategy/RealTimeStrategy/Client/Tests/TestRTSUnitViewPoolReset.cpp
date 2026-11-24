// RTSUnitViewPoolResetTest — 죽음 · 안개로 빠진 유닛 뷰를 풀(`PrefabPool`)에 돌려주면 다시 꺼낸 뷰가 갓 스폰한 뷰와 같다(따라가는 유닛 없음 · 자리 맞추기 전 · 메시 숨김).
// 인스턴스는 프리팹 대신 같은 컴포넌트를 코드로 단 오브젝트다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSUnitViewComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Foundation/GameMode/Tests/PrefabPoolComponentSpawner.h"

using namespace sw;

/** @brief [RTSUnitViewPoolResetTest] 유닛을 따라가던 뷰를 돌려주고 다시 꺼내면 같은 오브젝트가 배정 전 뷰다 */
SW_TEST_CASE( RTSUnitViewPoolResetTest, ReleasedViewComesBackUnassigned )
{
    GameObjectManager manager;
    PrefabPool        pool    = PrefabPoolComponentSpawner<MeshComponent, RTSUnitViewComponent>::makePool( 4 );
    GameObject*       pObject = pool.acquire( manager, "RTSUnit" );
    SW_REQUIRE_NOT_NULL( pObject ); // 처음 만든 인스턴스로 풀 계약을 확인했다
    RTSUnitViewComponent* pView = pObject->getComponent<RTSUnitViewComponent>();
    SW_REQUIRE_NOT_NULL( pView );
    const SlotHandle freshUnit = pView->getUnitID();

    pView->assignUnit( GameObjectHandle{}, SlotHandle::make( 3, 1 ), 2.0f );
    SW_REQUIRE_TRUE( pView->getUnitID().isValid() );

    pool.release( manager, pObject->getHandle() );
    SW_REQUIRE_TRUE( pool.acquire( manager, "RTSUnit" ) == pObject );
    SW_EXPECT_EQUAL( freshUnit.isValid(), pView->getUnitID().isValid() );
    SW_EXPECT_FALSE( pView->isPlaced() );
    SW_EXPECT_NEAR( -1.0f, pView->measureViewLag(), 1.0e-6f );
    SW_EXPECT_EQUAL( uint64{ 1 }, pool.getCreatedCount().get() );
}
