// RuleHealthViewTest — 키트 규칙 체력의 뷰(`RuleHealthViewComponent`): 디렉터가 넣은 스냅숏이 같은 오브젝트의 HP 바(공용 체력 소비자)에 체력 원천 계약으로 간다(nogpu, 마커 없음 — 플레이를 시작하지 않는다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Client/Marker/HealthBarComponent.h"
#include "GameFramework/Runtime/Character/Combat/Health/RuleHealthViewComponent.h"

using namespace sw;

namespace
{
    /** @brief 체력 뷰와 HP 바를 단 모습 오브젝트 하나입니다. */
    struct RuleHealthViewFixture
    {
        GameObjectManager        _manager;
        RuleHealthViewComponent* _pView{ nullptr };
        HealthBarComponent*      _pBar{ nullptr };

        RuleHealthViewFixture()
        {
            GameObject* const pObject = _manager.createGameObject( hashed_string( "UnitView" ) );
            _pView                    = pObject->addComponent<RuleHealthViewComponent>();
            _pBar                     = pObject->addComponent<HealthBarComponent>();
        }
    };
} // namespace

/** @brief [RuleHealthViewTest] 스냅숏 전에는 체력이 없는 주인으로 읽혀 HP 바가 저장된 비율을 쓴다 — 0 을 지어내 쓰러짐으로 보이지 않는다 */
SW_TEST_CASE( RuleHealthViewTest, BeforeSnapshotReadsAsNoHealth )
{
    RuleHealthViewFixture rig;
    SW_REQUIRE_TRUE( rig._pView != nullptr && rig._pBar != nullptr );
    const HealthReading reading = rig._pView->getHealthReading();
    SW_EXPECT_TRUE( reading._bHasHealth == SW_FALSE );
    SW_EXPECT_TRUE( reading._bDead == SW_FALSE );
    SW_EXPECT_FALSE( rig._pView->hasSnapshot() );
}

/** @brief [RuleHealthViewTest] 첫 스냅숏은 연출 없이(Reset) 그 비율로 두고, 바뀐 스냅숏은 목표만 옮긴다(Changed) — HP 바가 규칙 체력을 따라간다 */
SW_TEST_CASE( RuleHealthViewTest, SnapshotDrivesHealthBar )
{
    RuleHealthViewFixture rig;
    SW_REQUIRE_TRUE( rig._pView != nullptr && rig._pBar != nullptr );
    rig._pBar->resetRatio( 1.0f );

    rig._pView->applySnapshot( 80.0f, 100.0f );
    SW_EXPECT_TRUE( rig._pView->hasSnapshot() );
    SW_EXPECT_NEAR( 0.8f, rig._pBar->getTargetRatio(), 1.0e-6f );
    SW_EXPECT_NEAR( 0.8f, rig._pBar->getHPRatio(), 1.0e-6f );                                                  // Reset — 흔적 없이 바로 그 길이
    SW_EXPECT_NEAR( 0.8f, rig._pView->getOwner()->findInterface<IHealthSource>()->getHealthRatio(), 1.0e-6f ); // 체력 원천은 인터페이스로 찾힌다

    rig._pView->applySnapshot( 30.0f, 100.0f );
    SW_EXPECT_NEAR( 0.3f, rig._pBar->getTargetRatio(), 1.0e-6f );
    SW_EXPECT_NEAR( 0.8f, rig._pBar->getHPRatio(), 1.0e-6f ); // Changed — 채움은 틱이 옮긴다
}

/** @brief [RuleHealthViewTest] 같은 값의 스냅숏은 알리지 않는다 — 디렉터가 매 걸음 넣어도 HP 바의 목표를 다시 쓰지 않는다 */
SW_TEST_CASE( RuleHealthViewTest, SameSnapshotDoesNotNotify )
{
    RuleHealthViewFixture rig;
    SW_REQUIRE_TRUE( rig._pView != nullptr && rig._pBar != nullptr );
    rig._pView->applySnapshot( 50.0f, 100.0f );
    rig._pBar->setTargetRatio( 0.9f ); // 알림이 오면 0.5 로 돌아간다
    rig._pView->applySnapshot( 50.0f, 100.0f );
    SW_EXPECT_NEAR( 0.9f, rig._pBar->getTargetRatio(), 1.0e-6f );
}

/** @brief [RuleHealthViewTest] 체력 0 의 스냅숏은 쓰러짐(Died)으로 읽히고 HP 바가 0 을 향한다. 다시 연 판(resetSnapshot)은 연출 없이 가득으로 돌아온다 */
SW_TEST_CASE( RuleHealthViewTest, ZeroIsDeathAndResetStartsOver )
{
    RuleHealthViewFixture rig;
    SW_REQUIRE_TRUE( rig._pView != nullptr && rig._pBar != nullptr );
    rig._pView->applySnapshot( 100.0f, 100.0f );
    rig._pView->applySnapshot( 0.0f, 100.0f );
    SW_EXPECT_TRUE( rig._pView->getHealthReading()._bDead == SW_TRUE );
    SW_EXPECT_NEAR( 0.0f, rig._pBar->getTargetRatio(), 1.0e-6f );

    rig._pView->resetSnapshot();
    rig._pView->applySnapshot( 100.0f, 100.0f );
    SW_EXPECT_NEAR( 1.0f, rig._pBar->getHPRatio(), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, rig._pBar->getRemainRatio(), 1.0e-6f );
}
