#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 컴포넌트 틱 — 틱 그룹 순서 · 서브틱 하이브리드 · 기본값 적용 시점.

// ------------------------------------------------------------------------------
// 8) ComponentTickGroupTest — PrePhysics~PostUpdate 순서 및 상속 계층 연동 검증
// ------------------------------------------------------------------------------
/**
 * @brief [ComponentTickGroupTest] 다단계 상속 계층 컴포넌트들의 4단계 TickGroup(PrePhysics~PostUpdate) 시간순 실행 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, MultiLevelInheritanceTickGroupChronologicalSequence )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::vector<sw::string> listTickOrder;

    // 역순(PostUpdate -> PostPhysics -> DuringPhysics -> PrePhysics)으로 액터 및 컴포넌트를 생성하여
    // 생성 순서와 무관하게 TickGroup 순서대로 틱이 정렬·디스패치되는지 검증
    sw::GameObject*                 pActorPostUpdate = manager.createGameObject( sw::hashed_string( "Actor_PostUpdate" ) );
    sw::MockFlyingVehicleComponent* pCompFlying      = pActorPostUpdate->addComponent<sw::MockFlyingVehicleComponent>();
    pCompFlying->setTickGroup( sw::TickGroup::PostUpdate );
    pCompFlying->_pTickOrderLog = &listTickOrder;
    pCompFlying->_componentTag  = "3_PostUpdate_Flying";

    sw::GameObject*           pActorPostPhysics = manager.createGameObject( sw::hashed_string( "Actor_PostPhysics" ) );
    sw::MockVehicleComponent* pCompVehicle      = pActorPostPhysics->addComponent<sw::MockVehicleComponent>();
    pCompVehicle->setTickGroup( sw::TickGroup::PostPhysics );
    pCompVehicle->_pTickOrderLog = &listTickOrder;
    pCompVehicle->_componentTag  = "2_PostPhysics_Vehicle";

    sw::GameObject*            pActorDuringPhysics = manager.createGameObject( sw::hashed_string( "Actor_DuringPhysics" ) );
    sw::MockBasePawnComponent* pCompPawn           = pActorDuringPhysics->addComponent<sw::MockBasePawnComponent>();
    pCompPawn->setTickGroup( sw::TickGroup::DuringPhysics );
    pCompPawn->_pTickOrderLog = &listTickOrder;
    pCompPawn->_componentTag  = "1_DuringPhysics_Pawn";

    sw::GameObject*        pActorPrePhysics = manager.createGameObject( sw::hashed_string( "Actor_PrePhysics" ) );
    sw::MockRootComponent* pCompRoot        = pActorPrePhysics->addComponent<sw::MockRootComponent>();
    pCompRoot->setTickGroup( sw::TickGroup::PrePhysics );
    pCompRoot->_pTickOrderLog = &listTickOrder;
    pCompRoot->_componentTag  = "0_PrePhysics_Root";

    // 틱 1회 실행
    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listTickOrder.size() );
    if ( listTickOrder.size() == 4 )
    {
        SW_EXPECT_EQUAL( "0_PrePhysics_Root", listTickOrder[0] );
        SW_EXPECT_EQUAL( "1_DuringPhysics_Pawn", listTickOrder[1] );
        SW_EXPECT_EQUAL( "2_PostPhysics_Vehicle", listTickOrder[2] );
        SW_EXPECT_EQUAL( "3_PostUpdate_Flying", listTickOrder[3] );
    }
}

/**
 * @brief [ComponentTickGroupTest] 부모-자식 트리 계층에서 서로 다른 TickGroup을 가진 컴포넌트들의 프레임 내 단방향 데이터 파이프라인 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, ParentChildHierarchyHeterogeneousTickGroupDataPipeline )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    // 1) 루트 부모 (PrePhysics 단계에서 기본 체력 연산)
    // 계층은 씬 컴포넌트 사이에서 맺어진다 — 씬 컴포넌트 없이 붙이면 늘 실패한다(결과를 버리면 모른다).
    sw::GameObject* pParentObj = manager.createGameObject( sw::hashed_string( "PipelineParent" ) );
    SW_ASSERT_NOT_NULL( pParentObj->addComponent<sw::SceneComponent>() );
    sw::MockBasePawnComponent* pPawn = pParentObj->addComponent<sw::MockBasePawnComponent>();
    pPawn->setTickGroup( sw::TickGroup::PrePhysics );
    pPawn->_pawnHealth = 100;

    // 2) 자식 (DuringPhysics 단계에서 부모 체력에 기반하여 최대 속도 산출)
    sw::GameObject* pChildObj = manager.createGameObject( sw::hashed_string( "PipelineChild" ) );
    SW_ASSERT_NOT_NULL( pChildObj->addComponent<sw::SceneComponent>() );
    SW_ASSERT_TRUE( pChildObj->attachToParent( pParentObj ) );
    sw::MockVehicleComponent* pVehicle = pChildObj->addComponent<sw::MockVehicleComponent>();
    pVehicle->setTickGroup( sw::TickGroup::DuringPhysics );
    pVehicle->_maxSpeed = 0.0f;

    // 3) 손자 (PostUpdate 단계에서 자식 속도에 기반하여 비행 고도 산출)
    sw::GameObject* pGrandObj = manager.createGameObject( sw::hashed_string( "PipelineGrand" ) );
    SW_ASSERT_NOT_NULL( pGrandObj->addComponent<sw::SceneComponent>() );
    SW_ASSERT_TRUE( pGrandObj->attachToParent( pChildObj ) );
    sw::MockFlyingVehicleComponent* pFlying = pGrandObj->addComponent<sw::MockFlyingVehicleComponent>();
    pFlying->setTickGroup( sw::TickGroup::PostUpdate );
    pFlying->_maxAltitude = 0.0f;

    // 커스텀 파이프라인 로깅 연결
    sw::vector<sw::string> listTickOrder;
    pPawn->_pTickOrderLog    = &listTickOrder;
    pPawn->_componentTag     = "Stage1_ParentPrePhysics";
    pVehicle->_pTickOrderLog = &listTickOrder;
    pVehicle->_componentTag  = "Stage2_ChildDuringPhysics";
    pFlying->_pTickOrderLog  = &listTickOrder;
    pFlying->_componentTag   = "Stage3_GrandPostUpdate";

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );
    if ( listTickOrder.size() == 3 )
    {
        SW_EXPECT_EQUAL( "Stage1_ParentPrePhysics", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Stage2_ChildDuringPhysics", listTickOrder[1] );
        SW_EXPECT_EQUAL( "Stage3_GrandPostUpdate", listTickOrder[2] );
    }

    // 3개 계층 컴포넌트 모두 누락 없이 1회씩 틱을 완료했는지 확인
    SW_EXPECT_EQUAL( 1, pPawn->_pawnTickCount );
    SW_EXPECT_EQUAL( 1, pVehicle->_vehicleTickCount );
    SW_EXPECT_EQUAL( 1, pFlying->_flyingTickCount );
}

/**
 * @brief [ComponentTickGroupTest] 런타임에 TickGroup이 동적으로 변경(Migration)되었을 때 틱 스테이지 재구성 및 순서 역전 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, DynamicTickGroupRuntimeMigration )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::vector<sw::string> listTickOrder;

    sw::GameObject*        pActorA = manager.createGameObject( sw::hashed_string( "ActorA" ) );
    sw::MockRootComponent* pCompA  = pActorA->addComponent<sw::MockRootComponent>();
    pCompA->setTickGroup( sw::TickGroup::PostUpdate ); // A는 처음엔 PostUpdate (나중에 실행)
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompA->_componentTag  = "ActorA";

    sw::GameObject*                 pActorB = manager.createGameObject( sw::hashed_string( "ActorB" ) );
    sw::MockFlyingVehicleComponent* pCompB  = pActorB->addComponent<sw::MockFlyingVehicleComponent>();
    pCompB->setTickGroup( sw::TickGroup::PrePhysics ); // B는 처음엔 PrePhysics (먼저 실행)
    pCompB->_pTickOrderLog = &listTickOrder;
    pCompB->_componentTag  = "ActorB";

    // Frame 1: B -> A 순서로 실행되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "ActorB", listTickOrder[0] );
        SW_EXPECT_EQUAL( "ActorA", listTickOrder[1] );
    }

    // 런타임 동적 TickGroup 변경 (A -> PrePhysics, B -> PostUpdate)
    listTickOrder.clear();
    pCompA->setTickGroup( sw::TickGroup::PrePhysics );
    pCompB->setTickGroup( sw::TickGroup::PostUpdate );
    pActorA->markTickOrderDirty();
    pActorB->markTickOrderDirty();

    // Frame 2: 즉시 A -> B 순서로 역전되어 실행되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "ActorA", listTickOrder[0] );
        SW_EXPECT_EQUAL( "ActorB", listTickOrder[1] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 동일 컴포넌트 내의 복수 서브틱이 Phase 및 우선순위 순서대로 완벽히 정렬되는지 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, IntraComponentMultiSubTickPhaseAndPriorityOrder )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActor = manager.createGameObject( hashed_string( "SkeletalActor" ) );

    auto* pComp          = pActor->addComponent<MockRootComponent>();
    pComp->_componentTag = "Skeletal";

    vector<string> listTickOrder;
    pComp->_pTickOrderLog = &listTickOrder;

    // 메인 틱은 PrePhysics (애니메이션 평가)
    pComp->setTickGroup( sw::TickGroup::PrePhysics );

    // PostPhysics 단계에 3개의 서브틱을 '역순(Finalize -> Normal -> Early)'으로 등록
    constexpr uint32 kSubTickEarly    = 1;
    constexpr uint32 kSubTickNormal   = 2;
    constexpr uint32 kSubTickFinalize = 3;

    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTickFinalize, sw::TickPhase::Finalize );
    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTickNormal, sw::TickPhase::Normal );
    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTickEarly, sw::TickPhase::Early );

    manager.tick( 0.016f );

    // 검증:
    // 1. PrePhysics: 메인 틱 (Skeletal)
    // 2. PostPhysics: Early(1) -> Normal(2) -> Finalize(3) 순서로 정확히 정렬되어야 함!
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), listTickOrder.size() );
    if ( listTickOrder.size() == 4 )
    {
        SW_EXPECT_EQUAL( "Skeletal", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Skeletal_SubTick_1", listTickOrder[1] );
        SW_EXPECT_EQUAL( "Skeletal_SubTick_2", listTickOrder[2] );
        SW_EXPECT_EQUAL( "Skeletal_SubTick_3", listTickOrder[3] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 서로 다른 액터/컴포넌트 간 Prerequisite DAG에 의해 선행 서브틱이 후행보다 먼저 실행되도록 위상 승격되는지 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, InterComponentPrerequisiteDAGDependencyElevation )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pHorseActor = manager.createGameObject( hashed_string( "HorseActor" ) );
    sw::GameObject*       pRiderActor = manager.createGameObject( hashed_string( "RiderActor" ) );

    auto* pHorseComp          = pHorseActor->addComponent<MockRootComponent>();
    pHorseComp->_componentTag = "Horse";
    pHorseComp->setCanEverTick( false ); // 메인 틱 제외

    auto* pRiderComp          = pRiderActor->addComponent<MockRootComponent>();
    pRiderComp->_componentTag = "Rider";
    pRiderComp->setCanEverTick( false ); // 메인 틱 제외

    vector<string> listTickOrder;
    pHorseComp->_pTickOrderLog = &listTickOrder;
    pRiderComp->_pTickOrderLog = &listTickOrder;

    // 말: PostPhysics의 Normal Phase (64)
    constexpr uint32  kHorseTick  = 10;
    sw::SubTickHandle horseHandle = pHorseComp->registerSubTick( sw::TickGroup::PostPhysics, kHorseTick, sw::TickPhase::Normal );

    // 기수: PostPhysics의 Early Phase (0)
    // 일반적인 Phase 정렬만으로는 Early(기수)가 Normal(말)보다 먼저 실행되게 됨
    constexpr uint32 kRiderTick = 20;
    pRiderComp->registerSubTick( sw::TickGroup::PostPhysics, kRiderTick, sw::TickPhase::Early );

    // 하지만 기수가 말의 틱에 종속성(Prerequisite)을 추가함!
    const bool bAdded = pRiderComp->addSubTickPrerequisite( kRiderTick, horseHandle );
    SW_EXPECT_EQUAL( true, bAdded );

    manager.tick( 0.016f );

    // Prerequisite DAG 위상 정렬에 의해 반드시 말(Horse)이 먼저 돌고 기수(Rider)가 돌아야 함!
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "Horse_SubTick_10", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Rider_SubTick_20", listTickOrder[1] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 서브틱 동적 활성화/비활성화(setSubTickActive) 및 등록 해제(unregisterSubTick) 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, SubTickDynamicLifecycleAndActiveToggle )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActor = manager.createGameObject( hashed_string( "DynamicActor" ) );

    auto* pComp          = pActor->addComponent<MockRootComponent>();
    pComp->_componentTag = "Actor";
    pComp->setCanEverTick( true );

    vector<string> listTickOrder;
    pComp->_pTickOrderLog = &listTickOrder;

    constexpr uint32 kSubTick1 = 1;
    constexpr uint32 kSubTick2 = 2;

    pComp->registerSubTick( sw::TickGroup::DuringPhysics, kSubTick1, sw::TickPhase::Normal );
    pComp->registerSubTick( sw::TickGroup::PostPhysics, kSubTick2, sw::TickPhase::Normal );

    // Frame 1: 메인 틱 + SubTick 1 + SubTick 2 실행
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );

    // 동적 제어: SubTick 1 비활성화, SubTick 2 등록 해제
    listTickOrder.clear();
    pComp->setSubTickActive( kSubTick1, false );
    const bool bUnregistered = pComp->unregisterSubTick( kSubTick2 );
    SW_EXPECT_EQUAL( true, bUnregistered );

    // Frame 2: 메인 틱만 실행되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listTickOrder.size() );
    if ( listTickOrder.size() == 1 )
        SW_EXPECT_EQUAL( "Actor", listTickOrder[0] );

    // 동적 제어: SubTick 1 다시 활성화
    listTickOrder.clear();
    pComp->setSubTickActive( kSubTick1, true );

    // Frame 3: 메인 틱 + SubTick 1 실행
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "Actor", listTickOrder[0] );
        SW_EXPECT_EQUAL( "Actor_SubTick_1", listTickOrder[1] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 순환 종속성(Circular Dependency) 발생 시 데드락/크래시 없이 방어 및 안전 실행 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, CircularPrerequisiteDependencyCycleResilience )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActorA = manager.createGameObject( hashed_string( "ActorA" ) );
    sw::GameObject*       pActorB = manager.createGameObject( hashed_string( "ActorB" ) );

    auto* pCompA          = pActorA->addComponent<MockRootComponent>();
    pCompA->_componentTag = "ActorA";
    pCompA->setCanEverTick( false );

    auto* pCompB          = pActorB->addComponent<MockRootComponent>();
    pCompB->_componentTag = "ActorB";
    pCompB->setCanEverTick( false );

    vector<string> listTickOrder;
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompB->_pTickOrderLog = &listTickOrder;

    sw::SubTickHandle handleA = pCompA->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Early );
    sw::SubTickHandle handleB = pCompB->registerSubTick( sw::TickGroup::DuringPhysics, 2, sw::TickPhase::Late );

    // A는 B에 의존하고, B는 A에 의존하는 상호 순환 참조(Cycle) 형성
    pCompA->addSubTickPrerequisite( 1, handleB );
    pCompB->addSubTickPrerequisite( 2, handleA );

    // 틱 실행: 무한루프나 크래시 없이 안전하게 실행 완료되어야 함
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
}

/**
 * @brief [ComponentSubTickHybridTest] 다단계 부모-자식 계층(Grandparent->Parent->Child), 다중 컴포넌트, 다중 서브틱 및 계층을 넘나드는 Prerequisite DAG 위상 정렬 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, DeepHierarchyMultiComponentMultiSubTickDAGOrder )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject* pGrandparent = manager.createGameObject( sw::hashed_string( "Grandparent" ) );
    sw::GameObject* pParent      = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject* pChild       = manager.createGameObject( sw::hashed_string( "Child" ) );
    // 계층은 씬 컴포넌트 사이에서 맺어진다(위 시험과 같다).
    for ( sw::GameObject* pObj : { pGrandparent, pParent, pChild } )
        SW_ASSERT_NOT_NULL( pObj->addComponent<sw::SceneComponent>() );

    SW_ASSERT_TRUE( pParent->attachToParent( pGrandparent ) );
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );

    vector<string> listTickOrder;

    // 1. Grandparent 컴포넌트들
    auto* pCompGP1           = pGrandparent->addComponent<MockRootComponent>();
    pCompGP1->_componentTag  = "GP1";
    pCompGP1->_pTickOrderLog = &listTickOrder;
    pCompGP1->setTickGroup( sw::TickGroup::PrePhysics ); // Main: PrePhysics
    const sw::SubTickHandle hGP1_PostPhysLate = pCompGP1->registerSubTick( sw::TickGroup::PostPhysics, 1, sw::TickPhase::Late );
    pCompGP1->registerSubTick( sw::TickGroup::PostUpdate, 2, sw::TickPhase::Finalize );

    auto* pCompGP2           = pGrandparent->addComponent<MockRootComponent>();
    pCompGP2->_componentTag  = "GP2";
    pCompGP2->_pTickOrderLog = &listTickOrder;
    pCompGP2->setCanEverTick( false ); // 메인 틱 끔
    pCompGP2->registerSubTick( sw::TickGroup::DuringPhysics, 10, sw::TickPhase::Normal );

    // 2. Parent 컴포넌트들
    auto* pCompP1           = pParent->addComponent<MockRootComponent>();
    pCompP1->_componentTag  = "P1";
    pCompP1->_pTickOrderLog = &listTickOrder;
    pCompP1->setTickGroup( sw::TickGroup::DuringPhysics ); // Main: DuringPhysics
    pCompP1->registerSubTick( sw::TickGroup::PrePhysics, 1, sw::TickPhase::Early );
    pCompP1->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Early );

    auto* pCompP2           = pParent->addComponent<MockRootComponent>();
    pCompP2->_componentTag  = "P2";
    pCompP2->_pTickOrderLog = &listTickOrder;
    pCompP2->setTickGroup( sw::TickGroup::PostPhysics ); // Main: PostPhysics (Normal Phase)
    pCompP2->registerSubTick( sw::TickGroup::DuringPhysics, 20, sw::TickPhase::Late );

    // 3. Child 컴포넌트들
    auto* pCompC1           = pChild->addComponent<MockRootComponent>();
    pCompC1->_componentTag  = "C1";
    pCompC1->_pTickOrderLog = &listTickOrder;
    pCompC1->setCanEverTick( false ); // 메인 틱 끔
    const sw::SubTickHandle hC1_PrePhysNormal  = pCompC1->registerSubTick( sw::TickGroup::PrePhysics, 100, sw::TickPhase::Normal );
    const sw::SubTickHandle hC1_PostPhysNormal = pCompC1->registerSubTick( sw::TickGroup::PostPhysics, 101, sw::TickPhase::Normal );

    auto* pCompC2           = pChild->addComponent<MockRootComponent>();
    pCompC2->_componentTag  = "C2";
    pCompC2->_pTickOrderLog = &listTickOrder;
    pCompC2->setTickGroup( sw::TickGroup::PostUpdate ); // Main: PostUpdate (Normal Phase)
    pCompC2->registerSubTick( sw::TickGroup::PostPhysics, 200, sw::TickPhase::Early );

    // 계층을 넘나드는 선행 종속성 (Cross-Hierarchy DAG Prerequisites) 설정:
    // A) PrePhysics: Child(C1_100, Normal)이 Parent(P1_1, Early)보다 먼저 돌도록 Parent에 선행 조건 등록
    pCompP1->addSubTickPrerequisite( 1, hC1_PrePhysNormal );

    // B) PostPhysics: 체인 의존성
    //    GP1_1(Late) -> C1_101(Normal) -> P1_2(Early)
    //    (Late가 먼저 실행되도록 위상 승격)
    pCompC1->addSubTickPrerequisite( 101, hGP1_PostPhysLate );
    pCompP1->addSubTickPrerequisite( 2, hC1_PostPhysNormal );

    manager.tick( 0.016f );

    // 총 13개 틱 아이템 실행 검증 (메인틱 4개 + 서브틱 9개)
    SW_EXPECT_EQUAL( static_cast<size_t>( 13 ), listTickOrder.size() );

    // 헬퍼: 틱 로그에서 특정 항목의 인덱스 검색
    auto findIndex = [&listTickOrder]( const string& tag ) -> size_t
    {
        for ( size_t index = 0; index < listTickOrder.size(); ++index )
        {
            if ( listTickOrder[index] == tag )
                return index;
        }
        return static_cast<size_t>( -1 );
    };

    const size_t idxC1_PrePhys100 = findIndex( "C1_SubTick_100" );
    const size_t idxP1_PrePhys1   = findIndex( "P1_SubTick_1" );
    const size_t idxGP1_MainPre   = findIndex( "GP1" );

    const size_t idxGP2_DurPhys10  = findIndex( "GP2_SubTick_10" );
    const size_t idxP1_DurPhysMain = findIndex( "P1" );
    const size_t idxP2_DurPhys20   = findIndex( "P2_SubTick_20" );

    const size_t idxGP1_PostPhysLate = findIndex( "GP1_SubTick_1" );
    const size_t idxC1_PostPhysNorm  = findIndex( "C1_SubTick_101" );
    const size_t idxP1_PostPhysEarly = findIndex( "P1_SubTick_2" );

    const size_t idxC2_PostUpdateMain = findIndex( "C2" );
    const size_t idxGP1_PostUpFin     = findIndex( "GP1_SubTick_2" );

    // 1) PrePhysics 그룹 내 위상 승격 검증: C1_100 -> P1_1
    SW_EXPECT_TRUE( idxC1_PrePhys100 < idxP1_PrePhys1 );
    // PrePhysics 항목들은 모두 DuringPhysics 항목들보다 먼저 실행되어야 함
    SW_EXPECT_TRUE( idxP1_PrePhys1 < idxGP2_DurPhys10 );
    SW_EXPECT_TRUE( idxGP1_MainPre < idxP1_DurPhysMain );

    // 2) DuringPhysics 항목들은 모두 PostPhysics 항목들보다 먼저 실행되어야 함
    SW_EXPECT_TRUE( idxP2_DurPhys20 < idxGP1_PostPhysLate );

    // 3) PostPhysics 체인 의존성 검증: GP1_1 (Late) -> C1_101 (Normal) -> P1_2 (Early)
    SW_EXPECT_TRUE( idxGP1_PostPhysLate < idxC1_PostPhysNorm );
    SW_EXPECT_TRUE( idxC1_PostPhysNorm < idxP1_PostPhysEarly );

    // 4) PostPhysics 항목들은 모두 PostUpdate 항목들보다 먼저 실행되어야 함
    SW_EXPECT_TRUE( idxP1_PostPhysEarly < idxC2_PostUpdateMain );
    SW_EXPECT_TRUE( idxC2_PostUpdateMain < idxGP1_PostUpFin );
}

/**
 * @brief [ComponentSubTickHybridTest] 틱 실행 도중(Mid-Tick) 서브틱이 동적으로 비활성화되거나 등록 해제되는 경우 즉시 스킵되는지 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, MidTickSubTickDeactivationAndCancellation )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject* pActorA = manager.createGameObject( sw::hashed_string( "ActorA" ) );
    sw::GameObject* pActorB = manager.createGameObject( sw::hashed_string( "ActorB" ) );

    auto* pCompA          = pActorA->addComponent<MockMidTickDeactivatorComponent>();
    pCompA->_componentTag = "A";

    auto* pCompB          = pActorB->addComponent<MockRootComponent>();
    pCompB->_componentTag = "B";
    pCompB->setCanEverTick( false );

    vector<string> listTickOrder;
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompB->_pTickOrderLog = &listTickOrder;

    // PostPhysics 단계 설정
    // A: SubTick 1 (Early) - 실행 시 B의 SubTick 20을 비활성화하고 자신의 SubTick 2를 unregister
    pCompA->registerSubTick( sw::TickGroup::PostPhysics, 1, sw::TickPhase::Early );
    pCompA->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Late );

    // B: SubTick 20 (Normal), SubTick 21 (Finalize)
    pCompB->registerSubTick( sw::TickGroup::PostPhysics, 20, sw::TickPhase::Normal );
    pCompB->registerSubTick( sw::TickGroup::PostPhysics, 21, sw::TickPhase::Finalize );

    pCompA->_pTargetComp             = pCompB;
    pCompA->_targetSubTickId         = 20;
    pCompA->_selfSubTickToUnregister = 2;

    // Frame 1: A_1(Early) 실행 시 B_20과 A_2를 끔 -> B_20과 A_2는 스킵되고 B_21(Finalize)만 실행
    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "A_SubTick_1", listTickOrder[0] );
        SW_EXPECT_EQUAL( "B_SubTick_21", listTickOrder[1] );
    }

    // Frame 2: B의 SubTick 20을 다시 켜고 A의 동적 비활성화 트리거 해제
    listTickOrder.clear();
    pCompA->_pTargetComp     = nullptr;
    pCompA->_targetSubTickId = 0;
    pCompB->setSubTickActive( 20, true );

    manager.tick( 0.016f );

    // Frame 2에서는 A_1, B_20, B_21 세 개가 모두 정상 실행되어야 함 (A_2는 unregister되었으므로 미실행)
    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );
    if ( listTickOrder.size() == 3 )
    {
        SW_EXPECT_EQUAL( "A_SubTick_1", listTickOrder[0] );
        SW_EXPECT_EQUAL( "B_SubTick_20", listTickOrder[1] );
        SW_EXPECT_EQUAL( "B_SubTick_21", listTickOrder[2] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 서브틱 id 64 번부터도 틱 중에 끄거나 해제하면 이번 틱의 남은 항목이 바로 건너뛴다
 * @details 1~63 은 컴포넌트의 원자 마스크가 바로 꺼진다. 64 번부터도 같아야 한다 — 활성이 목록 값뿐이라 틱 뒤에야 바뀌면 같은 틱의 뒤 단계
 *          항목이 그대로 돌아, id 크기에 따라 같은 호출의 결과가 달라진다.
 */
