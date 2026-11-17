#include "pch.h"

#include "Core/Process/Process.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"

// AutomationScratchTest — 시나리오 사본의 프로세스 꼬리 판정. 나란히 도는 실행(백엔드마다 하나)의 남의 사본을 에디터 파일 감시가 무시하는 근거다(nogpu).

/**
 * @brief [AutomationScratchTest] 남의 사본(`scratch/<이름>_p<다른 pid>/…`)만 다른 프로세스의 것이고, 이 프로세스의 사본 · 꼬리 없는 경로 · scratch 밖은 아니다
 */
SW_TEST_CASE( AutomationScratchTest, TellsOtherProcessInstancesApart )
{
    const sw::string ownInstance   = sw::AutomationScratch::makeInstanceID( "scratch/assetmanage" );
    const sw::string otherInstance = "scratch/assetmanage_p" + sw::to_string( sw::Process::getCurrentProcessID() + 1 );

    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( ownInstance + "/probe.material" ) );
    SW_EXPECT_TRUE( sw::AutomationScratch::isOtherProcessInstance( otherInstance + "/probe.material" ) );
    SW_EXPECT_TRUE( sw::AutomationScratch::isOtherProcessInstance( otherInstance ) ); // 폴더 자체가 생기고 사라지는 사건
    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( "scratch/assetmanage/probe.material" ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( "scratch/assetmanage_pabc/probe.material" ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( "game/empty/assetmanage_p1/probe.material" ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( "scratchpad_p1/x.material" ) );
    // 가져오기 사본은 한 층 아래다(scratch/import/<이름>_p<pid>) — 예전 판정은 scratch/ 바로 아래만 봐 남의 가져오기를 집었다(editor.assetmove File.NotFound)
    const sw::string otherNested = "scratch/import/editor.assetidentity_p" + sw::to_string( sw::Process::getCurrentProcessID() + 1 );
    SW_EXPECT_TRUE( sw::AutomationScratch::isOtherProcessInstance( otherNested + "/flickerlight.prefab.xml" ) );
    SW_EXPECT_TRUE( sw::AutomationScratch::isOtherProcessInstance( otherNested ) );
    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( sw::AutomationScratch::makeInstanceID( "scratch/import/editor.assetidentity" ) + "/flickerlight.prefab.xml" ) );
}

/**
 * @brief [AutomationScratchTest] 리소스 루트를 훑은 디스크 경로 목록에서 남의 사본(파일 · 폴더)만 빠지고, 이 프로세스의 사본 · 원본 · 루트 밖 경로는 남는다.
 *        잔여 사본이 원본과 같은 파일 이름이면 Quick Open 이 사본을 먼저 집어 시퀀서가 씬 플레이어를 찾지 못했다(editor.sequencerkeys · sequencercuts).
 */
SW_TEST_CASE( AutomationScratchTest, RemovesOtherProcessInstancesFromScans )
{
    const sw::string contentRoot   = sw::test::makeScratchPath( "Content" ); // 디스크 경로 판정은 OS 의 절대 경로만 받는다(`/work` 는 Windows 에서 상대)
    const sw::string ownInstance   = sw::AutomationScratch::makeInstanceID( "scratch/sequencerkeys" );
    const sw::string otherInstance = "scratch/sequencerkeys_p" + sw::to_string( sw::Process::getCurrentProcessID() + 1 );

    sw::vector<sw::string> listPath{ contentRoot + "/" + otherInstance + "/sequencerkeys.seq.xml", contentRoot + "/" + otherInstance,
                                     contentRoot + "/" + ownInstance + "/sequencerkeys.seq.xml", contentRoot + "/engine/automation/editor/sequencerkeys/sequencerkeys.seq.xml",
                                     "/elsewhere/scratch/x_p1/a.xml" };
    sw::AutomationScratch::removeOtherProcessInstances( contentRoot, listPath );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listPath.size() );
    SW_EXPECT_EQUAL( contentRoot + "/" + ownInstance + "/sequencerkeys.seq.xml", listPath[0] );
    SW_EXPECT_EQUAL( contentRoot + "/engine/automation/editor/sequencerkeys/sequencerkeys.seq.xml", listPath[1] );
    SW_EXPECT_EQUAL( sw::string( "/elsewhere/scratch/x_p1/a.xml" ), listPath[2] );
}

/**
 * @brief [AutomationScratchTest] 프로세스 꼬리는 scratch/ 아래 첫 토막에 달린다 — 뒤 토막의 폴더 이름(`textures_raw`)은 그대로라 폴더 이름으로 원본 종류를 고르는 코드가 사본을 알아본다
 * @details 변이: 꼬리를 마지막 토막에 달면 `scratch/importsettings/textures_raw_p<pid>` 가 되어 인스펙터 Import Settings 가 원본이 아니라고 보았다(editor.importsettings).
 */
SW_TEST_CASE( AutomationScratchTest, InstanceMarkGoesOnTheFirstScratchSegment )
{
    const sw::string pid = sw::to_string( sw::Process::getCurrentProcessID() );
    SW_EXPECT_EQUAL( sw::string( "scratch/importsettings_p" ) + pid + "/textures_raw", sw::AutomationScratch::makeInstanceID( "scratch/importsettings/textures_raw" ) );
    SW_EXPECT_EQUAL( sw::string( "scratch/assetmanage_p" ) + pid, sw::AutomationScratch::makeInstanceID( "scratch/assetmanage" ) );
    SW_EXPECT_EQUAL( sw::string( "run_p" ) + pid, sw::AutomationScratch::makeInstanceID( "run" ) );
    SW_EXPECT_EQUAL( sw::string( "scratch/import/editor.flowimport_p" ) + pid, sw::AutomationScratch::makeInstanceID( "scratch/import/editor.flowimport" ) ); // 가져오기 그릇은 그대로
    SW_EXPECT_FALSE( sw::AutomationScratch::isOtherProcessInstance( sw::AutomationScratch::makeInstanceID( "scratch/importsettings/textures_raw" ) + "/splash.png" ) );
}
