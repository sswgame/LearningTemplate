#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include <algorithm>

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
            _pRemote = RemoteControllerComponent::spawnForConnection( _manager, hashed_string( "Remote" ), 0, *_pPawn );
            SW_REQUIRE_NOT_NULL( _pRemote );
            _manager.beginPlay();
        }

        void update() { _manager.update( 1.0f / 60.0f ); }

        static ControlIntent makeIntent( float32 forward, int32 buttonIndex, bool bTriggered )
        {
            ControlIntent intent;
            intent._move       = float2{ 0.0f, forward };
            intent._controlYaw = 0.5f;
            intent.setButton( buttonIndex, true );
            if ( bTriggered )
                intent.addTrigger( buttonIndex, intent._controlYaw, 0.0f, ControlViewTick::makePresent() );
            return intent;
        }
    };
} // namespace

/** @brief [RemoteControllerTest] 새 틱이 없는 프레임은 마지막 의도를 잇되 발동은 한 번만 낸다. */
SW_TEST_CASE( RemoteControllerTest, HoldsTheLastIntentAndTriggersOnce )
{
    RemoteControllerTestRig rig;
    rig._pRemote->receiveIntent( 0, RemoteControllerTestRig::makeIntent( 1.0f, 2, true ) );
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 2 ) );
    SW_EXPECT_TRUE( rig._pPawn->wasButtonTriggered( 2 ) );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );

    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 2 ) );
    SW_EXPECT_FALSE( rig._pPawn->wasButtonTriggered( 2 ) );
    SW_EXPECT_NEAR( 1.0f, rig._pPawn->getIntent()._move._y, 1e-3f );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._controlYaw, 1e-3f );
    SW_EXPECT_EQUAL( 0u, rig._pRemote->getMissingTickCount().get() ); // 프레임이 네트 틱보다 빠른 것은 빠진 틱이 아니다
}

/** @brief [RemoteControllerTest] 한 프레임에 여러 틱이 쌓이면 마지막 값에 그 사이의 발동을 모은다. */
SW_TEST_CASE( RemoteControllerTest, MergesTheTicksOfOneFrame )
{
    RemoteControllerTestRig rig;
    ControlIntent           first = RemoteControllerTestRig::makeIntent( 1.0f, 1, false );
    first.addTrigger( 1, 0.25f, 0.125f, ControlViewTick::makeSeen( 10.0 ) );
    ControlIntent second = RemoteControllerTestRig::makeIntent( -1.0f, 3, false );
    second.addTrigger( 1, 0.75f, 0.125f, ControlViewTick::makeSeen( 11.0 ) );
    rig._pRemote->receiveIntent( 0, first );
    rig._pRemote->receiveIntent( 1, second );
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->wasButtonTriggered( 1 ) );
    SW_EXPECT_FALSE( rig._pPawn->isButtonDown( 1 ) ); // 눌림은 마지막 틱의 값이다
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 3 ) );
    SW_EXPECT_NEAR( -1.0f, rig._pPawn->getIntent()._move._y, 1e-3f );
    // 같은 버튼의 두 발동이 저마다의 시선 · 보던 시각으로 남는다.
    const vector<ControlTrigger>& listTrigger = rig._pPawn->getIntent()._listTrigger;
    SW_REQUIRE_EQUAL( size_t{ 2 }, listTrigger.size() );
    SW_EXPECT_NEAR( 0.25f, listTrigger[0].getYaw(), 1e-3f );
    SW_EXPECT_NEAR( 10.0, listTrigger[0].getViewTick().getServerTick( 0.0 ), 1e-6 );
    SW_EXPECT_NEAR( 0.75f, listTrigger[1].getYaw(), 1e-3f );
    SW_EXPECT_NEAR( 11.0, listTrigger[1].getViewTick().getServerTick( 0.0 ), 1e-6 );
    SW_EXPECT_EQUAL( 2u, rig._pRemote->getNextInputTick() );
}