SW_TEST_CASE( ComponentSubTickHybridTest, MidTickDeactivationAppliesToHighSubTickIds )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject* pActorA = manager.createGameObject( sw::hashed_string( "ActorA" ) );
    sw::GameObject* pActorB = manager.createGameObject( sw::hashed_string( "ActorB" ) );

    auto* pCompA          = pActorA->addComponent<MockMidTickDeactivatorComponent>();
    pCompA->_componentTag = "A";
    auto* pCompB          = pActorB->addComponent<MockRootComponent>();
    pCompB->_componentTag = "B";
    pCompB->setCanEverTick( false );

    vector<string> listTickOrder;
    pCompA->_pTickOrderLog = &listTickOrder;
    pCompB->_pTickOrderLog = &listTickOrder;

    // A_100(Early) 이 돌면서 B_120 을 끄고 자기 A_101(Late) 을 해제한다. B_121(Finalize) 만 남아야 한다.
    pCompA->registerSubTick( sw::TickGroup::PostPhysics, 100, sw::TickPhase::Early );
    pCompA->registerSubTick( sw::TickGroup::PostPhysics, 101, sw::TickPhase::Late );
    pCompB->registerSubTick( sw::TickGroup::PostPhysics, 120, sw::TickPhase::Normal );
    pCompB->registerSubTick( sw::TickGroup::PostPhysics, 121, sw::TickPhase::Finalize );
    pCompA->_pTargetComp             = pCompB;
    pCompA->_targetSubTickId         = 120;
    pCompA->_selfSubTickToUnregister = 101;

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
    if ( listTickOrder.size() == 2 )
    {
        SW_EXPECT_EQUAL( "A_SubTick_100", listTickOrder[0] );
        SW_EXPECT_EQUAL( "B_SubTick_121", listTickOrder[1] );
    }
    SW_EXPECT_FALSE( pCompB->isSubTickActive( 120 ) );
    SW_EXPECT_FALSE( pCompA->isSubTickActive( 101 ) );

    // 다시 켜면 다음 틱에 돈다.
    listTickOrder.clear();
    pCompA->_pTargetComp     = nullptr;
    pCompA->_targetSubTickId = 0;
    pCompB->setSubTickActive( 120, true );
    SW_EXPECT_TRUE( pCompB->isSubTickActive( 120 ) );

    manager.tick( 0.016f );

    SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );
    if ( listTickOrder.size() == 3 )
    {
        SW_EXPECT_EQUAL( "A_SubTick_100", listTickOrder[0] );
        SW_EXPECT_EQUAL( "B_SubTick_120", listTickOrder[1] );
        SW_EXPECT_EQUAL( "B_SubTick_121", listTickOrder[2] );
    }
}

