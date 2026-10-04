#include "pch.h"

#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringBuilder.h"

#include "Engine/EngineInitSequence.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /**
     * @brief 단계 본문 대신 불린 순서를 적고, 정해 둔 단계에서 정해 둔 결과를 돌려주는 호스트입니다.
     * @details 표의 줄마다 `<단계>StartupStep` 을 같은 기록 구조체로 둔다(X-macro). 호스트가 구조체를 빠뜨리면 컴파일 오류인 것도 이 모양이 보인다.
     */
    struct StartupStepRecorder
    {
        template <EngineInitStep Step>
        struct RecordingStep
        {
            static EngineInitResult initialize( StartupStepRecorder& recorder )
            {
                recorder._listInitialized.push_back( Step );
                recorder._listInitializeMemoryTag.push_back( MemoryProfiler::getCurrentMemoryTag() );
                recorder._listEvent.push_back( string( "I:" ) + EngineInitSequence::getStepName( Step ) );
                return ( Step == recorder._resultStep ) ? recorder._result : EngineInitResult::Succeeded;
            }
            static void shutdown( StartupStepRecorder& recorder )
            {
                recorder._listShutdown.push_back( Step );
                recorder._listEvent.push_back( string( "S:" ) + EngineInitSequence::getStepName( Step ) );
            }
            static void destroy( StartupStepRecorder& recorder )
            {
                recorder._listDestroyed.push_back( Step );
                recorder._listEvent.push_back( string( "D:" ) + EngineInitSequence::getStepName( Step ) );
            }
        };

#define SW_ENGINE_STARTUP_STEP( Name, ... ) using Name##StartupStep = RecordingStep<EngineInitStep::Name>;
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

    /** @brief 표의 줄을 거꾸로 적은 단계 이름입니다 — 해제는 기동이 어디서 멈췄든 이 순서로 모든 단계를 돈다. */
    constexpr const utf8* kFullDestroyOrder =
        "SceneRhi LiveShader RenderThread FrameRenderer RHI Headless Scene ModuleTypes Input FileIo Audio ModuleImages Task ShaderCache EngineDefaultAssets Resource "
        "Config Reflection Compression";

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

    /** @brief 표에 적힌 줄 순서 그대로의 단계 이름입니다. */
    string joinTableOrder()
    {
        const vector<EngineInitNode> listNode = EngineInitSequence::makeStepNodes();
        vector<uint32>               listIndex;
        for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listNode.size() ); ++nodeIndex )
            listIndex.push_back( nodeIndex );
        return joinNodeOrder( listNode, listIndex );
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
} // namespace

/**
 * @brief [EngineInitSequenceTest] 씬을 읽는 단계(헤드리스 씬 쿠킹)는 모든 타입 공급자가 등록을 끝낸 뒤(`ModuleTypes`)에만 선다
 * @details `Headless` 의 의존 칸이 `ModuleTypes` 를 적고, 그 단계가 실패하면 쿠킹은 돌지 않는다. 쿠킹이 GameFramework · 킷 · 게임 모듈의 타입이
 *          오르기 전에 씬을 읽으면 그 컴포넌트를 `MissingComponent` 로 쿠킹한다. 의존을 빼면 이 시험이 진다.
 */
