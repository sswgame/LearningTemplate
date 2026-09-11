#include "pch.h"

#include "Editor/Common/Workspace/ReloadFileManager.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// ReloadFileManager 등록 및 해제 라이프사이클
//
// 파일 감시는 **에디터가 켜져 있을 때만** 도는 개발 기능이라 Engine 이 아니라 Editor 가 소유한다
// (Editor/Common/Workspace). 그래서 이 테스트도 EngineTest 가 아니라 여기에 있다.
// ------------------------------------------------------------------------------

SW_TEST_CASE( EditorHotReloadTest, ReloadFileManagerLifecycle )
{
    sw::ReloadFileManager manager;
    SW_EXPECT_TRUE( manager.initialize() );

    bool       bCallbackCalled = false;
    const auto handle          = manager.registerWatch( "Resource/shaders", { ".hlsl" },
                                                        SW_DELEGATE_LAMBDA( sw::FileWatchMatchDelegate, [&bCallbackCalled]( const sw::FileChangeEvent& )
             {
        bCallbackCalled = true;
    } ) );

    SW_EXPECT_TRUE( handle.isValid() );
    manager.unregisterWatch( handle );
    manager.shutdown();
}