/**
 * @brief [ComponentSubTickHybridTest] 부모-자식 계층에서 서브트리 비활성화, 재부모화(Reparenting), 연쇄 파괴 시 서브틱 라이프사이클 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, HierarchySubtreeDeactivationAndReparentingWithSubTicks )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject* pRoot    = manager.createGameObject( sw::hashed_string( "Root" ) );
    sw::GameObject* pBranch1 = manager.createGameObject( sw::hashed_string( "Branch1" ) );
    sw::GameObject* pLeaf1   = manager.createGameObject( sw::hashed_string( "Leaf1" ) );
    sw::GameObject* pBranch2 = manager.createGameObject( sw::hashed_string( "Branch2" ) );
    sw::GameObject* pLeaf2   = manager.createGameObject( sw::hashed_string( "Leaf2" ) );

    vector<string> listTickOrder;

    auto setupActor = [&listTickOrder]( sw::GameObject* pObj, const string& tag )
    {
        auto* pComp           = pObj->addComponent<MockRootComponent>();
        pComp->_componentTag  = tag;
        pComp->_pTickOrderLog = &listTickOrder;
        pComp->setCanEverTick( false ); // 메인 틱 제외
        pComp->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Early );
        pComp->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Normal );
    };

    // 계층 부착 전 컴포넌트(SceneComponent)를 먼저 생성
    setupActor( pRoot, "Root" );
    setupActor( pBranch1, "Branch1" );
    setupActor( pLeaf1, "Leaf1" );
    setupActor( pBranch2, "Branch2" );
    setupActor( pLeaf2, "Leaf2" );

    SW_ASSERT_TRUE( pBranch1->attachToParent( pRoot ) );
    SW_ASSERT_TRUE( pLeaf1->attachToParent( pBranch1 ) );
    SW_ASSERT_TRUE( pBranch2->attachToParent( pRoot ) );
    SW_ASSERT_TRUE( pLeaf2->attachToParent( pBranch2 ) );

    // Frame 1: 5개 액터 전체 활성 (각 2개 서브틱 = 총 10개)
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 10 ), listTickOrder.size() );

    // Frame 2: Branch1 비활성화 -> Branch1 및 Leaf1 서브트리 전체 틱 스킵 (Root, Branch2, Leaf2 = 총 6개)
    listTickOrder.clear();
    pBranch1->setActive( false );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 6 ), listTickOrder.size() );

    // Frame 3: Leaf1을 비활성화된 Branch1에서 활성화된 Branch2 밑으로 Reparent
    listTickOrder.clear();
    SW_ASSERT_TRUE( pLeaf1->attachToParent( pBranch2 ) );
    SW_EXPECT_TRUE( pLeaf1->isActiveInHierarchy() );
    manager.tick( 0.016f );
    // Root, Branch2, Leaf2, Leaf1 = 총 8개 실행 (Branch1만 스킵)
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), listTickOrder.size() );

    // Frame 4: Branch2 연쇄 삭제 (Branch2, Leaf2, Leaf1 삭제) -> Root만 남음 (2개)
    listTickOrder.clear();
    manager.destroyObject( pBranch2, true );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listTickOrder.size() );
}

/**
 * @brief [ComponentSubTickHybridTest] 100개 이상의 액터, 300개 컴포넌트, 600개 서브틱의 고밀도 다이아몬드 DAG 및 체인 종속성 멀티스레드 스트레스 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, MassiveSubTickStressAndMultiThreadedDAGValidation )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    constexpr size_t kActorCount    = 100;
    constexpr size_t kTotalSubTicks = kActorCount * 6; // 600개 서브틱

    std::atomic<uint32> globalTickSeq{ 1 };
    std::atomic<uint32> arrExecutionOrder[kTotalSubTicks + 16];
    for ( size_t index = 0; index < kTotalSubTicks + 16; ++index )
        arrExecutionOrder[index].store( 0, std::memory_order_relaxed );

    vector<sw::GameObject*>             listActor;
    vector<MockSubTickStressComponent*> listCompA;
    vector<MockSubTickStressComponent*> listCompB;
    listActor.reserve( kActorCount );
    listCompA.reserve( kActorCount );
    listCompB.reserve( kActorCount );

    struct DagEdge
    {
        uint32 _prereqGlobalId;
        uint32 _dependentGlobalId;
    };
    vector<DagEdge> listDagEdge;

    for ( size_t actorIdx = 0; actorIdx < kActorCount; ++actorIdx )
    {
        sw::fixed_string<sw::constant::kMaxBuffer64> nameBuf{};
        sw::formatstring( nameBuf.data(), nameBuf.capacity(), "StressActor_%#", actorIdx );
        sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( nameBuf.c_str() ) );
        listActor.push_back( pActor );

        // 컴포넌트 2개 부착
        auto* pCompA = pActor->addComponent<MockSubTickStressComponent>();
        auto* pCompB = pActor->addComponent<MockSubTickStressComponent>();

        pCompA->setCanEverTick( false );
        pCompB->setCanEverTick( false );

        pCompA->_pGlobalTickSequence   = &globalTickSeq;
        pCompA->_pExecutionOrderArray  = arrExecutionOrder;
        pCompA->_subTickGlobalIdOffset = static_cast<uint32>( actorIdx * 6 );

        pCompB->_pGlobalTickSequence   = &globalTickSeq;
        pCompB->_pExecutionOrderArray  = arrExecutionOrder;
        pCompB->_subTickGlobalIdOffset = static_cast<uint32>( actorIdx * 6 + 3 );

        // SubTick 등록 (CompA: 1, 2 / CompB: 1, 2)
        pCompA->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Early );
        pCompA->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Normal );

        pCompB->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Normal );
        pCompB->registerSubTick( sw::TickGroup::PostPhysics, 2, sw::TickPhase::Late );
        listCompA.push_back( pCompA );
        listCompB.push_back( pCompB );
    }

    // 1. 다이아몬드 DAG 종속성 30개 생성:
    // Node A(DuringPhysics, CompA_1) -> Node B(DuringPhysics, CompB_1)
    // Node A(DuringPhysics, CompA_1) -> Node C(DuringPhysics, nextActor CompA_1)
    // Node B, C -> Node D(DuringPhysics, nextActor CompB_1)
    for ( size_t diamondIdx = 0; diamondIdx < 30; ++diamondIdx )
    {
        const size_t actorAIdx = diamondIdx * 2;
        const size_t actorBIdx = diamondIdx * 2 + 1;

        auto* pCompA1 = listCompA[actorAIdx];
        auto* pCompB1 = listCompB[actorBIdx];

        const sw::SubTickHandle hA = sw::SubTickHandle{ pCompA1->getComponentId(), 1 };

        pCompB1->addSubTickPrerequisite( 1, hA );

        const uint32 gIdA = static_cast<uint32>( actorAIdx * 6 + 1 );
        const uint32 gIdB = static_cast<uint32>( actorBIdx * 6 + 3 + 1 );
        listDagEdge.push_back( { gIdA, gIdB } );
    }

    // 2. 10단계 긴 의존성 체인 생성 (PostPhysics)
    // T0 -> T1 -> T2 -> ... -> T9
    for ( size_t chainIdx = 0; chainIdx < 9; ++chainIdx )
    {
        auto* pCompSrc = listCompA[chainIdx];
        auto* pCompDst = listCompA[chainIdx + 1];

        const sw::SubTickHandle hSrc = sw::SubTickHandle{ pCompSrc->getComponentId(), 2 };
        pCompDst->addSubTickPrerequisite( 2, hSrc );

        const uint32 gIdSrc = static_cast<uint32>( chainIdx * 6 + 2 );
        const uint32 gIdDst = static_cast<uint32>( ( chainIdx + 1 ) * 6 + 2 );
        listDagEdge.push_back( { gIdSrc, gIdDst } );
    }

    // 5 프레임 동안 멀티스레드 스트레스 틱 실행 및 매 프레임 DAG 위상 정렬 정밀 검증
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        globalTickSeq.store( 1, std::memory_order_relaxed );

        manager.tick( 0.016f );

        // 모든 등록된 선행 의존성 엣지에 대해 선행 노드가 후행 노드보다 먼저 실행되었는지 검증
        for ( const DagEdge& edge : listDagEdge )
        {
            const uint32 orderPrereq    = arrExecutionOrder[edge._prereqGlobalId].load( std::memory_order_acquire );
            const uint32 orderDependent = arrExecutionOrder[edge._dependentGlobalId].load( std::memory_order_acquire );

            SW_EXPECT_TRUE( orderPrereq != 0 );
            SW_EXPECT_TRUE( orderDependent != 0 );
            SW_EXPECT_TRUE( orderPrereq < orderDependent );
        }
    }
}

/**
 * @brief `onTick` 을 오버라이드한 것이 곧 틱 선언이다 — 씬 컴포넌트도 돌고, `onTick` 이 없는 컴포넌트는 틱 목록에 들지 않는다.
 * @details 타입마다 기본값이 갈리면(`Component` 는 켜짐, `SceneComponent` 는 꺼짐) 씬 컴포넌트에 `onTick` 을 쓰고 선언을 잊을 때 **조용히 한 번도
 *          돌지 않고**, `onTick` 이 없는 데이터 컴포넌트(영속 표시 같은 것)는 매 프레임 빈 가상 호출로 디스패치된다. 유니티는 `Update` 가 있으면
 *          부르고 없으면 목록에 넣지 않는다. 언리얼의 `bCanEverTick = false` 처럼 생성자가 끄면 끈 것이 이긴다.
 */
