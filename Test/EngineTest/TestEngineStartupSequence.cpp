#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Engine/EngineStartupSequence.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 단계 본문 대신 불린 순서를 적고, 정해 둔 단계에서 정해 둔 결과를 돌려줍니다. */
    struct StartupStepRecorder
    {
        vector<EngineStartupStep> _listInitialized{};
        vector<EngineStartupStep> _listShutdown{};
        EngineStartupStep         _resultStep{ EngineStartupStep::Count };
        EngineStartupResult       _result{ EngineStartupResult::Succeeded };

        EngineStartupResult initializeStep( EngineStartupStep step )
        {
            _listInitialized.push_back( step );
            return ( step == _resultStep ) ? _result : EngineStartupResult::Succeeded;
        }

        void shutdownStep( EngineStartupStep step ) { _listShutdown.push_back( step ); }

        bool runInitialize( EngineStartupSequence& sequence )
        {
            return sequence.initializeAll( SW_DELEGATE_METHOD( EngineStartupSequence::InitializeStepDelegate, &StartupStepRecorder::initializeStep, this ),
                                           SW_DELEGATE_METHOD( EngineStartupSequence::ShutdownStepDelegate, &StartupStepRecorder::shutdownStep, this ) );
        }
    };

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

    /** @brief 표의 초기화 순서입니다. 의존 칸을 바꾸면 이 줄이 바뀌어야 합니다 — 바뀐 줄이 곧 순서 변경의 리뷰 대상입니다. */
    constexpr const utf8* kExpectedInitializeOrder = "Compression Reflection Config Resource EngineData ShaderCache Task ModuleImages Audio Input Scene Headless "
                                                     "RHI FrameRenderer RenderThread LiveShader SceneRhi";
} // namespace

/**
 * @brief [EngineStartupSequenceTest] 표(`EngineStartupStepList.xxx`)의 위상 순서가 기록해 둔 순서와 같은지 검증
 * @details 표는 이름 순이고 의존이 없는 단계끼리는 줄 순서로 가므로, 이 순서는 **의존 칸만으로** 나온다. 의존 하나를 빼면
 *          (예: Config 의 Reflection) 순서가 바뀌어 이 시험이 진다.
 */
SW_TEST_CASE( EngineStartupSequenceTest, InitializeOrderMatchesDump )
{
    const EngineStartupSequence sequence;
    SW_ASSERT_TRUE_MSG( sequence.getError().empty(), sequence.getError().c_str() );
    SW_EXPECT_STREQ( kExpectedInitializeOrder, joinStepNames( sequence.getInitializeOrder() ).c_str() );
}

/**
 * @brief [EngineStartupSequenceTest] 표의 줄이 이름 순인지 검증
 * @details 줄을 초기화 순서대로 적으면 의존 칸이 빠져도 순서가 그대로라 아무것도 잡지 못한다. 이름 순이라 줄 위치는 순서에 뜻이 없다.
 */
SW_TEST_CASE( EngineStartupSequenceTest, StepListIsSortedByName )
{
    const vector<EngineStartupNode> listNode = EngineStartupSequence::makeStepNodes();
    SW_ASSERT_TRUE( listNode.size() == static_cast<size_t>( EngineStartupStep::Count ) );
    for ( size_t nodeIndex = 1; nodeIndex < listNode.size(); ++nodeIndex )
        SW_EXPECT_TRUE_MSG( string_view{ listNode[nodeIndex - 1]._pName } < string_view{ listNode[nodeIndex]._pName }, listNode[nodeIndex]._pName );
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
    SW_ASSERT_TRUE( recorder.runInitialize( sequence ) );
    SW_EXPECT_STREQ( kExpectedInitializeOrder, joinStepNames( recorder._listInitialized ).c_str() );

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
    SW_EXPECT_FALSE( recorder.runInitialize( sequence ) );
    SW_EXPECT_STREQ( "Compression Reflection Config Resource EngineData ShaderCache Task ModuleImages Audio Input Scene",
                     joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( "Input Audio ModuleImages Task ShaderCache EngineData Resource Config Reflection Compression", joinStepNames( recorder._listShutdown ).c_str() );
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
    SW_EXPECT_TRUE( recorder.runInitialize( sequence ) );
    SW_EXPECT_STREQ( "Compression Reflection Config Resource EngineData ShaderCache Task ModuleImages Audio Input Scene Headless",
                     joinStepNames( recorder._listInitialized ).c_str() );

    sequence.shutdownAll();
    SW_EXPECT_STREQ( "Headless Scene Input Audio ModuleImages Task ShaderCache EngineData Resource Config Reflection Compression",
                     joinStepNames( recorder._listShutdown ).c_str() );
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
 * @brief [EngineStartupSequenceTest] 의존이 순서를 정하고, 의존이 없는 노드끼리는 목록 순서로 가는지 검증
 */
SW_TEST_CASE( EngineStartupSequenceTest, DependenciesOrderAndListBreaksTies )
{
    const vector<EngineStartupNode> listNode = {
        {"A",        "C"},
        {"B",         ""},
        {"C",         ""},
        {"D", "  A   B "}
    };
    EngineStartupGraph graph{};
    string             error;
    SW_ASSERT_TRUE_MSG( EngineStartupSequence::computeGraph( listNode, graph, error ), error.c_str() );
    SW_EXPECT_STREQ( "B C A D", joinNodeOrder( listNode, graph._listOrder ).c_str() );
}
