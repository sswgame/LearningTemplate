#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/EngineInitSequence.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /**
     * @brief 단계 본문 대신 불린 순서를 적고, 정해 둔 단계에서 정해 둔 결과를 돌려주는 호스트입니다.
     * @details 호스트 자신이 본문 모음이다 — 표의 줄마다 `initialize<단계>` · `shutdown<단계>` · `destroy<단계>` 를 X-macro 로 모두 둔다.
     */
    struct StartupStepRecorder
    {
        EngineInitResult recordInitialize( EngineInitStep step )
        {
            _listInitialized.push_back( step );
            _listInitializeMemoryTag.push_back( Memory::getCurrentMemoryTag() );
            _listEvent.push_back( string( "I:" ) + EngineInitSequence::getStepName( step ) );
            return ( step == _resultStep ) ? _result : EngineInitResult::Succeeded;
        }
        void recordShutdown( EngineInitStep step )
        {
            _listShutdown.push_back( step );
            _listEvent.push_back( string( "S:" ) + EngineInitSequence::getStepName( step ) );
        }
        void recordDestroy( EngineInitStep step )
        {
            _listDestroyed.push_back( step );
            _listEvent.push_back( string( "D:" ) + EngineInitSequence::getStepName( step ) );
        }

#define SW_ENGINE_STARTUP_STEP( Name, ... )                                   \
    static EngineInitResult initialize##Name( StartupStepRecorder& recorder ) \
    {                                                                         \
        return recorder.recordInitialize( EngineInitStep::Name );             \
    }                                                                         \
    static void shutdown##Name( StartupStepRecorder& recorder )               \
    {                                                                         \
        recorder.recordShutdown( EngineInitStep::Name );                      \
    }                                                                         \
    static void destroy##Name( StartupStepRecorder& recorder )                \
    {                                                                         \
        recorder.recordDestroy( EngineInitStep::Name );                       \
    }
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP

        vector<EngineInitStep> _listInitialized{};
        vector<MemoryTag>      _listInitializeMemoryTag{}; ///< 초기화 본문이 불린 동안의 할당 태그(`_listInitialized` 와 같은 순서)
        vector<EngineInitStep> _listShutdown{};
        vector<EngineInitStep> _listDestroyed{};
        vector<string>         _listEvent{}; ///< 세 본문을 불린 순서대로 — "I:" 초기화, "S:" 종료, "D:" 해제
        EngineInitStep         _resultStep{ EngineInitStep::Count };
        EngineInitResult       _result{ EngineInitResult::Succeeded };
    };

    /** @brief 본문을 두 단계에만 둔 본문 모음입니다 — 나머지 자리는 `EngineInitNoStepBody` 의 null 이다. */
    struct PartialStepBody : EngineInitNoStepBody
    {
        static EngineInitResult initializeScene( StartupStepRecorder& recorder ) { return recorder.recordInitialize( EngineInitStep::Scene ); }
        static void             destroyTask( StartupStepRecorder& recorder ) { recorder.recordDestroy( EngineInitStep::Task ); }
    };

    string joinStepNames( const vector<EngineInitStep>& listStep )
    {
        StringBuilder<constant::kMaxBuffer512> sb;
        for ( const EngineInitStep step : listStep )
        {
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( EngineInitSequence::getStepName( step ) );
        }
        return string( sb.c_str() );
    }

    string joinNodeOrder( const vector<EngineInitNode>& listNode, const vector<uint32>& listOrder )
    {
        StringBuilder<constant::kMaxBuffer256> sb;
        for ( const uint32 nodeIndex : listOrder )
        {
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( listNode[nodeIndex]._pName );
        }
        return string( sb.c_str() );
    }

    vector<EngineInitStep> makeReversed( const vector<EngineInitStep>& listStep )
    {
        vector<EngineInitStep> listReversed( listStep.rbegin(), listStep.rend() );
        return listReversed;
    }

    /** @brief 표의 의존 그래프입니다. */
    EngineInitGraph makeTableGraph()
    {
        EngineInitGraph graph{};
        string          error;
        (void)EngineInitSequence::computeGraph( EngineInitSequence::makeStepNodes(), graph, error ); // 표 오류는 컴파일 검사가 막고, ComputedOrder… 테스트가 다시 본다
        return graph;
    }

    /** @brief @p step 에서 의존 칸을 (간접으로라도) 거슬러 @p dependency 에 닿는가 — @p dependency 가 @p step 보다 먼저 서야 하는가. */
    bool dependsOnStep( const EngineInitGraph& graph, EngineInitStep step, EngineInitStep dependency )
    {
        vector<uint32> listPending{ static_cast<uint32>( step ) };
        vector<uint8>  listVisited( graph._listDependency.size(), SW_FALSE );
        while ( listPending.empty() == false )
        {
            const uint32 nodeIndex = listPending.back();
            listPending.pop_back();
            for ( const uint32 dependencyIndex : graph._listDependency[nodeIndex] )
            {
                if ( dependencyIndex == static_cast<uint32>( dependency ) )
                    return true;
                if ( listVisited[dependencyIndex] == SW_FALSE )
                {
                    listVisited[dependencyIndex] = SW_TRUE;
                    listPending.push_back( dependencyIndex );
                }
            }
        }
        return false;
    }

    /** @brief 초기화 순서에서 @p step 까지(포함)의 앞부분입니다. */
    vector<EngineInitStep> makeOrderPrefix( const vector<EngineInitStep>& listOrder, EngineInitStep step )
    {
        vector<EngineInitStep> listPrefix;
        for ( const EngineInitStep orderStep : listOrder )
        {
            listPrefix.push_back( orderStep );
            if ( orderStep == step )
                break;
        }
        return listPrefix;
    }

    /** @brief 초기화 순서에서 @p step 에 (간접으로라도) 의존하는 단계를 뺀 것입니다. @p bKeepDependents 면 거꾸로 그 단계만 남깁니다. */
    vector<EngineInitStep> filterDependentsOf( const EngineInitGraph& graph, const vector<EngineInitStep>& listOrder, EngineInitStep step, bool bKeepDependents )
    {
        vector<EngineInitStep> listFiltered;
        for ( const EngineInitStep orderStep : listOrder )
        {
            if ( dependsOnStep( graph, orderStep, step ) == bKeepDependents )
                listFiltered.push_back( orderStep );
        }
        return listFiltered;
    }
} // namespace

