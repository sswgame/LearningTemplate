#include "pch.h"

#include "Engine/Automation/AutomationRunTitle.h"
#include "Engine/Utility/EngineFrameObserver.h"
#include "Engine/Window/IWindowTitleTarget.h"

#include "TestFramework/TestFramework.h"

namespace
{
    /** @brief 창 대신 제목 거는 횟수와 마지막 제목을 세는 가짜 이음매입니다. */
    class FakeTitleTarget final : public sw::IWindowTitleTarget
    {
    public:
        const utf8*       getCurrentTitle() const override { return _current.c_str(); }
        const sw::string& getBaseTitle() const override { return _base; }
        void              applyWindowTitle( const utf8* pTitle ) override
        {
            ++_applyCount;
            _current = pTitle;
        }

        sw::string _current{ "Game" };
        sw::string _base{};
        int32      _applyCount{ 0 };
    };

    /** @brief 시나리오가 도는 것처럼 보이게 하는 가짜 관찰자입니다. */
    class FakeRunObserver final : public sw::IEngineFrameObserver
    {
    public:
        void               onFrameBegin( const sw::EngineFrameContext& ) override {}
        int32              onFrameEnd( const sw::EngineFrameContext& ) override { return kRunning; }
        int32              onWindowClosed( const sw::EngineFrameContext& ) override { return kRunning; }
        void               onBuildRenderPacket( sw::EngineRenderPacketContext& ) override {}
        [[nodiscard]] bool tryGetRunStatus( sw::string& outScenarioName, sw::string& outStepKind, uint32& outStepNumber, uint32& outFrame ) const override
        {
            if ( _bRunning == false )
                return false;
            outScenarioName = "boot";
            outStepKind     = "Expect";
            outStepNumber   = _stepNumber;
            outFrame        = 1;
            return true;
        }

        bool   _bRunning{ true };
        uint32 _stepNumber{ 1 };
    };

    void tickFrames( sw::AutomationRunTitleTracker& tracker, FakeTitleTarget& target, FakeRunObserver& observer, int32 count )
    {
        sw::vector<sw::IEngineFrameObserver*> listObserver{ &observer };
        for ( int32 index = 0; index < count; ++index )
        {
            tracker.onFrameEnd( &target, nullptr, "dx12", listObserver );
        }
    }
} // namespace

// 시나리오 · 벤치 실행 정보 글 — 창 제목과 단계 로그가 같은 함수로 만든 글을 쓴다(창이나 로그 없이 글자만 본다).

/**
 * @brief [AutomationRunTitleTest] 시나리오 요약은 이름 · 백엔드 · 단계 · 프레임을 순서대로 적는다
 */
