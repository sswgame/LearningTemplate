// 장르 기믹 — 무너지는 발판 · 발사대 · 컨베이어 · 미는 블록 · 횃불 퍼즐 · 폭발 사슬 · 포탑 · 아이템 상자 · 깜빡이는 빛 · 숨는 곳 · 소리 · 빛 노출 ·
// 능력 문 · 채집 지점, 그리고 공용 기믹 프리팹이 모두 스폰되고 배선이 검증을 지난다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Base/Gimmick/Genre/AdventureGimmicks.h"
#include "GameFramework/Base/Gimmick/Genre/HorrorStealthGimmicks.h"
#include "GameFramework/Base/Gimmick/Genre/PlatformerGimmicks.h"
#include "GameFramework/Base/Gimmick/Genre/ProgressionGimmicks.h"
#include "GameFramework/Base/Gimmick/Genre/RacingGimmicks.h"
#include "GameFramework/Base/Gimmick/Genre/ShooterGimmicks.h"
#include "GameFramework/Base/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Base/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/Interaction/InteractableComponent.h"
#include "GameFramework/Base/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Interaction/SmartObjectComponent.h"
#include "GameFramework/Base/World/GravityComponent.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct GimmickGenreTestInternal
    {
        static GameObject* spawnAt( GameObjectManager& manager, const utf8* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            pObject->addComponent<SceneComponent>()->setLocalPosition( position );
            return pObject;
        }

        static GameObject* spawnBox( GameObjectManager& manager, const utf8* pName, const float3& position, const float2& size, bool bTrigger )
        {
            GameObject*             pObject = manager.createGameObject( hashed_string( pName ) );
            BoxCollider2DComponent* pBox    = pObject->addComponent<BoxCollider2DComponent>();
            pBox->setOffsetScale( size );
            pBox->setTrigger( bTrigger );
            pBox->setLocalPosition( position );
            return pObject;
        }

        static void tickFrames( GameObjectManager& manager, int32 frameCount )
        {
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                manager.tick( 1.0f / 60.0f );
            }
        }

        static float3 getPosition( const GameObject* pObject ) { return pObject->getPrimarySceneComponent()->getWorldPosition(); }

        static bool isBodyActive( const GameObject* pObject )
        {
            for ( const Component* pComp : pObject->getComponents() )
            {
                if ( pComp->isSceneComponent() && pComp->isActive() )
                    return true;
            }
            return false;
        }
    };
} // namespace

/**
 * @brief [GimmickGenreTest] 플랫포머 — 무너지는 발판은 올라서면 정해진 걸음 뒤에 몸이 꺼지고 되살아나며, 발사대는 겹친 것의 수직 속도를 바꾸고, 컨베이어는 올라선 것을 옮긴다
 */
