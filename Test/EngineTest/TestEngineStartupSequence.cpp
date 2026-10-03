#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Engine/EngineStartupSequence.h"

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
        template <EngineStartupStep Step>
        struct RecordingStep
        {
            static EngineStartupResult initialize( StartupStepRecorder& recorder )
            {
                recorder._listInitialized.push_back( Step );
                recorder._listEvent.push_back( string( "I:" ) + EngineStartupSequence::getStepName( Step ) );
                return ( Step == recorder._resultStep ) ? recorder._result : EngineStartupResult::Succeeded;
            }
            static void shutdown( StartupStepRecorder& recorder )
            {
                recorder._listShutdown.push_back( Step );
                recorder._listEvent.push_back( string( "S:" ) + EngineStartupSequence::getStepName( Step ) );
            }
            static void destroy( StartupStepRecorder& recorder )
            {
                recorder._listDestroyed.push_back( Step );
                recorder._listEvent.push_back( string( "D:" ) + EngineStartupSequence::getStepName( Step ) );
            }
        };

#define SW_ENGINE_STARTUP_STEP( Name, ... ) using Name##StartupStep = RecordingStep<EngineStartupStep::Name>;
#include "Engine/EngineStartupStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP

        vector<EngineStartupStep> _listInitialized{};
        vector<EngineStartupStep> _listShutdown{};
        vector<EngineStartupStep> _listDestroyed{};
        vector<string>            _listEvent{}; ///< 세 본문을 불린 순서대로 — "I:" 초기화, "S:" 종료, "D:" 해제
        EngineStartupStep         _resultStep{ EngineStartupStep::Count };
        EngineStartupResult       _result{ EngineStartupResult::Succeeded };
    };

    /** @brief 표의 줄을 거꾸로 적은 단계 이름입니다 — 해제는 기동이 어디서 멈췄든 이 순서로 모든 단계를 돈다. */
    constexpr const utf8* kFullDestroyOrder =
        "SceneRhi LiveShader RenderThread FrameRenderer RHI Headless Scene Input Audio ModuleImages Task ShaderCache EngineData Resource "
        "Config Reflection Compression";

    string joinStepNames( const vector<EngineStartupStep>& listStep )
    {
        StringBuilder<constant::kMaxBuffer512> sb;
        for ( const EngineStartupStep step : listStep )
        {
            if ( sb.size() > 0 )
                sb.append( ' ' );
            sb.append( EngineStartupSequence::getStepName( step ) );
        }
        return string( sb.c_str() );
    }

    string joinNodeOrder( const vector<EngineStartupNode>& listNode, const vector<uint32>& listOrder )
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

    vector<EngineStartupStep> makeReversed( const vector<EngineStartupStep>& listStep )
    {
        vector<EngineStartupStep> listReversed( listStep.rbegin(), listStep.rend() );
        return listReversed;
    }

    /** @brief 표에 적힌 줄 순서 그대로의 단계 이름입니다. */
    string joinTableOrder()
    {
        const vector<EngineStartupNode> listNode = EngineStartupSequence::makeStepNodes();
        vector<uint32>                  listIndex;
        for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listNode.size() ); ++nodeIndex )
            listIndex.push_back( nodeIndex );
        return joinNodeOrder( listNode, listIndex );
    }
} // namespace

/**
 * @brief [EngineStartupSequenceTest] 표(`EngineStartupStepList.xxx`)가 실제 기동 순서대로 적혀 있는지 검증
 * @details 정렬은 의존 칸만 본다(동점은 이름 순, 줄 위치는 보지 않는다). 그 결과가 표의 줄 순서와 같아야 표를 위에서 아래로 기동 순서로
 *          읽을 수 있다. 의존을 빼먹어 계산 순서가 바뀌면(예: Config 의 Reflection) 이 시험이 진다 — 줄을 의존보다 위로 옮기는 것은
 *          컴파일 오류다(static_assert).
 */
SW_TEST_CASE( EngineStartupSequenceTest, TableIsWrittenInStartupOrder )
{
    const EngineStartupSequence sequence;
    SW_ASSERT_TRUE_MSG( sequence.getError().empty(), sequence.getError().c_str() );
    SW_ASSERT_TRUE( sequence.getInitializeOrder().size() == static_cast<size_t>( EngineStartupStep::Count ) );
    SW_EXPECT_STREQ( joinTableOrder().c_str(), joinStepNames( sequence.getInitializeOrder() ).c_str() );
}

