// DiskIOScopeTest — 디스크 IO 를 금지한 구간: 겹치기 · 잠시 풀기, 구간 안의 파일 읽기 · 쓰기 단언, 다른 스레드와 도와 돌리는 태스크는 막지 않는다.
#include "pch.h"

#include "Core/Concurrency/DiskIOScope.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [DiskIOScopeTest] 금지는 범위를 따르고 풀기 안에서는 꺼진다
 * @details 변이: 소멸자가 바깥 값을 되돌리지 않으면 겹친 구간 뒤의 단언이 진다.
 */
SW_TEST_CASE( DiskIOScopeTest, NestsAndSuspends )
{
    SW_EXPECT_FALSE( DiskIOForbiddenScope::isActive() );
    {
        const DiskIOForbiddenScope outer;
        SW_EXPECT_TRUE( DiskIOForbiddenScope::isActive() );
        {
            const DiskIOAllowedScope allowed;
            SW_EXPECT_FALSE( DiskIOForbiddenScope::isActive() );
            const DiskIOForbiddenScope inner;
            SW_EXPECT_TRUE( DiskIOForbiddenScope::isActive() );
        }
        SW_EXPECT_TRUE( DiskIOForbiddenScope::isActive() );
    }
    SW_EXPECT_FALSE( DiskIOForbiddenScope::isActive() );
}

/**
 * @brief [DiskIOScopeTest] 구간 안에서 파일을 읽고 쓰면 단언한다 — 구간 밖은 조용하다
 * @details 변이: `FileUtil` 의 읽기나 쓰기 입구에서 검사를 빼면 그쪽 단언이 진다.
 */
SW_TEST_CASE( DiskIOScopeTest, FileAccessInsideTheSectionAsserts )
{
    const AbsolutePath path = test::makeScratchPath( "disk_io_scope.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "outside" ) );
    {
        SW_EXPECT_LOG( SW_ASSERT_LOG_ID );
        const test::ScopedAssertCapture capture;
        const DiskIOForbiddenScope      forbidden;
        string                          text;
        SW_EXPECT_TRUE( FileUtil::readTextFile( path, text ) ); // 단언은 알리기만 한다 — 길은 이어진다
        SW_EXPECT_TRUE( FileUtil::writeTextFile( path, "inside" ) );
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 2u, capture.getCount() );
    }
    SW_EXPECT_TRUE( FileUtil::removeFile( path ) );
}

/**
 * @brief [DiskIOScopeTest] 금지한 스레드가 기다리며 도와 돌린 태스크 안에서는 금지가 꺼져 있다
 * @details 워커가 먼저 집어도 워커에는 금지가 없다 — 어느 쪽이 돌려도 같은 답이다. 변이: `TaskManager::executeTask` 의 풀기를 빼면 도와 돌린 경우에 진다.
 */
SW_TEST_CASE( DiskIOScopeTest, HelpedTasksMayUseTheDisk )
{
    TaskManager taskManager;
    SW_REQUIRE_TRUE( taskManager.initialize( 1 ) );
    TaskStageHandle            stage = taskManager.createStage();
    atomic<uint32>             forbiddenCount{ 0 };
    const DiskIOForbiddenScope forbidden;
    for ( uint32 taskIndex = 0; taskIndex < 64; ++taskIndex )
    {
        TaskHandle task = taskManager.emplaceTask( "DiskIOScopeTest", SW_DELEGATE_LAMBDA( TaskDelegate, [&forbiddenCount]()
        {
            if ( DiskIOForbiddenScope::isActive() )
                forbiddenCount.fetch_add( 1 );
        } ) );
        taskManager.addToStage( stage, task, "DiskIOScopeTest" );
        task.submit();
    }
    taskManager.waitStage( stage );
    SW_EXPECT_EQUAL( 0u, forbiddenCount.load() );
    SW_EXPECT_TRUE( DiskIOForbiddenScope::isActive() );
    taskManager.shutdown();
}