SW_TEST_CASE( GimmickGenreTest, PlatformerCrumbleLaunchConveyor )
{
    using Internal = GimmickGenreTestInternal;
    GameObjectManager manager;
    GameObject*       pPlatform = Internal::spawnBox( manager, "Crumble", float3{ 0.0f, 0.0f, 0.0f }, float2{ 2.0f, 1.0f }, false );
    pPlatform->addComponent<GimmickSensorComponent>();
    CrumblePlatformComponent* pCrumble = pPlatform->addComponent<CrumblePlatformComponent>();
    pCrumble->setDelays( 0.5f, 1.0f );
    GameObject*       pPlayer  = Internal::spawnBox( manager, "Player", float3{ 20.0f, 0.0f, 0.0f }, float2{ 1.0f, 1.0f }, false );
    GravityComponent* pGravity = pPlayer->addComponent<GravityComponent>();
    pGravity->setGroundY( -100.0f );
    pGravity->setGravity( 0.0f );

    GameObject* pSpring = Internal::spawnBox( manager, "Spring", float3{ 40.0f, 0.0f, 0.0f }, float2{ 1.0f, 1.0f }, true );
    pSpring->addComponent<LaunchPadComponent>();
    GameObject* pBelt = Internal::spawnBox( manager, "Belt", float3{ 60.0f, 0.0f, 0.0f }, float2{ 4.0f, 1.0f }, false );
    pBelt->addComponent<GimmickSensorComponent>();
    pBelt->addComponent<ConveyorComponent>();

    manager.beginPlay();
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_TRUE( pCrumble->getState() == CrumbleState::Solid );
    pPlayer->getPrimarySceneComponent()->setLocalPosition( float3{ 0.5f, 0.5f, 0.0f } );
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_TRUE( pCrumble->getState() == CrumbleState::Shaking );
    Internal::tickFrames( manager, 31 );
    SW_EXPECT_TRUE( pCrumble->getState() == CrumbleState::Fallen );
    SW_EXPECT_FALSE( Internal::isBodyActive( pPlatform ) );
    Internal::tickFrames( manager, 61 );
    SW_EXPECT_TRUE( pCrumble->getState() != CrumbleState::Fallen ); // 1 초 뒤 되살아났다
    SW_EXPECT_TRUE( Internal::isBodyActive( pPlatform ) );

    pPlayer->getPrimarySceneComponent()->setLocalPosition( float3{ 40.0f, 0.0f, 0.0f } );
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_EQUAL( 1, pSpring->getComponent<LaunchPadComponent>()->getLaunchCount() );
    SW_EXPECT_NEAR_EQUAL( 12.0f, pGravity->getVelocityY(), 1.0e-3f );

    pGravity->jump( 0.0f );
    pPlayer->getPrimarySceneComponent()->setLocalPosition( float3{ 60.0f, 0.0f, 0.0f } );
    Internal::tickFrames( manager, 2 );
    const float32 before = Internal::getPosition( pPlayer )._x;
    Internal::tickFrames( manager, 30 );
    SW_EXPECT_NEAR_EQUAL( before + 1.0f, Internal::getPosition( pPlayer )._x, 0.1f ); // 2 m/s × 0.5 초
    manager.endPlay();
}

/**
 * @brief [GimmickGenreTest] 어드벤처 — 미는 블록은 빈 칸으로 정확히 한 칸 가고 벽 앞에서는 서며, 횃불 둘에 불을 붙이면 Signal → And 회로의 문이 열린다(물로 끄면 닫힌다)
 */
SW_TEST_CASE( GimmickGenreTest, PushBlockAndTorchPuzzle )
{
    using Internal = GimmickGenreTestInternal;
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager   manager;
    GameObject*         pBlock = Internal::spawnBox( manager, "Block", float3{ 0.0f, 0.0f, 0.0f }, float2{ 0.9f, 0.9f }, false );
    PushBlockComponent* pPush  = pBlock->addComponent<PushBlockComponent>();
    Internal::spawnBox( manager, "Wall", float3{ -1.2f, 0.0f, 0.0f }, float2{ 1.0f, 1.0f }, false );

    GameObject* pTorchA = Internal::spawnAt( manager, "TorchA", float3{ 10.0f, 0.0f, 0.0f } );
    GameObject* pTorchB = Internal::spawnAt( manager, "TorchB", float3{ 12.0f, 0.0f, 0.0f } );
    for ( GameObject* pTorch : { pTorchA, pTorchB } )
    {
        pTorch->addComponent<GimmickSensorComponent>();
        pTorch->addComponent<ElementStatusComponent>()->setMaterial( "Torch" );
    }
    GameObject*              pDoor    = Internal::spawnAt( manager, "Door", float3{ 11.0f, 0.0f, 5.0f } );
    GameObject*              pLogic   = manager.createGameObject( hashed_string( "Puzzle" ) );
    GimmickCircuitComponent* pCircuit = pLogic->addComponent<GimmickCircuitComponent>();
    pCircuit->addNode( "a", "Signal", "", pTorchA->getHandle() );
    pCircuit->addNode( "b", "Signal", "", pTorchB->getHandle() );
    pCircuit->addNode( "both", "And", "" );
    pCircuit->addNode( "door", "Door", "openTime=0.1; closeTime=0.1; openOffset=0 4 0", pDoor->getHandle() );
    pCircuit->addWire( "a.Active", "both.A" );
    pCircuit->addWire( "b.Active", "both.B" );
    pCircuit->addWire( "both.Out", "door.Open" );
    SW_ASSERT_TRUE( pCircuit->rebuild() );

    manager.beginPlay();
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_TRUE( pPush->push( float3{ 3.0f, 0.0f, 0.4f } ) );  // X 축으로 맞춘다
    SW_EXPECT_FALSE( pPush->push( float3{ 1.0f, 0.0f, 0.0f } ) ); // 움직이는 중
    Internal::tickFrames( manager, 20 );
    SW_EXPECT_NEAR_EQUAL( 1.0f, Internal::getPosition( pBlock )._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::getPosition( pBlock )._z, 1.0e-4f );
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_TRUE( pPush->push( float3{ -1.0f, 0.0f, 0.0f } ) );
    Internal::tickFrames( manager, 20 );
    SW_EXPECT_FALSE( pPush->push( float3{ -1.0f, 0.0f, 0.0f } ) ); // 벽 앞 칸에서 막힌다
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::getPosition( pBlock )._x, 1.0e-4f );

    SW_EXPECT_TRUE( pTorchA->getComponent<ElementStatusComponent>()->applyStimulus( "Fire" ) );
    Internal::tickFrames( manager, 10 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::getPosition( pDoor )._y, 1.0e-4f ); // 하나만 켜졌다
    SW_EXPECT_TRUE( pTorchB->getComponent<ElementStatusComponent>()->applyStimulus( "Fire" ) );
    Internal::tickFrames( manager, 10 );
    SW_EXPECT_NEAR_EQUAL( 4.0f, Internal::getPosition( pDoor )._y, 1.0e-4f );
    Internal::tickFrames( manager, 120 );
    SW_EXPECT_TRUE( pTorchA->getComponent<ElementStatusComponent>()->hasStatus( "Burning" ) ); // 횃불은 다 타지 않는다
    SW_EXPECT_TRUE( pTorchA->getComponent<ElementStatusComponent>()->applyStimulus( "Water" ) );
    Internal::tickFrames( manager, 10 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::getPosition( pDoor )._y, 1.0e-4f );
    manager.endPlay();
}

