#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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
    sw::RegisterMockComponents( manager );

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
    sw::RegisterMockComponents( manager );

    // 1) 루트 부모 (PrePhysics 단계에서 기본 체력 연산)
    sw::GameObject*            pParentObj = manager.createGameObject( sw::hashed_string( "PipelineParent" ) );
    sw::MockBasePawnComponent* pPawn      = pParentObj->addComponent<sw::MockBasePawnComponent>();
    pPawn->setTickGroup( sw::TickGroup::PrePhysics );
    pPawn->_pawnHealth = 100;

    // 2) 자식 (DuringPhysics 단계에서 부모 체력에 기반하여 최대 속도 산출)
    sw::GameObject* pChildObj = manager.createGameObject( sw::hashed_string( "PipelineChild" ) );
    pChildObj->attachToParent( pParentObj );
    sw::MockVehicleComponent* pVehicle = pChildObj->addComponent<sw::MockVehicleComponent>();
    pVehicle->setTickGroup( sw::TickGroup::DuringPhysics );
    pVehicle->_maxSpeed = 0.0f;

    // 3) 손자 (PostUpdate 단계에서 자식 속도에 기반하여 비행 고도 산출)
    sw::GameObject* pGrandObj = manager.createGameObject( sw::hashed_string( "PipelineGrand" ) );
    pGrandObj->attachToParent( pChildObj );
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
 * @brief [ComponentTickGroupTest] 런타임에 TickGroup이 동적으로 변경(Migration)되었을 때 틱 웨이브 재구성 및 순서 역전 검증
 */
SW_TEST_CASE( ComponentTickGroupTest, DynamicTickGroupRuntimeMigration )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

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
    sw::RegisterMockComponents( manager );

    sw::GameObject* pGrandparent = manager.createGameObject( sw::hashed_string( "Grandparent" ) );
    sw::GameObject* pParent      = manager.createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject* pChild       = manager.createGameObject( sw::hashed_string( "Child" ) );

    pParent->attachToParent( pGrandparent );
    pChild->attachToParent( pParent );

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
    sw::RegisterMockComponents( manager );

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
 * @brief [ComponentSubTickHybridTest] 부모-자식 계층에서 서브트리 비활성화, 재부모화(Reparenting), 연쇄 파괴 시 서브틱 라이프사이클 검증
 */
SW_TEST_CASE( ComponentSubTickHybridTest, HierarchySubtreeDeactivationAndReparentingWithSubTicks )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

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

    pBranch1->attachToParent( pRoot );
    pLeaf1->attachToParent( pBranch1 );
    pBranch2->attachToParent( pRoot );
    pLeaf2->attachToParent( pBranch2 );

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
    pLeaf1->attachToParent( pBranch2 );
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
    sw::RegisterMockComponents( manager );

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
 * @brief [GameObjectHierarchy] refreshActiveInHierarchy 부모-자식-손자 다계층 합성 활성 상태 엣지 케이스 검증
 */
/**
 * @brief [ComponentDefaultsTest] gamedata 의 기본값은 **기반 타입 노드부터** 적용된다
 * @details 예전에는 기반 기본값 적용이 `Component` 생성자에 있었는데, 기반 생성자 시점에는 가상
 *          `getTypeInfo()` 가 파생으로 디스패치되지 않아 언제나 `Component` 의 TypeInfo 만 나왔다
 *          — 중간 기반(`SceneComponent`)의 기본값은 한 번도 적용된 적이 없고, 파생 기본값은
 *          등록 시점의 `applyTypeDefaults` 가 따로 넣고 있었다. 생성자 호출을 걷어내면서 적용을
 *          한 곳(`applyTypeDefaults`)으로 모았고, 이제 뿌리 → 파생 순서로 체인 전체를 적용한다.
 *          이 테스트는 그 순서를 고정한다: 기반 노드의 값이 들어가고, 같은 프로퍼티를 파생이
 *          다시 적으면 파생이 이긴다.
 */
SW_TEST_CASE( ComponentDefaultsTest, BaseTypeDefaultsApplyBeforeDerived )
{
    const sw::string defaultsPath =
        sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_component_defaults_chain.xml" );

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

        // 기반(SceneComponent) 노드가 적용됐는가 — 예전에는 여기가 (1,1,1) 이었다.
        const sw::float3 scale = pMesh->getLocalScale();
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( scale._x, 2.0f ) );
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( scale._y, 3.0f ) );
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( scale._z, 4.0f ) );

        // 파생(MeshComponent) 노드도 그대로 적용된다.
        SW_EXPECT_TRUE( sw::MathUtil::nearEqual( pMesh->getBoundsRadius(), 7.5f ) );
    }

    sw::Component::setDefaultGamedataPath( previousPath );
    sw::ComponentDefaults::reloadDefaults();
    sw::FileUtil::removeFile( defaultsPath );
}