/**
 * @brief [EngineInitSequenceTest] 씬을 읽는 단계(헤드리스 씬 쿠킹)는 모든 타입 공급자가 등록을 끝낸 뒤(`ModuleTypes`)에만 선다
 * @details `Headless` 의 의존 칸이 `ModuleTypes` 를 적고, 그 단계가 실패하면 쿠킹은 돌지 않는다. 쿠킹이 GameFramework · 킷 · 게임 모듈의 타입이
 *          오르기 전에 씬을 읽으면 그 컴포넌트를 `MissingComponent` 로 쿠킹한다. 의존을 빼면 이 테스트가 진다.
 */
SW_TEST_CASE( EngineInitSequenceTest, SceneReadingStepWaitsForModuleTypes )
{
    const EngineInitGraph graph = makeTableGraph();
    SW_EXPECT_TRUE( dependsOnStep( graph, EngineInitStep::Headless, EngineInitStep::ModuleTypes ) );
    // 타입 등록은 리플렉션 · 설정(키트 목록) 뒤다 — 모듈 이미지를 내리는 자리(ModuleImages)보다도 뒤라 종료에서 먼저 내려간다.
    SW_EXPECT_TRUE( dependsOnStep( graph, EngineInitStep::ModuleTypes, EngineInitStep::Reflection ) );
    SW_EXPECT_TRUE( dependsOnStep( graph, EngineInitStep::ModuleTypes, EngineInitStep::ModuleImages ) );

    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    recorder._resultStep = EngineInitStep::ModuleTypes;
    recorder._result     = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    for ( const EngineInitStep step : recorder._listInitialized )
    {
        SW_EXPECT_TRUE_MSG( step != EngineInitStep::Headless, "the scene cook ran although module types were not registered" );
    }
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 초기화 순서는 의존에서 계산되고, 결정적이며, 모든 의존이 앞에 선다
 * @details 순서의 진실은 의존 칸 하나다 — 표의 줄 위치는 보지 않는다. 같은 표를 거꾸로 넘겨도 같은 순서가 나와야 하고(동점은 이름 순),
 *          어느 단계든 그 의존은 순서에서 그보다 앞이다.
 */
SW_TEST_CASE( EngineInitSequenceTest, ComputedOrderIsDeterministicAndDependenciesComeFirst )
{
    const EngineInitSequence sequence;
    SW_ASSERT_TRUE_MSG( sequence.getError().empty(), sequence.getError().c_str() );
    const vector<EngineInitStep>& listOrder = sequence.getInitializeOrder();
    SW_ASSERT_TRUE( listOrder.size() == static_cast<size_t>( EngineInitStep::Count ) );

    // 다시 만들어도 같다.
    const EngineInitSequence other;
    SW_EXPECT_STREQ( joinStepNames( listOrder ).c_str(), joinStepNames( other.getInitializeOrder() ).c_str() );

    // 줄을 거꾸로 넘겨도 같다 — 줄 위치는 순서에 뜻이 없다.
    const vector<EngineInitNode> listNode = EngineInitSequence::makeStepNodes();
    const vector<EngineInitNode> listReversedNode( listNode.rbegin(), listNode.rend() );
    EngineInitGraph              reversedGraph{};
    string                       error;
    SW_ASSERT_TRUE_MSG( EngineInitSequence::computeGraph( listReversedNode, reversedGraph, error ), error.c_str() );
    SW_EXPECT_STREQ( joinStepNames( listOrder ).c_str(), joinNodeOrder( listReversedNode, reversedGraph._listOrder ).c_str() );

    // 모든 의존이 앞에 선다.
    const EngineInitGraph graph = makeTableGraph();
    vector<uint32>        listPosition( listOrder.size(), 0 );
    for ( uint32 position = 0; position < static_cast<uint32>( listOrder.size() ); ++position )
    {
        listPosition[static_cast<uint32>( listOrder[position] )] = position;
    }
    for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( EngineInitStep::Count ); ++stepIndex )
    {
        for ( const uint32 dependencyIndex : graph._listDependency[stepIndex] )
        {
            SW_EXPECT_TRUE_MSG( listPosition[dependencyIndex] < listPosition[stepIndex], EngineInitSequence::getStepName( static_cast<EngineInitStep>( stepIndex ) ) );
        }
    }
}