/**
 * @brief [GimmickGenreTest] 슈터 — 드럼통은 피해가 체력을 넘으면 터져 반경 안 드럼통을 다음 걸음에 터뜨리고(사슬, 반경 밖은 그대로), 포탑은 보이는 가장 가까운 태그 대상을
 *        향해 돌아 쏘며 벽 뒤는 쏘지 않는다, 엄폐물은 단계를 지나 부서진다
 */
SW_TEST_CASE( GimmickGenreTest, ShooterBarrelsTurretCover )
{
    using Internal = GimmickGenreTestInternal;
    GameObjectManager manager;
    GameObject*       arrBarrel[3] = {
        Internal::spawnAt( manager, "BarrelA", float3{ 0.0f, 0.0f, 0.0f } ),
        Internal::spawnAt( manager, "BarrelB", float3{ 1.5f, 0.0f, 0.0f } ),
        Internal::spawnAt( manager, "BarrelFar", float3{ 30.0f, 0.0f, 0.0f } ),
    };
    for ( GameObject* pBarrel : arrBarrel )
    {
        pBarrel->addComponent<GimmickSensorComponent>();
        pBarrel->addComponent<ExplosiveBarrelComponent>();
    }
    manager.beginPlay();
    arrBarrel[0]->getComponent<GimmickSensorComponent>()->applyDamage( 40.0f );
    Internal::tickFrames( manager, 1 );
    SW_EXPECT_TRUE( arrBarrel[0]->getComponent<ExplosiveBarrelComponent>()->hasExploded() );
    SW_EXPECT_FALSE( arrBarrel[1]->getComponent<ExplosiveBarrelComponent>()->hasExploded() ); // 사슬은 다음 걸음
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_TRUE( arrBarrel[1]->getComponent<ExplosiveBarrelComponent>()->hasExploded() );
    SW_EXPECT_FALSE( arrBarrel[2]->getComponent<ExplosiveBarrelComponent>()->hasExploded() );

    GameObject*      pTurret      = Internal::spawnAt( manager, "Turret", float3{ 100.0f, 0.0f, 0.0f } );
    TurretComponent* pTurretLogic = pTurret->addComponent<TurretComponent>();
    GameObject*      pNear        = Internal::spawnBox( manager, "NearPlayer", float3{ 104.0f, 0.0f, 0.0f }, float2{ 0.5f, 0.5f }, false );
    GameObject*      pFar         = Internal::spawnBox( manager, "FarPlayer", float3{ 100.0f, 0.0f, 8.0f }, float2{ 0.5f, 0.5f }, false );
    for ( GameObject* pTarget : { pNear, pFar } )
    {
        pTarget->addTag( TagID::request( "Player" ) );
        pTarget->addComponent<GimmickSensorComponent>();
    }
    Internal::spawnBox( manager, "Wall", float3{ 102.0f, 0.0f, 0.0f }, float2{ 0.5f, 4.0f }, false ); // 가까운 쪽을 가린다
    {
        // 표적 태그는 프리팹이 Player 로 둔다 — 시험은 같은 값을 리플렉션으로 넣는다.
        const PropertyInfo* pTags = pTurretLogic->getTypeInfo()->findProperty( "_targetTags" );
        SW_ASSERT_NOT_NULL( pTags );
        pTags->getValuePtr<TagContainer>( static_cast<void*>( pTurretLogic ) )->addTag( TagID::request( "Player" ) );
    }
    Internal::tickFrames( manager, 120 );
    SW_EXPECT_TRUE( pTurretLogic->getTarget() == pFar->getHandle() ); // 가려진 가까운 쪽 대신 보이는 쪽
    SW_EXPECT_TRUE( pTurretLogic->getShotCount() >= 2 );
    SW_EXPECT_TRUE( pFar->getComponent<GimmickSensorComponent>()->consumeDamage() > 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pTurret->getPrimarySceneComponent()->getLocalRotation()._y, 0.1f ); // +Z 를 본다

    GameObject* pCover = Internal::spawnBox( manager, "Cover", float3{ 200.0f, 0.0f, 0.0f }, float2{ 2.0f, 1.0f }, false );
    pCover->addComponent<GimmickSensorComponent>();
    DestructibleComponent* pDestructible = pCover->addComponent<DestructibleComponent>();
    pCover->getComponent<GimmickSensorComponent>()->applyDamage( 25.0f );
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_EQUAL( 1, pDestructible->getStage() );
    pCover->getComponent<GimmickSensorComponent>()->applyDamage( 30.0f );
    Internal::tickFrames( manager, 2 );
    SW_EXPECT_TRUE( pDestructible->isDestroyed() );
    SW_EXPECT_FALSE( Internal::isBodyActive( pCover ) );
    manager.endPlay();
}