SW_TEST_CASE( EngineInitSequenceTest, SceneReadingStepWaitsForModuleTypes )
{
    EngineInitGraph graph{};
    string          error;
    SW_ASSERT_TRUE_MSG( EngineInitSequence::computeGraph( EngineInitSequence::makeStepNodes(), graph, error ), error.c_str() );
    SW_EXPECT_TRUE( dependsOnStep( graph, EngineInitStep::Headless, EngineInitStep::ModuleTypes ) );
    // 타입 등록은 리플렉션 · 설정(키트 목록) 뒤다 — 모듈 이미지를 내리는 자리(ModuleImages)보다도 뒤라 종료에서 먼저 내려간다.
    SW_EXPECT_TRUE( dependsOnStep( graph, EngineInitStep::ModuleTypes, EngineInitStep::Reflection ) );
    SW_EXPECT_TRUE( dependsOnStep( graph, EngineInitStep::ModuleTypes, EngineInitStep::ModuleImages ) );

    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    recorder._resultStep = EngineInitStep::ModuleTypes;
    recorder._result     = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.initializeAll( recorder ) );
    for ( const EngineInitStep step : recorder._listInitialized )
    {
        SW_EXPECT_TRUE_MSG( step != EngineInitStep::Headless, "the scene cook ran although module types were not registered" );
    }
    sequence.destroyAll();
}

/**
 * @brief [EngineInitSequenceTest] 표(`EngineInitStepList.xxx`)가 실제 기동 순서대로 적혀 있는지 검증
 * @details 정렬은 의존 칸만 본다(동점은 이름 순, 줄 위치는 보지 않는다). 그 결과가 표의 줄 순서와 같아야 표를 위에서 아래로 기동 순서로
 *          읽을 수 있다. 의존을 빼먹어 계산 순서가 바뀌면(예: Config 의 Reflection) 이 시험이 진다 — 줄을 의존보다 위로 옮기는 것은
 *          컴파일 오류다(static_assert).
 */