SW_TEST_CASE( ComponentTickGroupTest, OverridingOnTickIsWhatMakesAComponentTick )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( hashed_string( "Spinner" ) );

    auto* pSpinner = pObject->addComponent<MockSelfTickSceneComponent>();
    auto* pOptOut  = pObject->addComponent<MockSelfTickSceneComponent>( true );
    auto* pMarker  = pObject->addComponent<MockNoTickComponent>();
    SW_ASSERT_NOT_NULL( pSpinner );
    SW_ASSERT_NOT_NULL( pOptOut );
    SW_ASSERT_NOT_NULL( pMarker );

    SW_EXPECT_TRUE( pSpinner->canEverTick() );
    SW_EXPECT_FALSE( pOptOut->canEverTick() );
    SW_EXPECT_FALSE( pMarker->canEverTick() );
    SW_EXPECT_FALSE( pMarker->hasTickWork() );

    manager.tick( 0.016f );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 2, pSpinner->_tickCount );
    SW_EXPECT_EQUAL( 0, pOptOut->_tickCount );
    // 틱 항목은 돌 것 하나뿐이다.
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( pObject->getTickItems().size() ) );
    SW_EXPECT_EQUAL( static_cast<sw::Component*>( pSpinner ), pObject->getTickItems()[0]._pComponent );
}