/**
 * @brief [GimmickGenreTest] 레이싱 · 공포 · 잠입 — 아이템 상자는 같은 씨앗 · 같은 순서면 같은 아이템이고 숨었다 되살아나며, 깜빡이는 빛은 무늬 글자대로, 숨는 곳은 숨음 태그를,
 *        삐걱이는 마루는 올라설 때 소리를 내고, 빛 노출은 거리 · 가림을 따른다
 */
SW_TEST_CASE( GimmickGenreTest, RacingHorrorStealth )
{
    using Internal = GimmickGenreTestInternal;
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    GameObjectManager         manager;
    vector<GimmickItemChoice> listChoice;
    for ( const utf8* pItem : { "Banana", "Shell", "Mushroom" } )
    {
        GimmickItemChoice choice;
        choice._item = hashed_string( pItem );
        listChoice.push_back( choice );
    }
    GameObject* pBoxA = Internal::spawnAt( manager, "BoxA", float3{} );
    GameObject* pBoxB = Internal::spawnAt( manager, "BoxB", float3{ 5.0f, 0.0f, 0.0f } );
    pBoxA->addComponent<ItemBoxComponent>()->setChoices( listChoice );
    pBoxB->addComponent<ItemBoxComponent>()->setChoices( listChoice );
    GameObject* pRacer = Internal::spawnAt( manager, "Racer", float3{} );
    manager.beginPlay();
    vector<hashed_string> listA;
    vector<hashed_string> listB;
    for ( int32 round = 0; round < 5; ++round )
    {
        listA.push_back( pBoxA->getComponent<ItemBoxComponent>()->open( *pRacer ) );
        listB.push_back( pBoxB->getComponent<ItemBoxComponent>()->open( *pRacer ) );
        SW_EXPECT_FALSE( pBoxA->getComponent<ItemBoxComponent>()->isAvailable() );
        Internal::tickFrames( manager, 181 ); // 3 초 뒤 되살아난다
        SW_EXPECT_TRUE( pBoxA->getComponent<ItemBoxComponent>()->isAvailable() );
    }
    bool bVaried = false;
    for ( size_t index = 0; index < listA.size(); ++index )
    {
        SW_EXPECT_TRUE( listA[index] == listB[index] && listA[index].empty() == false );
        bVaried = bVaried || listA[index] != listA[0];
    }
    SW_EXPECT_TRUE( bVaried ); // 연 횟수가 뽑기를 바꾼다(늘 같은 것이 아니다)

    GameObject*            pLamp    = Internal::spawnAt( manager, "Lamp", float3{ 50.0f, 0.0f, 0.0f } );
    FlickerLightComponent* pFlicker = pLamp->addComponent<FlickerLightComponent>();
    SW_EXPECT_NEAR_EQUAL( 1.0f, pFlicker->computeScaleAtStep( 0 ), 1.0e-4f );   // 'm'
    SW_EXPECT_NEAR_EQUAL( 0.0f, pFlicker->computeScaleAtStep( 13 ), 1.0e-4f );  // 0.22 초 → 2 번째 글자 'a'
    SW_EXPECT_NEAR_EQUAL( 1.0f, pFlicker->computeScaleAtStep( 151 ), 1.0e-4f ); // 2.52 초 → 25 글자 = 한 바퀴 돌아 처음

    GameObject*               pCloset  = Internal::spawnAt( manager, "Closet", float3{ 60.0f, 0.0f, 0.0f } );
    const InteractionCatalog* pCatalog = InteractionCatalog::findShared( InteractionCatalog::kDefaultPath );
    SW_ASSERT_NOT_NULL( pCatalog );
    pCloset->addComponent<SmartObjectComponent>()->setDefinition( *pCatalog->findSmartObject( "HidingSpot" ) );
    HidingSpotComponent* pHiding = pCloset->addComponent<HidingSpotComponent>();
    GameObject*          pOther  = Internal::spawnAt( manager, "Other", float3{} );
    SW_EXPECT_TRUE( pHiding->enter( *pRacer ) );
    SW_EXPECT_TRUE( HidingSpotComponent::isHidden( *pRacer ) );
    SW_EXPECT_FALSE( pHiding->enter( *pOther ) ); // 자리는 하나
    SW_EXPECT_TRUE( pHiding->exit( *pRacer ) );
    SW_EXPECT_FALSE( HidingSpotComponent::isHidden( *pRacer ) );

    GameObject* pFloor = Internal::spawnBox( manager, "Floor", float3{ 80.0f, 0.0f, 0.0f }, float2{ 2.0f, 1.0f }, true );
    pFloor->addComponent<GimmickSensorComponent>();
    NoiseEmitterComponent* pNoise  = pFloor->addComponent<NoiseEmitterComponent>();
    GameObject*            pWalker = Internal::spawnBox( manager, "Walker", float3{ 90.0f, 0.0f, 0.0f }, float2{ 0.5f, 0.5f }, false );
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_EQUAL( 0, pNoise->getEmitCount() );
    pWalker->getPrimarySceneComponent()->setLocalPosition( float3{ 80.0f, 0.0f, 0.0f } );
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_EQUAL( 1, pNoise->getEmitCount() );

    GameObject*          pBulb  = Internal::spawnAt( manager, "Bulb", float3{ 0.0f, 0.0f, 200.0f } );
    PointLightComponent* pPoint = pBulb->addComponent<PointLightComponent>();
    manager.flushSceneTransforms();
    const float32 nearLight = LightExposure::computeExposure( manager, float3{ 1.0f, 0.0f, 200.0f }, false );
    const float32 farLight  = LightExposure::computeExposure( manager, float3{ 4.0f, 0.0f, 200.0f }, false );
    SW_EXPECT_TRUE( nearLight > farLight );
    SW_EXPECT_TRUE( farLight > 0.0f );
    SW_EXPECT_EQUAL( 0.0f, LightExposure::computeExposure( manager, float3{ 0.0f, 0.0f, 260.0f }, false ) ); // 반경 밖은 어둠
    (void)pPoint;
    Internal::spawnBox( manager, "Shade", float3{ 0.5f, 0.0f, 200.0f }, float2{ 0.2f, 3.0f }, false );
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_EQUAL( 0.0f, LightExposure::computeExposure( manager, float3{ 1.0f, 0.0f, 200.0f }, true ) ); // 가려졌다
    manager.endPlay();
}

