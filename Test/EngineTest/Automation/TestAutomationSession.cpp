#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Automation/AutomationRunner.h"
#include "Engine/Automation/AutomationSession.h"
#include "Engine/Automation/AutomationStepRegistry.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Utility/EngineFrameObserver.h"

#include "TestFramework/TestFramework.h"

// 시나리오 구동기 — 파일 둘 이상을 한 프로세스에서 차례로 돌리고(통과해야 다음), 첫 실패에서 멈추거나(`keepGoing` 이면 이어 돌고) 종료 코드를 낸다.
// 세션은 프레임 관찰자(`IEngineFrameObserver`)다 — 엔진 루프가 프레임 앞 · 뒤에서 부르는 자리를 테스트가 대신 부른다.

namespace
{
    struct TestAutomationSessionInternal
    {
        static inline uint32 s_markCount = 0;

        static bool runMark( sw::AutomationRunner& /*runner*/, const sw::AutomationStep& /*step*/ )
        {
            ++s_markCount;
            return true;
        }

        /** @brief 시나리오 파일 하나를 `Saved/Automation/SessionTest/<name>.scenario.xml` 에 씁니다. @p body 는 `<At>` 들입니다. */
        static sw::string writeScenario( const utf8* pName, const utf8* pBody )
        {
            const sw::string folder = "Saved/Automation/SessionTest";
            (void)sw::FileUtil::ensureDirectoryExists( folder );
            const sw::string path = folder + "/" + pName + ".scenario.xml";
            const sw::string text = sw::string( "<Scenario name=\"" ) + pName + "\" startAfter=\"Immediately\" timeoutFrames=\"50\">" + pBody + "</Scenario>";
            SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( path, text ) );
            return path;
        }

        /** @brief 세션을 끝날 때까지(최대 @p maxFrames) 돌려 종료 코드를 돌려줍니다. */
        static int32 run( sw::AutomationSession& session, sw::InputManager& input, uint32 maxFrames = 200 )
        {
            sw::IEngineFrameObserver& observer = session; // 엔진 루프처럼 인터페이스로만 부른다
            sw::EngineFrameContext    context;
            context._pInput                   = &input;
            context._completedScreenshotCount = 0u;
            int32 exitCode                    = sw::IEngineFrameObserver::kRunning;
            for ( uint32 frame = 0; frame < maxFrames && exitCode == sw::IEngineFrameObserver::kRunning; ++frame )
            {
                observer.onFrameBegin( context );
                input.beginFrame( 1.0f / 60.0f );
                exitCode = observer.onFrameEnd( context );
                input.endFrame();
            }
            return exitCode;
        }
    };

    SW_AUTOMATION_STEP( sessionMark, "SessionMark", &TestAutomationSessionInternal::runMark, nullptr, true );
} // namespace

/**
 * @brief [AutomationSessionTest] 통과하는 시나리오 둘은 한 세션에서 차례로 돌고 종료 코드 0 으로 끝난다
 */
SW_TEST_CASE( AutomationSessionTest, PassingScenariosRunInOrderAndExitZero )
{
    using Internal = TestAutomationSessionInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    const sw::string first  = Internal::writeScenario( "sessionfirst", R"(<At frame="1"><SessionMark/></At><At frame="3"><Pass/></At>)" );
    const sw::string second = Internal::writeScenario( "sessionsecond", R"(<At frame="1"><SessionMark/></At><At frame="2"><SessionMark/></At><At frame="4"><Pass/></At>)" );

    Internal::s_markCount = 0;
    test::ScopedLogSuppressor suppressor; // 이 테스트 호스트에는 `gv_fixedFrameDelta` 가 없다는 경고
    sw::AutomationSession     session;
    SW_ASSERT_TRUE( session.begin( first + "," + second, {}, false, 0u ) );
    SW_EXPECT_EQUAL( 0, Internal::run( session, input ) );
    SW_EXPECT_EQUAL( 3u, Internal::s_markCount ); // 첫 시나리오 하나 + 둘째 둘 — 둘 다 돌았다
    input.shutdown();
}

