#include "pch.h"

#if !defined( SW_SHIPPING )
    #include "App/Module/LiveReloadManager.h"
#endif
#include "App/Module/ModuleHost.h"

#include "Engine/Graphics/RHI/RHI.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) ModuleHostTest — 디바이스가 없을 때의 호스트
//
// `RHI` 객체는 있는데 **디바이스가 없는 상태**가 실제로 존재한다: 백엔드 교체가 실패하면 디바이스만
// 사라지고 RHI 는 남는다(`BackendSwapController::applyPendingChange` 의 실패 경로). 그 뒤 종료가
// 돌면 호스트는 반드시 `drainRenderWorkers` 를 지나가는데, `RHI::getDevice()` 는 **널 참조**를
// 돌려주므로 묻는 순간 죽는다. `EngineLoop::shutdown` 은 같은 이유로 이미 `hasDevice()` 를 먼저
// 묻고 있었다 — 그 주석에 "이게 없어서 정상적인 실패가 종료 경로에서 SEGFAULT 로 끝났다" 고 적혀 있다.
//
// 이 스위트는 그 자리를 **디바이스 없는 RHI 하나로** 재현한다. GPU 도 창도 필요 없다.
// ------------------------------------------------------------------------------

/**
 * @brief [ModuleHostTest] 디바이스 없는 RHI 로도 초기화·종료가 죽지 않는다
 * @details 고치기 전에는 `shutdown()` 안의 `drainRenderWorkers` 가 널 참조를 물어 프로세스가 죽었다.
 *          배포 구성에서는 초기화가 게임 인스턴스를 만들려다 같은 자리에서 죽었다.
 */
SW_TEST_CASE( ModuleHostTest, SurvivesAnRhiThatHasNoDevice )
{
    RHI rhi; // 디바이스 없음 — initialize() 를 부르지 않는다.
    SW_ASSERT_FALSE( rhi.hasDevice() );

    ModuleHost host;
    // 리로드 매니저·창·렌더 스레드 없이. 개발 구성은 등록할 모듈이 없어 그대로 성공하고,
    // 배포 구성은 정적 게임 API 를 묶다가 **디바이스가 없다는 것을 알고 멈춘다.**
    SW_EXPECT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, false, {} ) );

    // 디바이스가 없으면 재생성도 거절한다 — 여기서 true 를 돌려주면 호출자가 인스턴스가 있다고 믿는다.
    SW_EXPECT_FALSE( host.reinitializeAfterRhiSwap( nullptr, nullptr ) );

    host.shutdown(); // 예전 코드는 여기서 죽었다.
}

#if !defined( SW_SHIPPING )
// 아래 케이스는 **개발 구성 전용**이다 — `LiveReloadManager` 는 배포 빌드에 아예 들어가지 않는다
// (모듈이 전부 정적 링크라 다시 올릴 것이 없다. `Test/SmokeTest/CMakeLists.txt` 의 소스 목록 참고).
/**
 * @brief [ModuleHostTest] 종료하면 등록부에 남은 콜백이 없다
 * @details 콜백은 `ModuleHost` 의 메서드를 가리킨다. `App` 은 호스트를 먼저 지우고 등록부를 나중에
 *          내리므로, 남아 있으면 그 사이의 리로드가 죽은 객체로 뛰어든다. 예전에는 배수·배치
 *          델리게이트 둘만 떼고 **모듈마다 건 것은 그대로 두었다.**
 */
SW_TEST_CASE( ModuleHostTest, ShutdownDetachesEveryCallbackItRegistered )
{
    LiveReloadManager manager;
    if ( manager.registerModule( "SWGame" ) == false )
        SW_TEST_SKIP( "SWGame 모듈이 옆에 없습니다 (배포 구성 또는 빌드 산출물 없음)" );

    void* const initialHandle = manager.getModuleHandle( "SWGame" );
    SW_ASSERT_NOT_NULL( initialHandle );

    bool bBeforeCalled = false;
    manager.setOnBeforeReload( "SWGame", SW_DELEGATE_LAMBDA( LiveReloadManager::OnBeforeReloadDelegate, [&bBeforeCalled]()
    {
        bBeforeCalled = true;
    } ) );

    // 호스트가 자기 콜백을 뗄 때 쓰는 바로 그 창구다.
    manager.clearReloadCallbacks();

    // 디바운스(300ms)를 넘겨 **실제로 다시 올린다** — 리로드가 일어나지 않으면 "안 불렸다" 는
    // 아무것도 증명하지 못한다. 핸들이 바뀌는 것이 리로드가 돌았다는 증거다.
    manager.triggerReload( "SWGame" );
    for ( int32 stepIndex = 0; stepIndex < 80; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( manager.getModuleHandle( "SWGame" ) != initialHandle )
            break;
    }

    SW_EXPECT_TRUE_MSG( manager.getModuleHandle( "SWGame" ) != initialHandle,
                        "리로드가 일어나지 않았습니다 — 이 케이스는 아무것도 검증하지 못합니다" );
    SW_EXPECT_TRUE_MSG( bBeforeCalled == false, "뗀 콜백이 아직 불립니다 — 죽은 객체를 가리키는 자리다" );

    manager.shutdown();
}
#endif
