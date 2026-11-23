// ShooterWeaponSwitchTest — 폰의 의도가 고르는 무기 슬롯: 숫자 버튼은 그 슬롯, 교체 축은 부호대로 앞 · 뒤로 한 칸(끝에서 돈다), 발동이 없으면 그대로(nogpu, 에셋 없음).
// 입력 층에서 한 번 누름이 한 칸인지는 시나리오 game.shooter3d.weaponswitch 가 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

using namespace sw;

namespace
{
    /** @brief 슈터 폰 스키마(버튼 Weapon1..3 · SwitchWeapon, 아날로그 SwitchWeapon)를 든 폰과 의도를 넣는 원격 조종자입니다. */
    struct ShooterWeaponSwitchTestRig
    {
        static constexpr int32 kSwitchButton = 3;

        GameObjectManager          _manager;
        PawnComponent*             _pPawn{ nullptr };
        RemoteControllerComponent* _pRemote{ nullptr };
        uint32                     _tick{ 0 };

        ShooterWeaponSwitchTestRig()
        {
            _pPawn = _manager.createGameObject( hashed_string( "Shooter" ) )->addComponent<PawnComponent>();
            _pPawn->setButtonNames( { hashed_string( "Weapon1" ), hashed_string( "Weapon2" ), hashed_string( "Weapon3" ), hashed_string( "SwitchWeapon" ) } );
            _pPawn->setAnalogNames( { hashed_string( "SwitchWeapon" ) } );
            _pRemote = RemoteControllerComponent::spawnForConnection( _manager, hashed_string( "Remote" ), 0, *_pPawn );
            SW_REQUIRE_NOT_NULL( _pRemote );
            _manager.beginPlay();
        }

        /** @brief 의도 하나를 폰에 넣고, 슬롯 @p currentSlot 에서 고른 슬롯을 돌려줍니다. @p buttonIndex 가 음수면 버튼 없이 축만 낸다. */
        int32 choose( int32 currentSlot, int32 buttonIndex, float32 switchAxis )
        {
            ControlIntent intent;
            if ( buttonIndex >= 0 )
                intent.setButton( buttonIndex, true, true );
            intent._arrAnalog[0] = switchAxis;
            _pRemote->receiveIntent( _tick++, intent );
            _manager.update( 1.0f / 60.0f );
            return ShooterPlayerComponent::chooseWeaponSlot( *_pPawn, currentSlot );
        }
    };
    using Rig = ShooterWeaponSwitchTestRig;
} // namespace

/** @brief [ShooterWeaponSwitchTest] 숫자 버튼은 그 슬롯을 고르고, 지금 슬롯의 버튼이면 그대로다 */
SW_TEST_CASE( ShooterWeaponSwitchTest, NumberButtonsPickTheirSlot )
{
    Rig rig;
    SW_EXPECT_EQUAL( 2, rig.choose( 0, 2, 0.0f ) );
    SW_EXPECT_EQUAL( 0, rig.choose( 2, 0, 0.0f ) );
    SW_EXPECT_EQUAL( 1, rig.choose( 1, 1, 0.0f ) );
}

/** @brief [ShooterWeaponSwitchTest] 교체 축은 부호대로 한 칸씩 가고 양 끝에서 돈다 */
SW_TEST_CASE( ShooterWeaponSwitchTest, SwitchAxisCyclesBothWays )
{
    Rig rig;
    SW_EXPECT_EQUAL( 1, rig.choose( 0, Rig::kSwitchButton, 1.0f ) );
    SW_EXPECT_EQUAL( 0, rig.choose( ShooterPlayerComponent::kWeaponCount - 1, Rig::kSwitchButton, 1.0f ) );
    SW_EXPECT_EQUAL( 1, rig.choose( 2, Rig::kSwitchButton, -1.0f ) );
    SW_EXPECT_EQUAL( ShooterPlayerComponent::kWeaponCount - 1, rig.choose( 0, Rig::kSwitchButton, -1.0f ) );
    SW_EXPECT_EQUAL( 1, rig.choose( 1, Rig::kSwitchButton, 0.0f ) ); // 방향이 없으면 그대로
}

/** @brief [ShooterWeaponSwitchTest] 발동이 없으면 그대로다 — 축 값만 남아 있거나(누른 채) 새 의도가 없는 프레임, 슬롯 버튼이 없는 폰 */
SW_TEST_CASE( ShooterWeaponSwitchTest, NothingHappensWithoutATrigger )
{
    Rig rig;
    SW_EXPECT_EQUAL( 1, rig.choose( 1, -1, 1.0f ) );
    SW_EXPECT_EQUAL( 2, rig.choose( 1, Rig::kSwitchButton, 1.0f ) );
    rig._manager.update( 1.0f / 60.0f ); // 마지막 의도를 잇되 발동은 지났다
    SW_EXPECT_EQUAL( 1, ShooterPlayerComponent::chooseWeaponSlot( *rig._pPawn, 1 ) );

    rig._pPawn->setButtonNames( { hashed_string( "Fire" ) } );
    SW_EXPECT_EQUAL( 1, rig.choose( 1, 0, 1.0f ) );
}
