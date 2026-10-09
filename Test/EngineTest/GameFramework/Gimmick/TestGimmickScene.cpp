// 씬의 기믹 — 회로 컴포넌트가 센서 컴포넌트(겹침 · 무게)를 읽고 액추에이터(문 · 무버)를 오브젝트에 건다, 상태가 오브젝트 상태와 함께 저장된다, 로드 때 검증.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/AdventureGimmicks.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/World/Spline/SplineComponent.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

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
    test::tickFrames( manager, 5 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f );

    pLight->getPrimarySceneComponent()->setLocalPosition( float3{ 0.5f, 0.0f, 0.0f } );
    test::tickFrames( manager, 20 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f ); // 20 kg — 못 연다

    pHeavy->getPrimarySceneComponent()->setLocalPosition( float3{ -0.5f, 0.0f, 0.0f } );
    test::tickFrames( manager, 20 );
    SW_EXPECT_EQUAL( 2, pPlate->getComponent<GimmickSensorComponent>()->getOccupantCount() );
    SW_EXPECT_NEAR_EQUAL( 4.0f, GimmickSceneTestInternal::getY( pDoor ), 1.0e-4f ); // 80 kg — 3 m 열림
    SW_EXPECT_NEAR_EQUAL( 5.0f, pDoor->getPrimarySceneComponent()->getWorldPosition()._x, 1.0e-4f );

    pHeavy->getPrimarySceneComponent()->setLocalPosition( float3{ 30.0f, 0.0f, 0.0f } );
    test::tickFrames( manager, 20 );
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
    test::tickFrames( manager, 60 ); // 1 초 — 5 m
    const float3 afterOneSecond = pPlatform->getPrimarySceneComponent()->getWorldPosition();
    SW_EXPECT_NEAR_EQUAL( 5.0f, afterOneSecond._x, 0.1f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, afterOneSecond._y, 1.0e-4f ); // 곡선은 발판 시작 자리 기준
    test::tickFrames( manager, 60 );                          // 2 초 — 모퉁이를 돌아 (10, 2, 0)
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
    test::tickFrames( manager, 30 );
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

/**
 * @brief [GimmickSceneTest] 쇼케이스 씬(game/empty/maps/gimmickshowcase.scene.xml) — 레벨 회로의 노드 대상(파일 엔티티 id)이 이 실행의 오브젝트로 옮겨지고,
 *        시계 → 토글이 문과 엘리베이터를 움직이며, 처음부터 불붙은 횃불이 Signal 센서로 들어간다
 */
SW_TEST_CASE( GimmickSceneTest, ShowcaseSceneWiresAcrossObjects )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXml( "game/empty/maps/gimmickshowcase.scene.xml" ) );
    Scene scene{ "GimmickShowcase" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    GameObjectManager& manager = *scene.getObjectManager();
    GameObject*        pLogic  = manager.findGameObjectByName( "ShowcaseLogic" );
    GameObject*        pGate   = manager.findGameObjectByName( "TorchGate" );
    GameObject*        pLift   = manager.findGameObjectByName( "LiftPlatform" );
    GameObject*        pTorch  = manager.findGameObjectByName( "TorchA" );
    SW_ASSERT_TRUE( pLogic != nullptr && pGate != nullptr && pLift != nullptr && pTorch != nullptr );
    GimmickCircuitComponent* pCircuit = pLogic->getComponent<GimmickCircuitComponent>();
    SW_ASSERT_NOT_NULL( pCircuit );
    SW_EXPECT_TRUE( pCircuit->getCircuit().isBuilt() );

    manager.beginPlay();
    const float32 gateY = GimmickSceneTestInternal::getY( pGate );
    const float32 liftY = GimmickSceneTestInternal::getY( pLift );
    test::tickFrames( manager, 60 * 3 + 30 ); // 2 초에 토글 — 문 0.8 초 · 엘리베이터 2 초
    SW_EXPECT_NEAR_EQUAL( gateY + 3.0f, GimmickSceneTestInternal::getY( pGate ), 1.0e-3f );
    SW_EXPECT_TRUE( GimmickSceneTestInternal::getY( pLift ) > liftY + 1.5f );
    SW_EXPECT_TRUE( pTorch->getComponent<ElementStatusComponent>()->hasStatus( "Burning" ) );
    SW_EXPECT_TRUE( pCircuit->getCircuit().getOutput( pCircuit->findNode( "torchA" ), hashed_string( "Active" ) ) );
    SW_EXPECT_FALSE( pCircuit->getCircuit().getOutput( pCircuit->findNode( "bothLit" ), hashed_string( "Out" ) ) );
    manager.endPlay();
}