/**
 * @brief 범위 밖의 틱 그룹은 거절한다 — 받아 두면 등록부가 조용히 버려 그 컴포넌트가 한 번도 돌지 않는다.
 * @details 그룹은 넷(`TickRegistry::kGroupCount`)이다. 정수에서 캐스트한 값(데이터 주도 설정)이 넘으면 `setTickGroup` 은 그대로 두고,
 *          `registerSubTick` 은 빈 핸들을 준다(언리얼 `TG_MAX` 도 유효한 그룹이 아니다).
 */
SW_TEST_CASE( ComponentTickGroupTest, OutOfRangeTickGroupIsRejected )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( hashed_string( "Grouped" ) );
    auto*                 pComp   = pObject->addComponent<MockRootComponent>();
    SW_ASSERT_NOT_NULL( pComp );
    vector<string> listTickOrder;
    pComp->_pTickOrderLog = &listTickOrder;
    pComp->_componentTag  = "Root";

    pComp->setTickGroup( sw::TickGroup::PostPhysics );
    pComp->setTickGroup( static_cast<sw::TickGroup>( 7 ) );
    SW_EXPECT_TRUE( pComp->getTickGroup() == sw::TickGroup::PostPhysics );

    const sw::SubTickHandle handle = pComp->registerSubTick( static_cast<sw::TickGroup>( 9 ), 5 );
    SW_EXPECT_FALSE( handle.isValid() );
    SW_EXPECT_TRUE( pComp->getAllSubTicks().empty() );

    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listTickOrder.size() );
    SW_EXPECT_EQUAL( "Root", listTickOrder[0] );
}

