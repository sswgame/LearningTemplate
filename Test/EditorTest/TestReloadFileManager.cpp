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

/**
 * @brief [EditorHotReloadTest] 콜백이 감시를 더 걸어도 전달이 무너지지 않는다
 * @details 전달은 `_listWatch` 를 범위 for 로 돌면서 콜백을 불렀다. 리로드 콜백이 자기 감시를
 *          다시 거는 것은 흔한 일인데(에셋을 다시 읽고 다시 arm 한다), 그러면 벡터가 순회 도중
 *          재할당돼 **참조가 뜬 메모리를 가리킨다.** 인덱스로 돌고 델리게이트를 부르기 전에
 *          복사하는 것으로 고쳤다 — `MulticastDelegate::broadcast` 와 같은 모양이다.
 *
 *          콜백 하나가 감시를 넉넉히 더 걸어 재할당을 **반드시** 일으킨다. 고치기 전 코드는
 *          ASAN 빌드에서 해제된 메모리 접근으로 그 자리에서 죽는다.
 */
SW_TEST_CASE( EditorHotReloadTest, DispatchSurvivesWatchesAddedFromItsOwnCallback )
{
    sw::ReloadFileManager manager;

    int32 callCount = 0;
    manager.registerWatch( "Resource", { ".hlsl" },
                           SW_DELEGATE_LAMBDA( sw::FileWatchMatchDelegate, [&manager, &callCount]( const sw::FileChangeEvent& )
    {
        ++callCount;
        // 순회 중인 목록을 **반드시 재할당시킨다** — 늘어나는 폭이 기하급수라 넉넉히 건다.
        for ( int32 addIndex = 0; addIndex < 64; ++addIndex )
        {
            manager.registerWatch( "Resource/never_matches", { ".none" }, {} );
        }
    } ) );

    sw::FileChangeEvent changeEvent{};
    changeEvent._action    = sw::FileWatcherAction::Modified;
    changeEvent._directory = "Resource/shaders";
    changeEvent._filename  = "forward_lit.hlsl";

    sw::vector<sw::FileChangeEvent> listEvent;
    listEvent.push_back( changeEvent );

    manager.dispatchEvents( listEvent );

    SW_EXPECT_EQUAL( 1, callCount );

    // 목록이 멀쩡히 살아 있어야 한다 — 한 번 더 보내도 같은 답이 나온다.
    manager.dispatchEvents( listEvent );
    SW_EXPECT_EQUAL( 2, callCount );
}