SW_TEST_CASE( EngineInitSequenceTest, TableIsWrittenInStartupOrder )
{
    const EngineInitSequence sequence;
    SW_ASSERT_TRUE_MSG( sequence.getError().empty(), sequence.getError().c_str() );
    SW_ASSERT_TRUE( sequence.getInitializeOrder().size() == static_cast<size_t>( EngineInitStep::Count ) );
    SW_EXPECT_STREQ( joinTableOrder().c_str(), joinStepNames( sequence.getInitializeOrder() ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 종료가 초기화의 정확한 역순인지 검증
 * @details 기동을 다 마친 엔진의 종료 순서다: 씬의 디바이스를 떼는 것이 처음, 씬이 입력 · 오디오보다, 입력 · 오디오가 모듈 이미지보다,
 *          모듈 이미지가 태스크보다 먼저 내려간다.
 */
SW_TEST_CASE( EngineInitSequenceTest, ShutdownRunsInReverseOfInitialization )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );
    SW_EXPECT_STREQ( joinTableOrder().c_str(), joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );
    SW_EXPECT_STREQ( "SceneRhi LiveShader RenderThread FrameRenderer RHI Headless Scene ModuleTypes Input FileIo Audio ModuleImages Task ShaderCache EngineDefaultAssets Resource "
                     "Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );

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
    SW_EXPECT_FALSE( sequence.initializeAll( recorder ) );
    SW_EXPECT_STREQ( "Compression Reflection Config Resource EngineDefaultAssets ShaderCache Task ModuleImages Audio FileIo Input ModuleTypes Scene",
                     joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( "ModuleTypes Input FileIo Audio ModuleImages Task ShaderCache EngineDefaultAssets Resource Config Reflection Compression", joinStepNames( recorder._listShutdown ).c_str() );

    // 해제는 실패한 단계(Scene — 종료는 받지 않았다)와 닿지 못한 단계까지 모두 돈다. 부트스트랩이 미리 만든 객체가 있기 때문이다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( kFullDestroyOrder, joinStepNames( recorder._listDestroyed ).c_str() );
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
    SW_EXPECT_TRUE( sequence.initializeAll( recorder ) );
    SW_EXPECT_STREQ( "Compression Reflection Config Resource EngineDefaultAssets ShaderCache Task ModuleImages Audio FileIo Input ModuleTypes Scene Headless",
                     joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( "Headless Scene ModuleTypes Input FileIo Audio ModuleImages Task ShaderCache EngineDefaultAssets Resource Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );

    // 건너뛴 단계(RHI 이후)도 해제는 받는다 — 그 단계의 서비스가 부트스트랩에서 만들어져 있을 수 있다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( kFullDestroyOrder, joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 해제가 표의 모든 단계를 줄의 역순으로, 모든 종료가 끝난 **뒤에** 도는지 검증
 * @details `destroyAll` 은 아직 내리지 않은 단계가 있으면 먼저 `shutdownAll` 을 한다 — 해제된 객체를 다른 단계의 종료가 보는 일이 없다.
 *          `EngineLoop::shutdown` 의 해제 순서가 이 줄이다(렌더러 쪽 → RHI → 씬 → 입력 · 오디오 → 태스크 → … → 압축).
 */
SW_TEST_CASE( EngineInitSequenceTest, DestroyRunsAfterEveryShutdownInReverseTableOrder )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );

    sequence.destroyAll();
    SW_EXPECT_STREQ( kFullDestroyOrder, joinStepNames( recorder._listDestroyed ).c_str() );
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );

    // 종료 열여덟이 모두 해제 열여덟보다 앞이다.
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
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );
    recorder._listInitialized.clear();

    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    SW_EXPECT_STREQ( "SceneRhi LiveShader RenderThread FrameRenderer", joinStepNames( recorder._listShutdown ).c_str() );
    SW_EXPECT_STREQ( "FrameRenderer RenderThread LiveShader SceneRhi", joinStepNames( sequence.getStoppedSteps() ).c_str() );
    SW_EXPECT_TRUE( recorder._listDestroyed.empty() );

    SW_EXPECT_TRUE( sequence.restartStoppedSteps() );
    SW_EXPECT_STREQ( "FrameRenderer RenderThread LiveShader SceneRhi", joinStepNames( recorder._listInitialized ).c_str() );
    SW_EXPECT_TRUE( sequence.getStoppedSteps().empty() );

    recorder._listShutdown.clear();
    sequence.shutdownAll();
    SW_EXPECT_STREQ( "SceneRhi LiveShader RenderThread FrameRenderer RHI Headless Scene ModuleTypes Input FileIo Audio ModuleImages Task ShaderCache EngineDefaultAssets Resource "
                     "Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 다시 세우다 실패하면 거기서 멈추고, 서지 못한 단계는 종료 대상에서 빠지는지 검증
 */
SW_TEST_CASE( EngineInitSequenceTest, FailedRestartLeavesTheRestStopped )
{
    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );

    sequence.shutdownDependentsOf( EngineInitStep::RHI );
    recorder._resultStep = EngineInitStep::RenderThread;
    recorder._result     = EngineInitResult::Failed;
    SW_EXPECT_FALSE( sequence.restartStoppedSteps() );

    recorder._listShutdown.clear();
    sequence.shutdownAll();
    SW_EXPECT_STREQ( "FrameRenderer RHI Headless Scene ModuleTypes Input FileIo Audio ModuleImages Task ShaderCache EngineDefaultAssets Resource Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );
}

/**
 * @brief [EngineInitSequenceTest] 단계 초기화(기동 · 재시작)는 표의 메모리 태그 칸 아래에서 돈다
 * @details 단계 초기화가 잡는 메모리(태스크 큐 · 타입 표 · 디바이스 · 렌더러)가 용도 줄로 세이는 근거다. 시험 스레드의 태그(Unknown)가
 *          아니라 단계마다 표에 적힌 태그여야 하고, 백엔드 교체처럼 단계를 다시 세울 때(`restartStoppedSteps`)도 같다.
 */
SW_TEST_CASE( EngineInitSequenceTest, InitializeRunsUnderTheStepMemoryTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );

    EngineInitSequence  sequence;
    StartupStepRecorder recorder;
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );
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
    SW_EXPECT_TRUE( MemoryProfiler::getCurrentMemoryTag() == MemoryTag::Unknown );
    sequence.destroyAll();
}