/**
 * @brief [EngineInitSequenceTest] 종료가 초기화의 정확한 역순인지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, ShutdownRunsInReverseOfInitialization )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    SW_EXPECT_STREQ( joinStepNames( sequence.getInitializeOrder() ).c_str(), joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );

    // 두 번 불러도 다시 내리지 않는다.
    sequence.shutdownAll();
    SW_EXPECT_EQUAL( static_cast<size_t>( EngineInitStep::Count ), recorder._listShutdown.size() );
}

/**
 * @brief [EngineInitSequenceTest] 단계가 실패하면 거기서 멈추고, 초기화한 단계만 역순으로 내리는지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, FailedStepStopsAndShutsDownOnlyInitializedSteps )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    recorder._resultStep = EngineInitStep::Scene;
    recorder._result     = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    vector<EngineInitStep> listExpected = makeOrderPrefix( sequence.getInitializeOrder(), EngineInitStep::Scene );
    SW_EXPECT_STREQ( joinStepNames( listExpected ).c_str(), joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    listExpected.pop_back(); // 실패한 Scene 은 종료를 받지 않는다
    SW_EXPECT_STREQ( joinStepNames( makeReversed( listExpected ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );

    // 해제는 실패한 단계(Scene — 종료는 받지 않았다)와 닿지 못한 단계까지 모두 돈다. 부트스트랩이 미리 만든 객체가 있기 때문이다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( sequence.getInitializeOrder() ) ).c_str(), joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] `SkipDependents`(헤드리스 작업)가 그 단계에 의존하는 단계만 건너뛰는지 검증
 * @details 셰이더 쿠킹 · 씬 쿠킹은 RHI · 렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬 디바이스를 세우지 않는다. 건너뛴 단계는 종료도 하지 않는다.
 */
SW_TEST_CASE( EngineInitSequenceTest, SkipDependentsSkipsEveryDependentStep )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    recorder._resultStep = EngineInitStep::Headless;
    recorder._result     = EngineInitResult::SkipDependents;
    SW_EXPECT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    const EngineInitGraph        graph        = makeTableGraph();
    const vector<EngineInitStep> listExpected = filterDependentsOf( graph, sequence.getInitializeOrder(), EngineInitStep::Headless, false );
    SW_EXPECT_STREQ( joinStepNames( listExpected ).c_str(), joinStepNames( recorder._listInitialized ).c_str() );
    for ( const EngineInitStep step : { EngineInitStep::RHI, EngineInitStep::FrameRenderer, EngineInitStep::RenderThread, EngineInitStep::LiveShader, EngineInitStep::SceneRHI } )
    {
        SW_EXPECT_TRUE_MSG( dependsOnStep( graph, step, EngineInitStep::Headless ), EngineInitSequence::getStepName( step ) );
    }

    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( listExpected ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );

    // 건너뛴 단계(RHI 이후)도 해제는 받는다 — 그 단계의 서비스가 부트스트랩에서 만들어져 있을 수 있다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( sequence.getInitializeOrder() ) ).c_str(), joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 해제가 모든 단계를 초기화 순서의 역순으로, 모든 종료가 끝난 **뒤에** 도는지 검증
 * @details `destroyAll` 은 아직 내리지 않은 단계가 있으면 먼저 `shutdownAll` 을 한다 — 해제된 객체를 다른 단계의 종료가 보는 일이 없다.
 */