/**
 * @brief 우선순위는 단계 안의 0..63 이다 — 넘으면 그 단계의 맨 뒤로 묶는다(`& 63` 으로 감기면 70 이 6 이 되어 10 보다 앞선다).
 */
SW_TEST_CASE( ComponentSubTickHybridTest, PriorityAboveTheBandStaysLastInItsPhase )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pActor = manager.createGameObject( hashed_string( "PriorityActor" ) );
    auto*                 pComp  = pActor->addComponent<MockRootComponent>();
    SW_ASSERT_NOT_NULL( pComp );
    pComp->setCanEverTick( false );
    pComp->_componentTag = "P";
    vector<string> listTickOrder;
    pComp->_pTickOrderLog = &listTickOrder;

    pComp->registerSubTick( sw::TickGroup::DuringPhysics, 1, sw::TickPhase::Normal, 70 );
    pComp->registerSubTick( sw::TickGroup::DuringPhysics, 2, sw::TickPhase::Normal, 10 );
    // 묶어도 다음 단계(Late)의 0 보다는 앞이다 — 단계를 넘어가면 안 된다.
    pComp->registerSubTick( sw::TickGroup::DuringPhysics, 3, sw::TickPhase::Late, 0 );

    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listTickOrder.size() );
    SW_EXPECT_EQUAL( "P_SubTick_2", listTickOrder[0] );
    SW_EXPECT_EQUAL( "P_SubTick_1", listTickOrder[1] );
    SW_EXPECT_EQUAL( "P_SubTick_3", listTickOrder[2] );
}

