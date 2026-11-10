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
}

/**
 * @brief [AutomationScratchTest] 리소스 루트를 훑은 디스크 경로 목록에서 남의 사본(파일 · 폴더)만 빠지고, 이 프로세스의 사본 · 원본 · 루트 밖 경로는 남는다.
 *        잔여 사본이 원본과 같은 파일 이름이면 Quick Open 이 사본을 먼저 집어 시퀀서가 씬 플레이어를 찾지 못했다(editor.sequencerkeys · sequencercuts).
 */
SW_TEST_CASE( AutomationScratchTest, RemovesOtherProcessInstancesFromScans )
{
    const sw::string contentRoot   = "/work/Content";
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