SW_TEST_CASE( EngineInitSequenceTest, DestroyRunsAfterEveryShutdownInReverseOrder )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );

    sequence.destroyAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( sequence.getInitializeOrder() ) ).c_str(), joinStepNames( recorder._listDestroyed ).c_str() );
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );

    // 종료가 모두 해제보다 앞이다.
    const size_t stepCount = static_cast<size_t>( EngineInitStep::Count );
    SW_ASSERT_TRUE( recorder._listEvent.size() == stepCount * 3 );
    for ( size_t eventIndex = 0; eventIndex < recorder._listEvent.size(); ++eventIndex )
    {
        const utf8 expectedKind = ( eventIndex < stepCount ) ? 'I' : ( eventIndex < stepCount * 2 ) ? 'S'
                                                                                                    : 'D';
        SW_EXPECT_TRUE_MSG( recorder._listEvent[eventIndex][0] == expectedKind, recorder._listEvent[eventIndex].c_str() );
    }
}

/**
 * @brief [EngineInitSequenceTest] 본문 모음이 정의하지 않은 자리는 부르지 않고, 순서 · 판정은 그대로인지 검증
 * @details 단계를 더할 때 호스트마다 타입을 짓지 않는다 — 본문 모음(`EngineInitNoStepBody` 상속)이 필요한 함수만 둔다.
 */
SW_TEST_CASE( EngineInitSequenceTest, MissingBodiesAreSkipped )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<PartialStepBody>( recorder ) );
    SW_EXPECT_STREQ( "Scene", joinStepNames( recorder._listInitialized ).c_str() );
    // 본문이 없는 단계도 초기화된 것으로 친다 — 종료 · 재시작 판정이 본문 유무에 따라 달라지지 않는다.
    SW_EXPECT_EQUAL( static_cast<size_t>( EngineInitStep::Count ), sequence.getInitializedSteps().size() );

    sequence.destroyAll();
    SW_EXPECT_TRUE( recorder._listShutdown.empty() );
    SW_EXPECT_STREQ( "Task", joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 호스트를 받기 전(`initializeAll` 전)의 해제는 아무것도 부르지 않는지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, DestroyBeforeInitializeDoesNothing )
{
    EngineInitSequence sequence;
    sequence.shutdownAll();
    sequence.destroyAll();
    SW_EXPECT_TRUE( sequence.getInitializedSteps().empty() );
}

/**
 * @brief [EngineInitSequenceTest] 순환은 정렬 오류이고, 순환에 든 노드를 이름으로 알리는지 검증
 * @details 표(`EngineInitStepList.xxx`)의 순환은 컴파일 오류다(`EngineInitTableCheck::isAcyclic`). 이것은 같은 판정의 실행 시간 쪽이다.
 */
SW_TEST_CASE( EngineInitSequenceTest, CycleIsRejected )
{
    const vector<EngineInitNode> listNode = {
        {"A", "C"},
        {"B", "A"},
        {"C", "B"},
        {"D",  ""}
    };
    EngineInitGraph graph{};
    string          error;
    SW_EXPECT_FALSE( EngineInitSequence::computeGraph( listNode, graph, error ) );
    SW_EXPECT_STREQ( "Startup steps in or behind a dependency cycle: A B C", error.c_str() );
    SW_EXPECT_EMPTY( graph._listOrder );
}