SW_TEST_CASE( AutomationRunTitleTest, ScenarioSummaryHasNameBackendStepAndFrame )
{
    sw::AutomationRunTitleInput input;
    input._scenarioName = "shooter3d/weapons";
    input._backendName  = "DirectX12";
    input._stepKind     = "Expect";
    input._stepNumber   = 7;
    input._frame        = 120;
    SW_EXPECT_STREQ( "shooter3d/weapons \xC2\xB7 DirectX12 \xC2\xB7 step 7 Expect \xC2\xB7 f120", sw::AutomationRunTitleUtil::makeSummary( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 백엔드나 단계가 없으면 그 칸을 적지 않는다(헤드리스 · 첫 단계 전)
 */
SW_TEST_CASE( AutomationRunTitleTest, MissingBackendAndStepAreOmitted )
{
    sw::AutomationRunTitleInput input;
    input._scenarioName = "boot";
    input._frame        = 3;
    SW_EXPECT_STREQ( "boot \xC2\xB7 f3", sw::AutomationRunTitleUtil::makeSummary( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 시나리오가 없으면 벤치 인자 요약을 적는다
 */
SW_TEST_CASE( AutomationRunTitleTest, BenchSummaryReplacesScenarioName )
{
    sw::AutomationRunTitleInput input;
    input._benchSummary = "gv_benchMeshes=1000";
    input._backendName  = "Vulkan";
    input._frame        = 60;
    SW_EXPECT_STREQ( "bench gv_benchMeshes=1000 \xC2\xB7 Vulkan \xC2\xB7 f60", sw::AutomationRunTitleUtil::makeSummary( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 창 제목은 기본 제목 뒤에 요약을 붙인다
 */
SW_TEST_CASE( AutomationRunTitleTest, WindowTitlePrefixesBaseTitle )
{
    sw::AutomationRunTitleInput input;
    input._baseTitle    = "SWEngine";
    input._scenarioName = "boot";
    input._frame        = 1;
    SW_EXPECT_STREQ( "SWEngine \xE2\x80\x94 boot \xC2\xB7 f1", sw::AutomationRunTitleUtil::makeWindowTitle( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 에디터가 만든 제목(이미 구분 글이 든 것)도 그대로 앞에 두고 요약만 붙인다
 */
SW_TEST_CASE( AutomationRunTitleTest, WindowTitleKeepsComposedEditorTitle )
{
    sw::AutomationRunTitleInput input;
    input._baseTitle    = "Game \xE2\x80\x94 scene* \xE2\x80\x94 SW Editor";
    input._scenarioName = "editor/x";
    input._frame        = 2;
    SW_EXPECT_STREQ( "Game \xE2\x80\x94 scene* \xE2\x80\x94 SW Editor \xE2\x80\x94 editor/x \xC2\xB7 f2", sw::AutomationRunTitleUtil::makeWindowTitle( input ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 긴 제목은 한계 안으로 줄이고 UTF-8 글자 중간을 자르지 않는다
 */
SW_TEST_CASE( AutomationRunTitleTest, LongTitleIsTruncatedOnCharacterBoundary )
{
    sw::string longName;
    for ( int32 index = 0; index < 100; ++index )
    {
        longName += "\xEA\xB0\x80"; // 가 (3 바이트) x 100 = 300 바이트
    }
    sw::AutomationRunTitleInput input;
    input._scenarioName    = longName;
    const sw::string title = sw::AutomationRunTitleUtil::makeWindowTitle( input );
    SW_EXPECT_TRUE( title.size() <= sw::AutomationRunTitleUtil::kMaxTitleBytes );
    SW_ASSERT_TRUE( title.size() >= 3 );
    SW_EXPECT_STREQ( "\xE2\x80\xA6", title.c_str() + title.size() - 3 );
    // 줄인 글은 전부 3 바이트 글자(앞 바이트 0xEA)와 끝의 말줄임표뿐이라 길이가 3 의 배수다.
    SW_EXPECT_EQUAL( size_t{ 0 }, title.size() % 3 );
}

/**
 * @brief [AutomationRunTitleTest] 단계 전환 로그는 이름 · 단계 번호 · 단계 이름 · 프레임을 적는다
 */
SW_TEST_CASE( AutomationRunTitleTest, StepLogLineMatchesAgreedFormat )
{
    SW_EXPECT_STREQ( "[Scenario] boot step 4 Screenshot frame 90", sw::AutomationRunTitleUtil::makeStepLogLine( "boot", 4, "Screenshot", 90 ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 재현 명령은 게임 · 백엔드 · 에디터 · 시나리오 목록 · 추가 인자를 순서대로 적고, 점이나 공백이 든 인자는 큰따옴표로 감싼다
 */
SW_TEST_CASE( AutomationRunTitleTest, ReproduceCommandListsTheArgumentsInOrder )
{
    sw::AutomationReproduceInput input;
    input._game             = "Shooter3D";
    input._backendName      = "DirectX12";
    input._scenarioArgument = "engine/automation/a.scenario.xml,b.scenario.xml";
    input._bEditor          = true;
    input._bKeepGoing       = true;
    input._listExtraArgument.push_back( "-gv_fixedFrameDelta=0.5" );
    input._listExtraArgument.push_back( "-gv_firstScene=a b" );
    SW_EXPECT_STREQ( "App.exe -game=Shooter3D -gv_rhiBackend=DirectX12 -EnableEditor \"-scenario=engine/automation/a.scenario.xml,b.scenario.xml\" "
                     "-scenario-keep-going \"-gv_fixedFrameDelta=0.5\" \"-gv_firstScene=a b\"",
                     sw::AutomationRunTitleUtil::makeReproduceCommand( input ).c_str() );

    sw::AutomationReproduceInput bare;
    bare._scenarioArgument = "a_scenario";
    SW_EXPECT_STREQ( "App.exe -scenario=a_scenario", sw::AutomationRunTitleUtil::makeReproduceCommand( bare ).c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 시나리오 인자가 없으면 재현 명령을 지어내지 않는다(세션 없이 돈 실행기)
 */
SW_TEST_CASE( AutomationRunTitleTest, ReproduceCommandIsEmptyWithoutAScenario )
{
    sw::AutomationReproduceInput input;
    input._game = "Shooter3D";
    SW_EXPECT_TRUE( sw::AutomationRunTitleUtil::makeReproduceCommand( input ).empty() );
}

/**
 * @brief [AutomationRunTitleTest] 벤치 요약은 gv_bench 인자만 이름순으로 잇는다
 */
SW_TEST_CASE( AutomationRunTitleTest, BenchSummaryKeepsOnlyBenchArgumentsSorted )
{
    sw::vector<sw::string> listPair;
    listPair.push_back( "gv_benchMeshes=1000" );
    listPair.push_back( "gv_fixedFrameDelta=0.016" );
    listPair.push_back( "gv_benchCharacters=50" );
    SW_EXPECT_STREQ( "gv_benchCharacters=50 gv_benchMeshes=1000", sw::AutomationRunTitleUtil::makeBenchSummary( listPair ).c_str() );
    SW_EXPECT_TRUE( sw::AutomationRunTitleUtil::makeBenchSummary( {} ).empty() );
}

/**
 * @brief [AutomationRunTitleTest] 같은 단계에서는 처음 한 번과 kTitleRefreshFrames 프레임마다만 제목을 건다
 */
SW_TEST_CASE( AutomationRunTitleTest, TrackerSetsTitleOnlyOnFirstFrameAndRefreshInterval )
{
    sw::AutomationRunTitleTracker tracker;
    FakeTitleTarget               target;
    FakeRunObserver               observer;
    tickFrames( tracker, target, observer, static_cast<int32>( sw::AutomationRunTitleUtil::kTitleRefreshFrames ) - 1 );
    SW_EXPECT_EQUAL( 1, target._applyCount );
    tickFrames( tracker, target, observer, 1 ); // 30 번째 프레임
    SW_EXPECT_EQUAL( 2, target._applyCount );
}

/**
 * @brief [AutomationRunTitleTest] 단계가 바뀌면 새로 고침 시점이 아니어도 바로 건다
 */
SW_TEST_CASE( AutomationRunTitleTest, TrackerSetsTitleImmediatelyWhenStepChanges )
{
    sw::AutomationRunTitleTracker tracker;
    FakeTitleTarget               target;
    FakeRunObserver               observer;
    tickFrames( tracker, target, observer, 3 );
    SW_EXPECT_EQUAL( 1, target._applyCount );
    observer._stepNumber = 2;
    tickFrames( tracker, target, observer, 1 );
    SW_EXPECT_EQUAL( 2, target._applyCount );
}

/**
 * @brief [AutomationRunTitleTest] 평소 제목이 바뀌면 실행 중에도 바로 새 평소 제목 뒤에 실행 정보를 붙여 건다
 */
SW_TEST_CASE( AutomationRunTitleTest, TrackerFollowsBaseTitleChangeDuringRun )
{
    sw::AutomationRunTitleTracker tracker;
    FakeTitleTarget               target;
    FakeRunObserver               observer;
    target._base = "Game \xE2\x80\x94 a \xE2\x80\x94 SW Editor";
    tickFrames( tracker, target, observer, 2 );
    SW_EXPECT_EQUAL( 1, target._applyCount );
    target._base = "Game \xE2\x80\x94 a* \xE2\x80\x94 SW Editor";
    tickFrames( tracker, target, observer, 1 );
    SW_EXPECT_EQUAL( 2, target._applyCount );
    SW_EXPECT_TRUE( target._current.find( "a* \xE2\x80\x94 SW Editor \xE2\x80\x94 boot" ) != sw::string::npos );
}

/**
 * @brief [AutomationRunTitleTest] 시나리오가 없으면 평소 제목을 한 번만 걸고, 끝나면 평소 제목으로 한 번 되돌린다
 */
SW_TEST_CASE( AutomationRunTitleTest, TrackerRestoresBaseTitleOnceWhenRunEnds )
{
    sw::AutomationRunTitleTracker tracker;
    FakeTitleTarget               target;
    FakeRunObserver               observer;
    observer._bRunning = false;
    tickFrames( tracker, target, observer, 5 );
    SW_EXPECT_EQUAL( 1, target._applyCount ); // 처음 평소 제목 하나뿐 — 같은 제목을 다시 걸지 않는다
    SW_EXPECT_STREQ( "Game", target._current.c_str() );
    observer._bRunning = true;
    tickFrames( tracker, target, observer, 2 );
    SW_EXPECT_EQUAL( 2, target._applyCount );
    SW_EXPECT_TRUE( target._current != "Game" );
    observer._bRunning = false;
    tickFrames( tracker, target, observer, 4 );
    SW_EXPECT_EQUAL( 3, target._applyCount );
    SW_EXPECT_STREQ( "Game", target._current.c_str() );
}

/**
 * @brief [AutomationRunTitleTest] 창이 없으면(이음매 nullptr) 아무것도 걸지 않고 멈추지 않는다
 */
SW_TEST_CASE( AutomationRunTitleTest, TrackerWithoutWindowDoesNothing )
{
    sw::AutomationRunTitleTracker         tracker;
    FakeRunObserver                       observer;
    sw::vector<sw::IEngineFrameObserver*> listObserver{ &observer };
    tracker.onFrameEnd( nullptr, nullptr, "dx12", listObserver );
    SW_EXPECT_EQUAL( size_t{ 1 }, listObserver.size() ); // 위 호출이 죽지 않고 돌아왔다
}