/**
 * @brief [GimmickSceneTest] 레이저 · 근접 센서와 위험 지대 · 켜기 액추에이터 — 상자가 광선을 막으면 Blocked, 태그 대상이 반경에 들면 Near(2D 평면 거리),
 *        위험 지대는 켜진 동안 겹친 것의 센서에 피해를 주고, Enable 은 대상 오브젝트를 끈다
 */
SW_TEST_CASE( GimmickSceneTest, LaserProximityHazardEnable )
{
    GameObjectManager manager;
    GameObject*       pEmitter = manager.createGameObject( hashed_string( "LaserEmitter" ) );
    pEmitter->addComponent<SceneComponent>();
    GameObject* pBlocker = GimmickSceneTestInternal::spawnBox( manager, "Blocker", float3{ 0.0f, 20.0f, 0.0f }, false );
    GameObject* pSeeker  = manager.createGameObject( hashed_string( "Seeker" ) );
    pSeeker->addComponent<SceneComponent>()->setLocalPosition( float3{ 0.0f, -40.0f, 30.0f } );
    pSeeker->addTag( TagID::request( "Player" ) );
    GameObject* pZone = GimmickSceneTestInternal::spawnBox( manager, "Zone", float3{ 40.0f, 0.0f, 0.0f }, true );
    pZone->addComponent<GimmickSensorComponent>();
    GameObject* pVictim = GimmickSceneTestInternal::spawnBox( manager, "Victim", float3{ 40.5f, 0.0f, 0.0f }, false );
    pVictim->addComponent<GimmickSensorComponent>();
    GameObject* pLamp = manager.createGameObject( hashed_string( "Lamp" ) );
    pLamp->addComponent<SceneComponent>();

    GameObject*              pLogic   = manager.createGameObject( hashed_string( "Logic" ) );
    GimmickCircuitComponent* pCircuit = pLogic->addComponent<GimmickCircuitComponent>();
    pCircuit->addNode( "beam", "Laser", "range=10; direction=1 0 0", pEmitter->getHandle() );
    pCircuit->addNode( "near", "Proximity", "radius=5; tag=Player; planar=1", pEmitter->getHandle() );
    pCircuit->addNode( "hazard", "Hazard", "onTime=1; offTime=1; damage=7; damageInterval=0.5", pZone->getHandle() );
    pCircuit->addNode( "lamp", "Enable", "", pLamp->getHandle() );
    pCircuit->addWire( "beam.Blocked", "lamp.Enable", true );
    SW_ASSERT_TRUE( pCircuit->rebuild() );
    const int32 beam      = pCircuit->findNode( "beam" );
    const int32 proximity = pCircuit->findNode( "near" );

    manager.beginPlay();
    test::tickFrames( manager, 3 );
    SW_EXPECT_FALSE( pCircuit->getCircuit().getOutput( beam, hashed_string( "Blocked" ) ) );
    SW_EXPECT_FALSE( pCircuit->getCircuit().getOutput( proximity, hashed_string( "Near" ) ) );
    SW_EXPECT_TRUE( pLamp->isActive() );

    pBlocker->getPrimarySceneComponent()->setLocalPosition( float3{ 5.0f, 0.0f, 0.0f } ); // 광선 위
    pSeeker->getPrimarySceneComponent()->setLocalPosition( float3{ 3.0f, 0.0f, 30.0f } ); // XY 거리 3(Z 는 그리기 순서)
    test::tickFrames( manager, 4 );
    SW_EXPECT_TRUE( pCircuit->getCircuit().getOutput( beam, hashed_string( "Blocked" ) ) );
    SW_EXPECT_TRUE( pCircuit->getCircuit().getOutput( proximity, hashed_string( "Near" ) ) );
    SW_EXPECT_FALSE( pLamp->isActive() ); // 광선이 막히면 끈다

    // 위험 지대 — 1 초 켜짐 · 1 초 꺼짐, 켜진 동안 0.5 초마다 7(켜지는 걸음 포함). 걸음 7..126 에는 30 · 120 걸음의 두 번이다
    // (첫 켜짐 0 걸음에는 아직 겹친 것이 없다 — 겹침은 첫 물리 step 뒤에 온다).
    (void)pVictim->getComponent<GimmickSensorComponent>()->consumeDamage();
    test::tickFrames( manager, 120 );
    SW_EXPECT_NEAR_EQUAL( 14.0f, pVictim->getComponent<GimmickSensorComponent>()->consumeDamage(), 1.0e-3f );
    manager.endPlay();
}