/**
 * @brief [AutomationSessionTest] 첫 실패에서 멈추고 뒤 시나리오는 돌리지 않는다 — 종료 코드는 그 실패의 것(10)이다. `keepGoing` 이면 이어 돌고 처음 실패의 코드로 끝난다
 */
SW_TEST_CASE( AutomationSessionTest, FailureStopsTheChainUnlessKeepGoing )
{
    using Internal = TestAutomationSessionInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    const sw::string failing = Internal::writeScenario( "sessionfailing", R"(<At frame="1"><Fail reason="expected by the test"/></At>)" );
    const sw::string after   = Internal::writeScenario( "sessionafter", R"(<At frame="1"><SessionMark/></At><At frame="2"><Pass/></At>)" );

    test::ScopedLogSuppressor suppressor;
    {
        Internal::s_markCount = 0;
        sw::AutomationSession session;
        SW_ASSERT_TRUE( session.begin( failing + "," + after, {}, false, 0u ) );
        SW_EXPECT_EQUAL( static_cast<int32>( sw::AutomationResult::Failed ), Internal::run( session, input ) );
        SW_EXPECT_EQUAL( 0u, Internal::s_markCount ); // 뒤 시나리오는 돌지 않았다
    }
    {
        Internal::s_markCount = 0;
        sw::AutomationSession session;
        SW_ASSERT_TRUE( session.begin( failing + "," + after, {}, true, 0u ) );
        SW_EXPECT_EQUAL( static_cast<int32>( sw::AutomationResult::Failed ), Internal::run( session, input ) );
        SW_EXPECT_EQUAL( 1u, Internal::s_markCount ); // 이어 돌았다
    }
    input.shutdown();
}

/**
 * @brief [AutomationSessionTest] 읽을 수 없는 시나리오는 읽기 오류(11)로 끝나고, 목록이 비면 시작하지 않는다
 */
SW_TEST_CASE( AutomationSessionTest, MissingScenarioIsALoadErrorAndAnEmptyListDoesNotStart )
{
    using Internal = TestAutomationSessionInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    test::ScopedLogSuppressor suppressor;

    sw::AutomationSession missing;
    SW_ASSERT_TRUE( missing.begin( "Saved/Automation/SessionTest/does-not-exist.scenario.xml", {}, false, 0u ) );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::AutomationResult::LoadError ), Internal::run( missing, input ) );

    sw::AutomationSession empty;
    SW_EXPECT_FALSE( empty.begin( " , ", {}, false, 0u ) );
    input.shutdown();
}

/**
 * @brief [AutomationSessionTest] 시작하지 않은 세션은 관찰자로서 아무것도 정하지 않는다 — 창 닫힘 · 프레임 끝이 `kRunning` 이고 패킷의 스크린샷 경로를 건드리지 않는다
 */
SW_TEST_CASE( AutomationSessionTest, IdleSessionIsANeutralFrameObserver )
{
    sw::AutomationSession        session;
    sw::IEngineFrameObserver&    observer = session;
    const sw::EngineFrameContext context; // 입력 없음
    observer.onFrameBegin( context );
    SW_EXPECT_EQUAL( sw::IEngineFrameObserver::kRunning, observer.onFrameEnd( context ) );
    SW_EXPECT_EQUAL( sw::IEngineFrameObserver::kRunning, observer.onWindowClosed( context ) );

    sw::string                    screenshotPath = "earlier.ppm";
    sw::EngineRenderPacketContext packetContext;
    packetContext._pScreenshotPath = &screenshotPath;
    observer.onBuildRenderPacket( packetContext );
    SW_EXPECT_TRUE( screenshotPath == "earlier.ppm" ); // 앞 관찰자가 채운 경로는 그대로 둔다
}