/** @brief [RemoteControllerTest] 뒤의 틱이 왔는데 빠진 틱만 센다. 이미 읽은 틱은 버린다. */
SW_TEST_CASE( RemoteControllerTest, CountsGapsAndDropsOldTicks )
{
    RemoteControllerTestRig rig;
    rig._pRemote->receiveIntent( 0, RemoteControllerTestRig::makeIntent( 1.0f, 0, false ) );
    rig.update();
    rig._pRemote->receiveIntent( 3, RemoteControllerTestRig::makeIntent( 0.5f, 0, false ) );
    rig.update();
    SW_EXPECT_EQUAL( 2u, rig._pRemote->getMissingTickCount().get() );
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._move._y, 1e-2f );

    rig._pRemote->receiveIntent( 2, RemoteControllerTestRig::makeIntent( -1.0f, 0, true ) );
    rig.update();
    SW_EXPECT_NEAR( 0.5f, rig._pPawn->getIntent()._move._y, 1e-2f );
    SW_EXPECT_FALSE( rig._pPawn->wasButtonTriggered( 0 ) );
}

/** @brief [RemoteControllerTest] 잊으면 되풀이도 멈춘다 — 새 연결이 앞 연결의 눌림을 물려받지 않는다. */
SW_TEST_CASE( RemoteControllerTest, ClearReceivedStopsTheRepeat )
{
    RemoteControllerTestRig rig;
    rig._pRemote->receiveIntent( 0, RemoteControllerTestRig::makeIntent( 1.0f, 0, false ) );
    rig.update();
    SW_EXPECT_TRUE( rig._pPawn->isButtonDown( 0 ) );
    rig._pRemote->clearReceived();
    rig.update();
    SW_EXPECT_FALSE( rig._pPawn->isButtonDown( 0 ) );
    SW_EXPECT_NEAR( 0.0f, rig._pPawn->getIntent()._move._y, 1e-5f );
}

/**
 * @brief [RemoteControllerTest] 데이터로 놓지 못한다(`REFLECT( NotPlaceable )`) — 이름 생성 함수가 없어 "Add Component" 목록에 없고, 씬 읽기는 오류를 남기고 만들지 않는다(연결 없는 원격 조종자가 서지 않는다)
 * @details 변이: `NotPlaceable` 을 지우면 생성 함수 단언과 읽은 오브젝트에 조종자가 없다는 단언이 진다.
 */
SW_TEST_CASE( RemoteControllerTest, DataCannotPlaceIt )
{
    SW_EXPECT_LOG( "ObjectStateSerializer.ComponentNotPlaceable" );
    const TypeInfo* pType = RemoteControllerComponent::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    SW_EXPECT_TRUE( pType->_addComponent == nullptr );
    const vector<hashed_string> listPlaceable = GameObjectManager::getRegisteredComponentTypeNames();
    SW_EXPECT_TRUE( std::find( listPlaceable.begin(), listPlaceable.end(), pType->_name ) == listPlaceable.end() );

    RemoteControllerTestRig rig; // 코드의 정본 길(`spawnForConnection`)로 선 조종자를 글로 적어 씬 데이터처럼 읽힌다
    const expected<string>  saved = ObjectStateSerializer::saveToXMLString( rig._pRemote->getOwner(), ObjectSaveOptions{ SerializePurpose::Authoring } );
    SW_REQUIRE_TRUE( saved.has_value() );
    SW_REQUIRE_TRUE( saved->find( pType->_name.c_str() ) != string::npos );
    GameObjectManager placed;
    GameObject*       pPlaced = placed.createGameObject( hashed_string( "Placed" ) );
    SW_REQUIRE_NOT_NULL( pPlaced );
    SW_EXPECT_TRUE( ObjectStateSerializer::loadFromXMLString( pPlaced, *saved, ObjectLoadContext{ SerializePurpose::Authoring } ) );
    SW_EXPECT_NULL( pPlaced->getComponent<RemoteControllerComponent>() );
}
