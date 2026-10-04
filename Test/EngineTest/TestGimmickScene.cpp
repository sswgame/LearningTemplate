// 씬의 기믹 — 회로 컴포넌트가 센서 컴포넌트(겹침 · 무게)를 읽고 액추에이터(문 · 무버)를 오브젝트에 건다, 상태가 오브젝트 상태와 함께 저장된다, 로드 때 검증.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"

#include "GameFramework/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Spline/SplineComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct GimmickSceneTestInternal
    {
        static GameObject* spawnBox( GameObjectManager& manager, const utf8* pName, const float3& position, bool bTrigger )
        {
            GameObject*             pObject = manager.createGameObject( hashed_string( pName ) );
            BoxCollider2DComponent* pBox    = pObject != nullptr ? pObject->addComponent<BoxCollider2DComponent>() : nullptr;
            if ( pBox == nullptr )
                return nullptr;
            pBox->setOffsetScale( float2{ 2.0f, 2.0f } );
            pBox->setTrigger( bTrigger );
            pBox->setLocalPosition( position );
            return pObject;
        }

        static void tickFrames( GameObjectManager& manager, int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
                manager.tick( 1.0f / 60.0f );
        }

        static float32 getY( const GameObject* pObject ) { return pObject->getPrimarySceneComponent()->getWorldPosition()._y; }
    };
} // namespace

/**
 * @brief [GimmickSceneTest] 2D 눌림판 — 트리거 상자에 무거운 상자가 들어오면 문이 열리고(가벼운 것은 못 연다), 나가면 닫힌다
 * @details 센서는 엔진 겹침 훅으로 겹친 것을 세고 무게를 더한다. 회로는 다음 틱에 읽어 문 오브젝트의 로컬 자리를 쉬는 자세 + 열림 × openOffset 로 쓴다.
 */
SW_TEST_CASE( GimmickSceneTest, PressurePlateInSceneDrivesDoor )
{
    GameObjectManager manager;
    GameObject*       pPlate = GimmickSceneTestInternal::spawnBox( manager, "Plate", float3{ 0.0f, 0.0f, 0.0f }, true );
    GameObject*       pHeavy = GimmickSceneTestInternal::spawnBox( manager, "Heavy", float3{ 20.0f, 0.0f, 0.0f }, false );
    GameObject*       pLight = GimmickSceneTestInternal::spawnBox( manager, "Light", float3{ -20.0f, 0.0f, 0.0f }, false );
    GameObject*       pDoor  = manager.createGameObject( hashed_string( "Door" ) );
    GameObject*       pLogic = manager.createGameObject( hashed_string( "Logic" ) );
    SW_ASSERT_TRUE( pPlate != nullptr && pHeavy != nullptr && pLight != nullptr && pDoor != nullptr && pLogic != nullptr );
    SW_ASSERT_NOT_NULL( pPlate->addComponent<GimmickSensorComponent>() );
    pHeavy->addComponent<GimmickWeightComponent>()->setWeight( 60.0f );
    pLight->addComponent<GimmickWeightComponent>()->setWeight( 20.0f );
    pDoor->addComponent<SceneComponent>()->setLocalPosition( float3{ 5.0f, 1.0f, 0.0f } );
    GimmickCircuitComponent* pCircuit = pLogic->addComponent<GimmickCircuitComponent>();
    SW_ASSERT_NOT_NULL( pCircuit );
    pCircuit->addNode( "plate", "PressurePlate", "threshold=50", pPlate->getHandle() );
    pCircuit->addNode( "door", "Door", "openTime=0.1; closeTime=0.1; openOffset=0 3 0", pDoor->getHandle() );
    pCircuit->addWire( "plate.Pressed", "door.Open" );
    SW_ASSERT_TRUE( pCircuit->rebuild() );

    manager.beginPlay();
    GimmickSceneTestInternal::tickFrames( manager, 5 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f );

    pLight->getPrimarySceneComponent()->setLocalPosition( float3{ 0.5f, 0.0f, 0.0f } );
    GimmickSceneTestInternal::tickFrames( manager, 20 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f ); // 20 kg — 못 연다

    pHeavy->getPrimarySceneComponent()->setLocalPosition( float3{ -0.5f, 0.0f, 0.0f } );
    GimmickSceneTestInternal::tickFrames( manager, 20 );
    SW_EXPECT_EQUAL( 2, pPlate->getComponent<GimmickSensorComponent>()->getOccupantCount() );
    SW_EXPECT_NEAR_EQUAL( 4.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f ); // 80 kg — 3 m 열림
    SW_EXPECT_NEAR_EQUAL( 5.0f, pDoor->getPrimarySceneComponent()->getWorldPosition()._x, 1.0e-4f );

    pHeavy->getPrimarySceneComponent()->setLocalPosition( float3{ 30.0f, 0.0f, 0.0f } );
    GimmickSceneTestInternal::tickFrames( manager, 20 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f );
    manager.endPlay();
}

/**
 * @brief [GimmickSceneTest] 3D 무버 — 발판 하나(곡선 + 회로)가 자기 곡선을 따라 가고, 오브젝트 상태로 저장한 회로 상태를 새 오브젝트가 이어 받는다
 * @details 곡선은 플레이 시작의 변환으로 굳어 발판이 움직여도 길은 그대로다. 저장한 상태(`_stateBytes`)는 로드의 `onPostLoad` 가 회로에 되돌린다 —
 *          레벨 세이브 · 핫 리로드가 지나는 길이다.
 */