/**
 * @brief [GimmickGenreTest] 진행 — 능력 태그가 있는 것이 다가오면 능력 문이 열려 남고(없는 것은 못 연다), 채집 지점은 정해진 횟수 뒤 다 써서 꺼졌다가 되살아난다
 */
SW_TEST_CASE( GimmickGenreTest, AbilityGateAndGatheringNode )
{
    using Internal = GimmickGenreTestInternal;
    GameObjectManager     manager;
    GameObject*           pGate      = Internal::spawnBox( manager, "Gate", float3{}, float2{ 1.0f, 3.0f }, false );
    AbilityGateComponent* pGateLogic = pGate->addComponent<AbilityGateComponent>();
    {
        const PropertyInfo* pTags = pGateLogic->getTypeInfo()->findProperty( "_requiredTags" );
        SW_ASSERT_NOT_NULL( pTags );
        pTags->getValuePtr<TagContainer>( static_cast<void*>( pGateLogic ) )->addTag( TagID::request( "Ability.Bomb" ) );
    }
    GameObject*             pHero   = Internal::spawnAt( manager, "Hero", float3{ 1.5f, 0.0f, 0.0f } );
    GameObject*             pNode   = Internal::spawnAt( manager, "Herbs", float3{ 50.0f, 0.0f, 0.0f } );
    GatheringNodeComponent* pGather = pNode->addComponent<GatheringNodeComponent>();
    manager.beginPlay();
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_FALSE( pGateLogic->isOpen() ); // 능력이 없다
    pHero->addTag( TagID::request( "Ability.Bomb" ) );
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_TRUE( pGateLogic->isOpen() );
    SW_EXPECT_FALSE( Internal::isBodyActive( pGate ) );
    pHero->getPrimarySceneComponent()->setLocalPosition( float3{ 30.0f, 0.0f, 0.0f } );
    Internal::tickFrames( manager, 3 );
    SW_EXPECT_TRUE( pGateLogic->isOpen() ); // 한 번 열리면 남는다

    for ( int32 use = 0; use < 3; ++use )
    {
        SW_EXPECT_TRUE( pGather->gather( *pHero ) );
    }
    SW_EXPECT_TRUE( pGather->isDepleted() );
    SW_EXPECT_FALSE( pGather->gather( *pHero ) );
    Internal::tickFrames( manager, 30 * 60 + 2 );
    SW_EXPECT_FALSE( pGather->isDepleted() );
    SW_EXPECT_EQUAL( 3, pGather->getUsesLeft() );
    manager.endPlay();
}