/**
 * @brief [EngineInitSequenceTest] 모르는 의존 이름 · 같은 이름 두 번은 정렬 오류인지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, UnknownOrDuplicateNameIsRejected )
{
    EngineInitGraph graph{};
    string          error;
    SW_EXPECT_FALSE( EngineInitSequence::computeGraph( {
                                                           { "A", "Missing" }
    },
                                                       graph, error ) );
    SW_EXPECT_STREQ( "Startup step 'A' depends on unknown step 'Missing'", error.c_str() );

    error.clear();
    SW_EXPECT_FALSE( EngineInitSequence::computeGraph( {
                                                           {"A", ""},
                                                           {"A", ""}
    },
                                                       graph, error ) );
    SW_EXPECT_STREQ( "Startup step 'A' is declared twice", error.c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 의존이 순서를 정하고, 의존이 없는 노드끼리는 이름 순으로 가는지 검증(목록 순서는 보지 않는다)
 * @details 목록은 이름과 거꾸로 두고, 의존 글은 표의 꼴(`{ A, B }`)과 공백 꼴을 섞는다.
 */
SW_TEST_CASE( EngineInitSequenceTest, DependenciesOrderAndNameBreaksTies )
{
    const vector<EngineInitNode> listNode = {
        {"D", "{ A, B }"},
        {"C",         ""},
        {"B",       "{}"},
        {"A",      " C "}
    };
    EngineInitGraph graph{};
    string          error;
    SW_ASSERT_TRUE_MSG( EngineInitSequence::computeGraph( listNode, graph, error ), error.c_str() );
    SW_EXPECT_STREQ( "B C A D", joinNodeOrder( listNode, graph._listOrder ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 한 단계에 (간접으로라도) 의존하는 단계만 역순으로 내리고, 같은 본문으로 다시 세우는지 검증
 * @details 백엔드 교체가 이 길을 탄다: RHI 의 디바이스를 갈아 끼우는 동안 렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬의 디바이스만 내렸다가
 *          다시 세운다. 다시 세운 뒤의 종료 순서는 처음 기동한 것과 같아야 한다.
 */
SW_TEST_CASE( EngineInitSequenceTest, DependentsOfAStepRestartWithTheSameBodies )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    recorder._listInitialized.clear();

    const vector<EngineInitStep> listDependent = filterDependentsOf( makeTableGraph(), sequence.getInitializeOrder(), EngineInitStep::RHI, true );
    SW_EXPECT_STREQ( "FrameRenderer RenderThread LiveShader SceneRHI", joinStepNames( listDependent ).c_str() );

    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    SW_EXPECT_STREQ( joinStepNames( makeReversed( listDependent ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );
    SW_EXPECT_STREQ( joinStepNames( listDependent ).c_str(), joinStepNames( sequence.getStoppedSteps() ).c_str() );
    SW_EXPECT_TRUE( recorder._listDestroyed.empty() );

    SW_EXPECT_TRUE( sequence.restartStoppedSteps() );
    SW_EXPECT_STREQ( joinStepNames( listDependent ).c_str(), joinStepNames( recorder._listInitialized ).c_str() );
    SW_EXPECT_TRUE( sequence.getStoppedSteps().empty() );

    recorder._listShutdown.clear();
    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( sequence.getInitializeOrder() ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 다시 세우다 실패하면 거기서 멈추고, 서지 못한 단계는 종료 대상에서 빠지는지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, FailedRestartLeavesTheRestStopped )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );

    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    const vector<EngineInitStep> listStopped = sequence.getStoppedSteps();
    recorder._resultStep                     = EngineInitStep::RenderThread;
    recorder._result                         = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.restartStoppedSteps() );

    // 실패한 RenderThread 와 그 뒤로 다시 세우지 못한 단계는 종료 대상이 아니다.
    vector<EngineInitStep> listNotRestarted;
    bool                   bReachedFailure = false;
    for ( const EngineInitStep step : listStopped )
    {
        bReachedFailure = bReachedFailure || step == EngineInitStep::RenderThread;
        if ( bReachedFailure )
            listNotRestarted.push_back( step );
    }
    vector<EngineInitStep> listExpected;
    for ( const EngineInitStep step : makeReversed( sequence.getInitializeOrder() ) )
    {
        bool bNotRestarted = false;
        for ( const EngineInitStep notRestarted : listNotRestarted )
        {
            bNotRestarted = bNotRestarted || step == notRestarted;
        }
        if ( bNotRestarted == false )
            listExpected.push_back( step );
    }

    recorder._listShutdown.clear();
    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( listExpected ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 단계 초기화(기동 · 재시작)는 표의 메모리 태그 칸 아래에서 돈다
 * @details 단계 초기화가 잡는 메모리(태스크 큐 · 타입 표 · 디바이스 · 렌더러)가 용도 줄로 세이는 근거다. 테스트 스레드의 태그(Unknown)가
 *          아니라 단계마다 표에 적힌 태그여야 하고, 백엔드 교체처럼 단계를 다시 세울 때(`restartStoppedSteps`)도 같다.
 */
SW_TEST_CASE( EngineInitSequenceTest, InitializeRunsUnderTheStepMemoryTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );

    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    SW_ASSERT_TRUE( sequence.restartStoppedSteps() );
    SW_ASSERT_TRUE( recorder._listInitialized.size() > static_cast<size_t>( EngineInitStep::Count ) );
    SW_ASSERT_EQUAL( recorder._listInitialized.size(), recorder._listInitializeMemoryTag.size() );

    uint32 wrongCount{ 0 };
    for ( size_t order = 0; order < recorder._listInitialized.size(); ++order )
    {
        if ( recorder._listInitializeMemoryTag[order] != EngineInitSequence::getStepMemoryTag( recorder._listInitialized[order] ) )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
    SW_EXPECT_TRUE( Memory::getCurrentMemoryTag() == MemoryTag::Unknown );
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 전용 서버(대상 Server)는 창 · RHI · 렌더러 · 플레이어 설정 · 텔레메트리 · 글꼴 단계를 돌리지 않고, 씬 · 씬 쿠킹까지는 선다
 * @details 서버에 GPU 가 없으므로 RHI 단계가 돌면 기동이 실패한다. `initializeAllInternal` 의 대상 검사를 빼면 진다.
 *          해제는 기동이 어디까지 갔든 모든 단계를 돈다(클라이언트 단계의 객체는 만들지 않았으니 null 안전한 해제가 아무것도 하지 않는다).
 */
SW_TEST_CASE( EngineInitSequenceTest, DedicatedServerSkipsClientSteps )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder, EngineInitTarget::Server ) );

    const EngineInitStep arrClientOnly[]   = { EngineInitStep::UserSettings, EngineInitStep::RHI, EngineInitStep::FrameRenderer, EngineInitStep::RenderThread,
                                               EngineInitStep::LiveShader, EngineInitStep::SceneRHI, EngineInitStep::Telemetry, EngineInitStep::Fonts, EngineInitStep::UI };
    bool                 bSceneInitialized = false;
    for ( const EngineInitStep step : recorder._listInitialized )
    {
        bSceneInitialized = bSceneInitialized || step == EngineInitStep::Scene;
        for ( const EngineInitStep clientStep : arrClientOnly )
        {
            SW_EXPECT_TRUE_MSG( step != clientStep, EngineInitSequence::getStepName( step ) );
        }
    }
    SW_EXPECT_TRUE( bSceneInitialized );
    SW_EXPECT_EQUAL( static_cast<size_t>( EngineInitStep::Count ) - SW_COUNT_OF( arrClientOnly ), recorder._listInitialized.size() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );
    sequence.destroyAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( sequence.getInitializeOrder() ) ).c_str(), joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 클라이언트(App · 테스트 하네스)는 모든 단계를 돌린다 — 대상 칸이 기본 기동을 바꾸지 않는다
 */
SW_TEST_CASE( EngineInitSequenceTest, ClientRunsEveryStep )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll<StartupStepRecorder>( recorder ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( EngineInitStep::Count ), recorder._listInitialized.size() );
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 단계가 도는 대상마다 그 의존도 돈다(컴파일 검사 `areTargetsCovered` 를 실행 시간에 다시 본다 — 검사가 빠져도 이 테스트가 남는다)
 */
SW_TEST_CASE( EngineInitSequenceTest, EveryStepRunsWhereItsDependenciesRun )
{
    const EngineInitGraph graph = makeTableGraph();
    for ( uint32 stepIndex = 0; stepIndex < static_cast<uint32>( EngineInitStep::Count ); ++stepIndex )
    {
        const uint8 selfMask = static_cast<uint8>( EngineInitSequence::getStepTarget( static_cast<EngineInitStep>( stepIndex ) ) );
        for ( const uint32 dependencyIndex : graph._listDependency[stepIndex] )
        {
            const uint8 dependencyMask = static_cast<uint8>( EngineInitSequence::getStepTarget( static_cast<EngineInitStep>( dependencyIndex ) ) );
            SW_EXPECT_TRUE_MSG( ( selfMask & static_cast<uint8>( ~dependencyMask ) ) == 0, EngineInitSequence::getStepName( static_cast<EngineInitStep>( stepIndex ) ) );
        }
    }
}