/**
 * @brief 선행 조건이 뒤 그룹에 있으면 뒤따르는 틱이 그 그룹으로 옮겨 간다(언리얼 `ActualStartTickGroup`). 사슬을 따라 옮긴다.
 * @details 그룹마다 따로 DAG 를 지으면 다른 그룹의 선행 조건을 "찾을 수 없음" 으로 버려, PrePhysics 의 기수가 PostPhysics 의 말보다
 *          먼저 돈다. 앞 그룹의 선행 조건은 이미 끝났으므로 그대로다.
 */
SW_TEST_CASE( ComponentSubTickHybridTest, PrerequisiteInALaterGroupMovesTheDependentThere )
{
    sw::GameObjectManager manager;
    vector<string>        listTickOrder;

    auto addSubTicker = [&manager, &listTickOrder]( const utf8* pName ) -> MockRootComponent*
    {
        sw::GameObject* pActor = manager.createGameObject( hashed_string( pName ) );
        auto*           pComp  = pActor->addComponent<MockRootComponent>();
        pComp->setCanEverTick( false );
        pComp->_componentTag  = pName;
        pComp->_pTickOrderLog = &listTickOrder;
        return pComp;
    };

    // 시계: DuringPhysics 의 주 틱 — 그룹 경계를 보는 표지.
    sw::GameObject* pClockActor = manager.createGameObject( hashed_string( "Clock" ) );
    auto*           pClock      = pClockActor->addComponent<MockRootComponent>();
    pClock->setTickGroup( sw::TickGroup::DuringPhysics );
    pClock->_componentTag  = "Clock";
    pClock->_pTickOrderLog = &listTickOrder;

    MockRootComponent* pLance = addSubTicker( "Lance" );
    MockRootComponent* pRider = addSubTicker( "Rider" );
    MockRootComponent* pHorse = addSubTicker( "Horse" );

    const sw::SubTickHandle horse = pHorse->registerSubTick( sw::TickGroup::PostPhysics, 10 );
    const sw::SubTickHandle rider = pRider->registerSubTick( sw::TickGroup::PrePhysics, 20 );
    pLance->registerSubTick( sw::TickGroup::DuringPhysics, 30 );
    SW_EXPECT_TRUE( pRider->addSubTickPrerequisite( 20, horse ) );
    SW_EXPECT_TRUE( pLance->addSubTickPrerequisite( 30, rider ) );

    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( static_cast<size_t>( 4 ), listTickOrder.size() );
    SW_EXPECT_EQUAL( "Clock", listTickOrder[0] );
    SW_EXPECT_EQUAL( "Horse_SubTick_10", listTickOrder[1] );
    SW_EXPECT_EQUAL( "Rider_SubTick_20", listTickOrder[2] );
    SW_EXPECT_EQUAL( "Lance_SubTick_30", listTickOrder[3] );
}

/**
 * @brief [ComponentDefaultsTest] gamedata 의 기본값은 **기반 타입 노드부터** 적용된다
 * @details 적용은 한 곳(`applyTypeDefaults`)에서 뿌리 → 파생 순서로 체인 전체를 한다. 기반 생성자에서 적용하면
 *          그 시점에는 가상 `getTypeInfo()` 가 파생으로 디스패치되지 않아 언제나 `Component` 의 TypeInfo 만 나오고,
 *          중간 기반(`SceneComponent`)의 기본값이 적용되지 않는다.
 *          이 테스트는 그 순서를 고정한다: 기반 노드의 값이 들어가고, 같은 프로퍼티를 파생이
 *          다시 적으면 파생이 이긴다.
 */
SW_TEST_CASE( ComponentDefaultsTest, BaseTypeDefaultsApplyBeforeDerived )
{
    const sw::string defaultsPath =
        test::makeTempPath( "test_component_defaults_chain.xml" );

    // SceneComponent(기반)가 Scale 을, MeshComponent(파생)가 BoundsRadius 를 정한다.
    const sw::string xml =
        "<GameData>\n"
        "  <Defaults>\n"
        "    <SceneComponent _localScale=\"2,3,4\" />\n"
        "    <MeshComponent _boundsRadius=\"7.5\" />\n"
        "  </Defaults>\n"
        "</GameData>\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( defaultsPath, xml ) );

    const sw::string previousPath{ sw::Component::getDefaultGamedataPath() };
    sw::Component::setDefaultGamedataPath( defaultsPath );

    {
        sw::GameObjectManager manager;
        sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "DefaultsChainTarget" ) );
        sw::MeshComponent*    pMesh   = ( pObject != nullptr ) ? pObject->addComponent<sw::MeshComponent>() : nullptr;
        SW_ASSERT_TRUE( pMesh != nullptr );

        // 기반(SceneComponent) 노드가 적용됐는가 — 빠지면 여기가 (1,1,1) 이다.
        const sw::float3 scale = pMesh->getLocalScale();
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( scale._x, 2.0f ) );
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( scale._y, 3.0f ) );
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( scale._z, 4.0f ) );

        // 파생(MeshComponent) 노드도 그대로 적용된다.
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( pMesh->getBoundsRadius(), 7.5f ) );
    }

    sw::Component::setDefaultGamedataPath( previousPath );
    sw::ComponentDefaults::reloadDefaults();
}

/**
 * @brief [ComponentDefaultsTest] 한 타입의 해석 결과는 다른 타입들이 캐시에 들어와도 제자리에 있다
 * @details `apply` 는 해석 결과(`resolveFor` 가 돌려준 참조)를 **잠금 밖에서** 돈다. 결과가 해시 맵의 밀집 벡터 안에 살면,
 *          다른 타입이 처음 들어오며 벡터가 다시 잡힐 때 그 참조가 풀린 메모리를 가리킨다 — 비동기 씬 로드의 워커와 게임 스레드가
 *          처음 보는 타입을 함께 만들 때의 해제 후 사용이다. 등록된 모든 타입을 한 번씩 풀게 해 벡터를 여러 번 자라게 한다.
 */
SW_TEST_CASE( ComponentDefaultsTest, ResolvedDefaultsStayPutWhileOtherTypesResolve )
{
    if ( sw::engine::areEngineServicesBound() == false )
        SW_TEST_SKIP( "ComponentDefaults service is not bound in this process." );

    const sw::string defaultsPath = test::makeTempPath( "test_component_defaults_stable.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( defaultsPath, "<GameData><Defaults><MeshComponent _boundsRadius=\"7.5\" /></Defaults></GameData>\n" ) );
    const sw::string previousPath{ sw::Component::getDefaultGamedataPath() };
    sw::Component::setDefaultGamedataPath( defaultsPath );

    sw::ComponentDefaults& defaults  = sw::engine::getComponentDefaults();
    const sw::TypeInfo*    pMeshType = sw::MeshComponent::StaticType();
    SW_ASSERT_NOT_NULL( pMeshType );
    sw::vector<uint8> meshScratch( pMeshType->_size, 0 );
    defaults.apply( meshScratch.data(), *pMeshType );
    const void* pAddress = defaults.findResolvedAddress( *pMeshType );
    SW_ASSERT_NOT_NULL( pAddress );

    // 레지스트리 잠금을 쥔 채 풀지 않는다(해석이 레지스트리를 다시 찾는다) — 목록을 먼저 받는다.
    sw::vector<const sw::TypeInfo*> listType;
    sw::engine::getTypeRegistry().forEachType( [&listType]( const sw::TypeInfo& typeInfo )
    { listType.push_back( &typeInfo ); } );
    SW_ASSERT_TRUE( listType.size() >= 32 );
    for ( const sw::TypeInfo* pType : listType )
    {
        // 기본값은 메시(와 그 파생)의 float 하나뿐이다 — 크기만큼의 빈 버퍼에 써도 안전하다.
        sw::vector<uint8> scratch( pType->_size > 0 ? pType->_size : 1, 0 );
        defaults.apply( scratch.data(), *pType );
    }
    SW_EXPECT_TRUE( defaults.findResolvedAddress( *pMeshType ) == pAddress );

    sw::Component::setDefaultGamedataPath( previousPath );
    sw::ComponentDefaults::reloadDefaults();
}