SW_TEST_CASE( GimmickSceneTest, MoverFollowsSplineAndStateSurvivesSaveLoad )
{
    GameObjectManager manager;
    GameObject*       pPlatform = manager.createGameObject( hashed_string( "Platform" ) );
    SW_ASSERT_NOT_NULL( pPlatform );
    pPlatform->addComponent<SceneComponent>()->setLocalPosition( float3{ 0.0f, 2.0f, 0.0f } );
    SplineComponent* pSpline = pPlatform->addComponent<SplineComponent>();
    SW_ASSERT_NOT_NULL( pSpline );
    manager.flushSceneTransforms();
    pSpline->setControlPoints( {
                                   float3{ 0.0f, 0.0f,  0.0f},
                                   float3{10.0f, 0.0f,  0.0f},
                                   float3{10.0f, 0.0f, 10.0f}
    },
                               SplineType::Linear, false );
    GimmickCircuitComponent* pCircuit = pPlatform->addComponent<GimmickCircuitComponent>();
    pCircuit->addNode( "move", "Mover", "speed=5; mode=PingPong" );
    SW_ASSERT_TRUE( pCircuit->rebuild() );

    manager.beginPlay();
    GimmickSceneTestInternal::tickFrames( manager, 60 ); // 1 초 — 5 m
    const float3 afterOneSecond = pPlatform->getPrimarySceneComponent()->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 5.0f, afterOneSecond._x, 0.1f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, afterOneSecond._y, 1.0e-4f ); // 곡선은 발판 시작 자리 기준
    GimmickSceneTestInternal::tickFrames( manager, 60 );      // 2 초 — 모퉁이를 돌아 (10, 2, 0)
    SW_EXPECT_NEAR_EQUAL( 10.0f, pPlatform->getPrimarySceneComponent()->getWorldPosition()._x, 0.1f );

    const uint64 savedHash = pCircuit->getCircuit().computeStateHash();
    const string xml       = ObjectStateSerializer::saveToXmlString( pPlatform );
    SW_ASSERT_FALSE( xml.empty() );

    GameObjectManager restoredManager;
    GameObject*       pRestored = restoredManager.createGameObject( hashed_string( "Platform" ) );
    SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pRestored, xml ) );
    GimmickCircuitComponent* pRestoredCircuit = pRestored->getComponent<GimmickCircuitComponent>();
    SW_ASSERT_NOT_NULL( pRestoredCircuit );
    SW_EXPECT_EQUAL( savedHash, pRestoredCircuit->getCircuit().computeStateHash() );

    // 체크포인트 — 잡고, 더 가고, 돌아오면 같은 해시와 같은 자리.
    pCircuit->captureCheckpoint();
    GimmickSceneTestInternal::tickFrames( manager, 30 );
    SW_EXPECT_NOT_EQUAL( savedHash, pCircuit->getCircuit().computeStateHash() );
    pCircuit->restoreCheckpoint();
    SW_EXPECT_EQUAL( savedHash, pCircuit->getCircuit().computeStateHash() );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 10.0f, pPlatform->getPrimarySceneComponent()->getWorldPosition()._x, 0.1f );
    manager.endPlay();
}

/**
 * @brief [GimmickSceneTest] 로드 때 검증 — 씬 데이터의 배선이 모르는 노드 · 출력을 가리키면 오류로 알리고 회로는 돌지 않는다
 */
SW_TEST_CASE( GimmickSceneTest, InvalidWiringIsRejectedAtLoad )
{
    GameObjectManager        manager;
    GameObject*              pLogic   = manager.createGameObject( hashed_string( "Logic" ) );
    GimmickCircuitComponent* pCircuit = pLogic->addComponent<GimmickCircuitComponent>();
    pCircuit->addNode( "button", "Interaction", "" );
    pCircuit->addNode( "door", "Door", "openTime=1" );
    pCircuit->addWire( "button.OnUsed", "gate.Open" );
    pCircuit->addWire( "button.OnPushed", "door.Open" );
    pCircuit->addNode( "lamp", "Light", "startOn" ); // = 가 없는 매개변수
    const string xml = ObjectStateSerializer::saveToXmlString( pLogic );

    GameObject* pLoaded = manager.createGameObject( hashed_string( "Loaded" ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "gimmick circuit with unknown target node and output" );
        SW_ASSERT_TRUE( ObjectStateSerializer::loadFromXmlString( pLoaded, xml ) );
    }
    const GimmickCircuitComponent* pLoadedCircuit = pLoaded->getComponent<GimmickCircuitComponent>();
    SW_ASSERT_NOT_NULL( pLoadedCircuit );
    SW_EXPECT_FALSE( pLoadedCircuit->getCircuit().isBuilt() );
    bool bUnknownTarget = false;
    bool bUnknownOutput = false;
    bool bBrokenParam   = false;
    for ( const string& error : pLoadedCircuit->getErrors() )
    {
        bUnknownTarget = bUnknownTarget || error.find( "unknown target node 'gate'" ) != string::npos;
        bUnknownOutput = bUnknownOutput || error.find( "unknown output 'OnPushed'" ) != string::npos;
        bBrokenParam   = bBrokenParam || error.find( "without name=value" ) != string::npos;
    }
    SW_EXPECT_TRUE( bUnknownTarget );
    SW_EXPECT_TRUE( bUnknownOutput );
    SW_EXPECT_TRUE( bBrokenParam );
}