/**
 * @brief [GimmickGenreTest] 공용 기믹 프리팹(Resource/common/prefabs/gimmicks)이 모두 스폰되고, 든 회로는 배선 검증을 오류 없이 지난다
 */
SW_TEST_CASE( GimmickGenreTest, EveryGimmickPrefabSpawnsWithValidWiring )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string   folder = FileUtil::joinPath( ResourceUtil::getRootFolderPath(), "common/prefabs/gimmicks" );
    vector<string> listFile;
    SW_ASSERT_TRUE( FileUtil::collectFiles( folder, ".xml", listFile, true ) );
    GameObjectManager manager;
    int32             prefabCount  = 0;
    int32             circuitCount = 0;
    for ( const string& filePath : listFile )
    {
        const string resourceId = ResourceUtil::toResourceId( filePath );
        PrefabAsset  prefab;
        SW_EXPECT_TRUE_MSG( prefab.loadFromXmlFile( resourceId ), resourceId.c_str() );
        GameObject* pObject = manager.createGameObject( hashed_string( "Gimmick" ) );
        SW_EXPECT_TRUE_MSG( prefab.applyStateTo( pObject ), resourceId.c_str() );
        ++prefabCount;
        const GimmickCircuitComponent* pCircuit = pObject->getComponent<GimmickCircuitComponent>();
        if ( pCircuit == nullptr )
            continue;
        ++circuitCount;
        SW_EXPECT_TRUE_MSG( pCircuit->getCircuit().isBuilt() && pCircuit->getErrors().empty(), resourceId.c_str() );
    }
    SW_EXPECT_TRUE( prefabCount >= 25 );
    SW_EXPECT_TRUE( circuitCount >= 6 );
}