/**
 * @brief [ComponentDefaultsTest] 모듈이 타입을 등록해도 살아 있는 컴포넌트의 값은 기본값으로 돌아가지 않는다
 * @details 기본값은 만들 때 한 번이다. 모듈 등록(`registerModuleTypes`)이나 게임 DLL 리로드가 씬의 모든 컴포넌트에 기본값을
 *          다시 찍으면 게임이 바꾼 값이 모듈 로드 · 핫 리로드마다 기본값으로 돌아간다.
 */
SW_TEST_CASE( ComponentDefaultsTest, ModuleRegistrationKeepsLiveValues )
{
    if ( sw::engine::areEngineServicesBound() == false )
        SW_TEST_SKIP( "ComponentDefaults service is not bound in this process." );

    const sw::string defaultsPath = test::makeTempPath( "test_component_defaults_restamp.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( defaultsPath, "<GameData><Defaults><MeshComponent _boundsRadius=\"7.5\" /></Defaults></GameData>\n" ) );
    const sw::string previousPath{ sw::Component::getDefaultGamedataPath() };
    sw::Component::setDefaultGamedataPath( defaultsPath );

    // 모듈 등록은 엔진 씬 매니저의 씬들을 돈다 — 그 씬에 둔다.
    sw::Scene* pScene = sw::engine::getSceneManager().getActiveScene();
    if ( pScene == nullptr )
        pScene = sw::engine::getSceneManager().createScene( "DefaultsRestampScene" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();
    sw::GameObject*        pObject  = pObjects->createGameObject( sw::hashed_string( "DefaultsRestampProbe" ) );
    sw::MeshComponent*     pMesh    = pObject->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( pMesh->getBoundsRadius(), 7.5f ) );

    pMesh->setBoundsRadius( 2.0f );
    sw::engine::registerModuleTypes( "DefaultsRestampProbeModule" );
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( pMesh->getBoundsRadius(), 2.0f ) );
#if !defined( SW_SHIPPING )
    sw::engine::unregisterModuleTypes( "DefaultsRestampProbeModule" );
#endif

    pObjects->destroyObject( pObject );
    pObjects->processDeferredDestruction();
    sw::Component::setDefaultGamedataPath( previousPath );
    sw::ComponentDefaults::reloadDefaults();
}

/**
 * @brief [ComponentDefaultsTest] **없는** 기본값 파일은 딱 한 번만 열어 본다
 * @details 성공 깃발 하나만 보면 로드가 실패할 때 그 깃발이 false 로 남아 **컴포넌트를 만들 때마다 파일을 다시 연다.**
 *          기본값 파일은 있어도 되고 없어도 되는 것이라 없는 게 정상인데, 그러면 씬 로드가
 *          `컴포넌트 수 x 파일 열기 실패` 가 되어 로드 시간의 대부분을 차지한다.
 * @note 시간으로 재면 흔들리므로 **열어 본 횟수**로 본다.
 */
SW_TEST_CASE( ComponentDefaultsTest, MissingDefaultsFileIsOpenedOnlyOnce )
{
    if ( sw::engine::areEngineServicesBound() == false )
    {
        SW_TEST_SKIP( "ComponentDefaults service is not bound in this process." );
        return;
    }
    sw::ComponentDefaults& defaults = sw::engine::getComponentDefaults();

    const sw::string previousPath{ sw::Component::getDefaultGamedataPath() };

    // 일부러 없는 경로를 준다 — 이것이 "기본값 없음" 의 정상 상태다.
    const sw::string missingPath = test::makeTempPath( "no_such_component_defaults.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( missingPath ) );
    defaults.setPath( missingPath );

    const uint32 before = defaults.getLoadAttemptCount();

    {
        sw::GameObjectManager manager;
        sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "DefaultsRetryTarget" ) );
        SW_ASSERT_TRUE( pObject != nullptr );

        // 컴포넌트를 여럿 만든다 — 실패를 기억하지 않으면 이 수만큼 파일을 다시 연다.
        constexpr int32 kComponentCount = 32;
        for ( int32 index = 0; index < kComponentCount; ++index )
        {
            sw::GameObject* pEach = manager.createGameObject( sw::hashed_string( "Each" ) );
            SW_ASSERT_TRUE( pEach != nullptr );
            SW_ASSERT_TRUE( pEach->addComponent<sw::MeshComponent>() != nullptr );
        }
    }

    const uint32 attempts = defaults.getLoadAttemptCount() - before;
    SW_EXPECT_TRUE_MSG( attempts <= 1, "없는 기본값 파일을 컴포넌트마다 다시 열었습니다" );

    sw::Component::setDefaultGamedataPath( previousPath );
    sw::ComponentDefaults::reloadDefaults();
}

/**
 * @brief [ComponentDefaultsTest] 기본값 경로가 **값으로** 돌아오는지 검증
 * @details `getDefaultGamedataPath()` 가 `string_view` 를 돌려주면 그 뷰는 뮤텍스로 지키는 `_customDefaultsPath` 의
 *          내부 버퍼를 가리키는데, **뮤텍스는 함수가 끝나면서 풀린다.** 받아 든 쪽이 그것을 들고 있는 동안 다른 곳에서
 *          `setDefaultGamedataPath` 를 부르면(길이가 달라지면 버퍼를 새로 잡는다) 뷰는 사라진 메모리를 가리킨다.
 */
SW_TEST_CASE( ComponentDefaultsTest, DefaultsPathIsReturnedByValue )
{
    const sw::string previousPath{ sw::Component::getDefaultGamedataPath() };

    sw::Component::setDefaultGamedataPath( "game/data/short.xml" );
    const auto heldPath = sw::Component::getDefaultGamedataPath();

    // 길이를 크게 바꿔 내부 버퍼를 **다시 잡게** 만든다.
    sw::Component::setDefaultGamedataPath( sw::string( 4096, 'x' ) );

    // 뷰를 돌려주면 여기서 사라진 버퍼를 읽는다 — ASAN 이 잡는다.
    SW_EXPECT_STREQ( "game/data/short.xml", sw::string( heldPath ).c_str() );

    sw::Component::setDefaultGamedataPath( previousPath );
    sw::ComponentDefaults::reloadDefaults();
}