/**
 * @brief [EngineStartupSequenceTest] 종료가 초기화의 정확한 역순인지 검증
 * @details 기동을 다 마친 엔진의 종료 순서다: 씬의 디바이스를 떼는 것이 처음, 씬이 입력 · 오디오보다, 입력 · 오디오가 모듈 이미지보다,
 *          모듈 이미지가 태스크보다 먼저 내려간다.
 */
SW_TEST_CASE( EngineStartupSequenceTest, ShutdownRunsInReverseOfInitialization )
{
    EngineStartupSequence sequence;
    StartupStepRecorder   recorder;
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );
    SW_EXPECT_STREQ( joinTableOrder().c_str(), joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );
    SW_EXPECT_STREQ( "SceneRhi LiveShader RenderThread FrameRenderer RHI Headless Scene Input Audio ModuleImages Task ShaderCache EngineData Resource "
                     "Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );

    // 두 번 불러도 다시 내리지 않는다.
    sequence.shutdownAll();
    SW_EXPECT_EQUAL( static_cast<size_t>( EngineStartupStep::Count ), recorder._listShutdown.size() );
}

/**
 * @brief [EngineStartupSequenceTest] 단계가 실패하면 거기서 멈추고, 초기화한 단계만 역순으로 내리는지 검증
 */
