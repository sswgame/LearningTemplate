#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

using namespace sw;

namespace
{
    /** @brief 폰 하나와 그것을 쥔 원격 조종자입니다. 상대 틱(네트)과 조종 시스템 틱(프레임)의 걸음이 달라도 의도가 맞는지 봅니다. */
    struct RemoteControllerTestRig
    {
        GameObjectManager          _manager;
        PawnComponent*             _pPawn{ nullptr };
        RemoteControllerComponent* _pRemote{ nullptr };

        RemoteControllerTestRig()
        {
            _pPawn   = _manager.createGameObject( hashed_string( "Hero" ) )->addComponent<PawnComponent>();
            _pRemote = _manager.createGameObject( hashed_string( "Remote" ) )->addComponent<RemoteControllerComponent>();
            _manager.beginPlay();
            _pRemote->possess( *_pPawn );
        }

        void tick() { _manager.update( 1.0f / 60.0f ); }

        static ControlIntent makeIntent( float32 forward, int32 buttonIndex, bool bTriggered )
        {
            ControlIntent intent;
            intent._move       = float2{ 0.0f, forward };
            intent._controlYaw = 0.5f;
            intent.setButton( buttonIndex, true, bTriggered );
            return intent;
        }
    };
} // namespace

/** @brief [RemoteControllerTest] 새 틱이 없는 프레임은 마지막 의도를 잇되 발동은 한 번만 낸다. */
SW_TEST_CASE( RemoteControllerTest, HoldsTheLastIntentAndTriggersOnce )
{
    RemoteControllerTestRig rig;
    rig._pRemote->receiveIntent( 0, RemoteControllerTestRig::makeIntent( 1.0f, 2, true ) );
    rig.tick();
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 2 ) );
    SW_EXPECT_TRUE( rig._pPawn->wasButtonTriggered( 2 ) );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );

    rig.tick();
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 2 ) );
    SW_EXPECT_FALSE( rig._pPawn->wasButtonTriggered( 2 ) );
    SW_EXPECT_NEAR( 1.0f, rig._pPawn->getIntent()._move._y, 1e-3f );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );
    SW_EXPECT_EQUAL( 0u, rig._pRemote->getMissingTickCount() ); // 프레임이 네트 틱보다 빠른 것은 빠진 틱이 아니다
}

/** @brief [RemoteControllerTest] 한 프레임에 여러 틱이 쌓이면 마지막 값에 그 사이의 발동을 모은다. */
SW_TEST_CASE( RemoteControllerTest, MergesTheTicksOfOneFrame )
{
    RemoteControllerTestRig rig;
    ControlIntent           first = RemoteControllerTestRig::makeIntent( 1.0f, 1, true );
    first.setTriggerLook( 0.25f, 0.125f );
    rig._pRemote->receiveIntent( 0, first );
    rig._pRemote->receiveIntent( 1, RemoteControllerTestRig::makeIntent( -1.0f, 3, false ) );
    rig.tick();
    SW_EXPECT_TRUE( rig._pPawn->wasButtonTriggered( 1 ) );
    SW_EXPECT_FALSE( rig._pPawn->isButtonDown( 1 ) ); // 눌림은 마지막 틱의 값이다
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 3 ) );
    SW_EXPECT_NEAR( -1.0f, rig._pPawn->getIntent()._move._y, 1e-3f );
    SW_EXPECT_TRUE( rig._pPawn->getIntent()._bTriggerLook );
    SW_EXPECT_NEAR( 0.25f, rig._pPawn->getIntent()._triggerYaw, 1e-3f );
    SW_EXPECT_EQUAL( 2u, rig._pRemote->getNextInputTick() );
}

/** @brief [RemoteControllerTest] 뒤의 틱이 왔는데 빠진 틱만 센다. 이미 읽은 틱은 버린다. */
SW_TEST_CASE( RemoteControllerTest, CountsGapsAndDropsOldTicks )
{
    RemoteControllerTestRig rig;
    rig._pRemote->receiveIntent( 0, RemoteControllerTestRig::makeIntent( 1.0f, 0, false ) );
    rig.tick();
    rig._pRemote->receiveIntent( 3, RemoteControllerTestRig::makeIntent( 0.5f, 0, false ) );
    rig.tick();
    SW_EXPECT_EQUAL( 2u, rig._pRemote->getMissingTickCount() );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._move._y, 1e-2f );

    rig._pRemote->receiveIntent( 2, RemoteControllerTestRig::makeIntent( -1.0f, 0, true ) );
    rig.tick();
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._move._y, 1e-2f );
    SW_EXPECT_FALSE( rig._pPawn->wasButtonTriggered( 0 ) );
}

/** @brief [RemoteControllerTest] 잊으면 되풀이도 멈춘다 — 새 연결이 앞 연결의 눌림을 물려받지 않는다. */
SW_TEST_CASE( RemoteControllerTest, ClearReceivedStopsTheRepeat )
{
    RemoteControllerTestRig rig;
    rig._pRemote->receiveIntent( 0, RemoteControllerTestRig::makeIntent( 1.0f, 0, false ) );
    rig.tick();
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 0 ) );
    rig._pRemote->clearReceived();
    rig.tick();
    SW_EXPECT_FALSE( rig._pPawn->isButtonDown( 0 ) );
    SW_EXPECT_NEAR( 0.0f, rig._pPawn->getIntent()._move._y, 1e-5f );
}