SW_TEST_CASE( EngineStartupSequenceTest, FailedStepStopsAndShutsDownOnlyInitializedSteps )
{
    EngineStartupSequence sequence;
    StartupStepRecorder   recorder;
    recorder._resultStep = EngineStartupStep::Scene;
    recorder._result     = EngineStartupResult::Failed;
    SW_EXPECT_FALSE( sequence.initializeAll( recorder ) );
    SW_EXPECT_STREQ( "Compression Reflection Config Resource EngineData ShaderCache Task ModuleImages Audio Input Scene",
                     joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( "Input Audio ModuleImages Task ShaderCache EngineData Resource Config Reflection Compression", joinStepNames( recorder._listShutdown ).c_str() );

    // 해제는 실패한 단계(Scene — 종료는 받지 않았다)와 닿지 못한 단계까지 모두 돈다. 부트스트랩이 미리 만든 객체가 있기 때문이다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( kFullDestroyOrder, joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineStartupSequenceTest] `SkipDependents`(헤드리스 작업)가 그 단계에 의존하는 단계만 건너뛰는지 검증
 * @details 셰이더 베이크 · 씬 쿠킹은 RHI · 렌더러 · 렌더 스레드 · 라이브 셰이더 · 씬 디바이스를 세우지 않는다. 건너뛴 단계는 종료도 하지 않는다.
 */
SW_TEST_CASE( EngineStartupSequenceTest, SkipDependentsSkipsEveryDependentStep )
{
    EngineStartupSequence sequence;
    StartupStepRecorder   recorder;
    recorder._resultStep = EngineStartupStep::Headless;
    recorder._result     = EngineStartupResult::SkipDependents;
    SW_EXPECT_TRUE( sequence.initializeAll( recorder ) );
    SW_EXPECT_STREQ( "Compression Reflection Config Resource EngineData ShaderCache Task ModuleImages Audio Input Scene Headless",
                     joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( "Headless Scene Input Audio ModuleImages Task ShaderCache EngineData Resource Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );

    // 건너뛴 단계(RHI 이후)도 해제는 받는다 — 그 단계의 서비스가 부트스트랩에서 만들어져 있을 수 있다.
    sequence.destroyAll();
    SW_EXPECT_STREQ( kFullDestroyOrder, joinStepNames( recorder._listDestroyed ).c_str() );
}

/**
 * @brief [EngineStartupSequenceTest] 해제가 표의 모든 단계를 줄의 역순으로, 모든 종료가 끝난 **뒤에** 도는지 검증
 * @details `destroyAll` 은 아직 내리지 않은 단계가 있으면 먼저 `shutdownAll` 을 한다 — 해제된 객체를 다른 단계의 종료가 보는 일이 없다.
 *          `EngineLoop::shutdown` 의 해제 순서가 이 줄이다(렌더러 쪽 → RHI → 씬 → 입력 · 오디오 → 태스크 → … → 압축).
 */
SW_TEST_CASE( EngineStartupSequenceTest, DestroyRunsAfterEveryShutdownInReverseTableOrder )
{
    EngineStartupSequence sequence;
    StartupStepRecorder   recorder;
    SW_ASSERT_TRUE( sequence.initializeAll( recorder ) );

    sequence.destroyAll();
    SW_EXPECT_STREQ( kFullDestroyOrder, joinStepNames( recorder._listDestroyed ).c_str() );
    SW_EXPECT_STREQ( joinStepNames( makeReversed( recorder._listInitialized ) ).c_str(), joinStepNames( recorder._listShutdown ).c_str() );

    // 종료 열일곱이 모두 해제 열일곱보다 앞이다.
    const size_t stepCount = static_cast<size_t>( EngineStartupStep::Count );
    SW_ASSERT_TRUE( recorder._listEvent.size() == stepCount * 3 );
    for ( size_t eventIndex = 0; eventIndex < recorder._listEvent.size(); ++eventIndex )
    {
        const utf8 expectedKind = ( eventIndex < stepCount ) ? 'I' : ( eventIndex < stepCount * 2 ) ? 'S'
                                                                                                    : 'D';
        SW_EXPECT_TRUE_MSG( recorder._listEvent[eventIndex][0] == expectedKind, recorder._listEvent[eventIndex].c_str() );
    }
}

/**
 * @brief [EngineStartupSequenceTest] 호스트를 받기 전(`initializeAll` 전)의 해제는 아무것도 부르지 않는지 검증
 */
SW_TEST_CASE( EngineStartupSequenceTest, DestroyBeforeInitializeDoesNothing )
{
    EngineStartupSequence sequence;
    sequence.shutdownAll();
    sequence.destroyAll();
    SW_EXPECT_TRUE( sequence.getInitializedSteps().empty() );
}

/**
 * @brief [EngineStartupSequenceTest] 순환은 정렬 오류이고, 순환에 든 노드를 이름으로 알리는지 검증
 */
SW_TEST_CASE( EngineStartupSequenceTest, CycleIsRejected )
{
    const vector<EngineStartupNode> listNode = {
        {"A", "C"},
        {"B", "A"},
        {"C", "B"},
        {"D",  ""}
    };
    EngineStartupGraph graph{};
    string             error;
    SW_EXPECT_FALSE( EngineStartupSequence::computeGraph( listNode, graph, error ) );
    SW_EXPECT_STREQ( "Startup steps in or behind a dependency cycle: A B C", error.c_str() );
    SW_EXPECT_EMPTY( graph._listOrder );
}

/**
 * @brief [EngineStartupSequenceTest] 모르는 의존 이름 · 같은 이름 두 번은 정렬 오류인지 검증
 */
SW_TEST_CASE( EngineStartupSequenceTest, UnknownOrDuplicateNameIsRejected )
{
    EngineStartupGraph graph{};
    string             error;
    SW_EXPECT_FALSE( EngineStartupSequence::computeGraph( {
                                                              { "A", "Missing" }
    },
                                                          graph, error ) );
    SW_EXPECT_STREQ( "Startup step 'A' depends on unknown step 'Missing'", error.c_str() );

    error.clear();
    SW_EXPECT_FALSE( EngineStartupSequence::computeGraph( {
                                                              {"A", ""},
                                                              {"A", ""}
    },
                                                          graph, error ) );
    SW_EXPECT_STREQ( "Startup step 'A' is declared twice", error.c_str() );
}

/**
 * @brief [EngineStartupSequenceTest] 의존이 순서를 정하고, 의존이 없는 노드끼리는 이름 순으로 가는지 검증(목록 순서는 보지 않는다)
 * @details 목록은 이름과 거꾸로 두고, 의존 글은 표의 꼴(`{ A, B }`)과 공백 꼴을 섞는다.
 */
SW_TEST_CASE( EngineStartupSequenceTest, DependenciesOrderAndNameBreaksTies )
{
    const vector<EngineStartupNode> listNode = {
        {"D", "{ A, B }"},
        {"C",         ""},
        {"B",       "{}"},
        {"A",      " C "}
    };
    EngineStartupGraph graph{};
    string             error;
    SW_ASSERT_TRUE_MSG( EngineStartupSequence::computeGraph( listNode, graph, error ), error.c_str() );
    SW_EXPECT_STREQ( "B C A D", joinNodeOrder( listNode, graph._listOrder ).c_str() );
}
