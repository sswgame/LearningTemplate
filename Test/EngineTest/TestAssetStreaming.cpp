#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetStreamingQueue.h"

#include "TestFramework/TestFramework.h"

// AssetStreamingQueue — 비동기 요청 큐의 완료 통지 · 인플라이트 멀티캐스트 · 퓨처와 동시성 스트레스.
// 예전엔 이 주제가 세 스위트(Engine_Resource · Engine_Streaming · AssetStreamingTest)로 갈려
// 두 파일에 흩어져 있었다.

SW_TEST_CASE( AssetStreamingTest, AssetStreamingQueueAsyncOperations )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    bool bCompleteCalled = false;
    queue.requestAsset( "Resource/common/shaders/forward_lit.hlsl", sw::StreamingPriority::High,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&bCompleteCalled]( std::string_view, bool )
    {
        bCompleteCalled = true;
    } ) );

    // 이 경로는 **일부러 없는 것**이다(도메인 접두사가 `Resource/` 라 해석되지 않는다). 여기서
    // 보는 것은 "요청이 등록됐는가" 뿐이므로 성패를 묻지 않는다 — 아직 돌고 있거나(`isStreaming`)
    // 이미 결과가 적혔거나(`getCompletedCount`) 둘 중 하나다. 예전에는 뒷항이 `isLoaded` 였는데,
    // 그때의 `isLoaded` 는 실패한 경로에도 true 를 돌려줘서 **틀린 이유로 통과**하고 있었다.
    SW_EXPECT_TRUE( queue.isStreaming( "Resource/common/shaders/forward_lit.hlsl" ) || queue.getCompletedCount() > 0 );
    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 실패한 요청은 "로드됨" 이 아니며, 다시 요청하면 재시도된다
 * @details 결과 표에는 성공도 실패도 들어간다. 키의 존재만 보면 둘을 구별하지 못한다 —
 *          그러면 아직 굽지 않은 셰이더나 늦게 마운트되는 팩을 한 번 헛읽은 뒤로 영원히
 *          "이미 로드됨, 성공" 이라고 답하고 다시는 디스크를 보지 않는다.
 */
SW_TEST_CASE( AssetStreamingTest, FailedRequestIsNotLoadedAndRetries )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    // 존재하지 않는 경로. 파일을 만들지 않으므로 워커는 반드시 실패한다.
    const sw::string missingPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_missing_asset.dat" );
    sw::FileUtil::removeFile( missingPath );

    bool bFirstCompleted{ false };
    bool bFirstSuccess{ true };
    queue.requestAsset( missingPath, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&]( sw::string_view, bool bSuccess )
    {
        bFirstCompleted = true;
        bFirstSuccess   = bSuccess;
    } ) );

    for ( int32 attempt = 0; attempt < 100 && bFirstCompleted == false; ++attempt )
    {
        queue.update();
        if ( bFirstCompleted )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }

    SW_ASSERT_TRUE( bFirstCompleted );
    SW_EXPECT_FALSE( bFirstSuccess );
    // 끝나기는 했다 — 그러나 로드된 것은 아니다. 이 둘이 갈리는 자리가 이 테스트의 전부다.
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getCompletedCount() );
    SW_EXPECT_FALSE( queue.isLoaded( missingPath ) );

    // 이제 파일이 생겼다. 다시 요청하면 기록된 실패를 넘어 **다시 읽어야** 한다.
    const sw::string payload = "RETRY_PAYLOAD";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( missingPath,
                                             reinterpret_cast<const uint8*>( payload.data() ),
                                             static_cast<uint64>( payload.size() ) ) );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [missingPath]()
    {
        sw::FileUtil::removeFile( missingPath );
    } ) );

    bool bSecondCompleted{ false };
    bool bSecondSuccess{ false };
    queue.requestAsset( missingPath, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&]( sw::string_view, bool bSuccess )
    {
        bSecondCompleted = true;
        bSecondSuccess   = bSuccess;
    } ) );

    for ( int32 attempt = 0; attempt < 100 && bSecondCompleted == false; ++attempt )
    {
        queue.update();
        if ( bSecondCompleted )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }

    SW_ASSERT_TRUE( bSecondCompleted );
    SW_EXPECT_TRUE( bSecondSuccess );
    SW_EXPECT_TRUE( queue.isLoaded( missingPath ) );

    queue.shutdown();
}

// ------------------------------------------------------------------------------
// 12) AssetStreamingQueue In-Flight Multicast Callbacks 검증
// ------------------------------------------------------------------------------
SW_TEST_CASE( AssetStreamingTest, AssetStreamingQueueInFlightMulticastCallbacks )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    int32 callback1Count = 0;
    int32 callback2Count = 0;
    int32 callback3Count = 0;

    // 동일한 가상 에셋 경로에 대해 연속으로 3회 요청
    const utf8* pTestAsset = "Resource/test_dummy_asset.png";

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback1Count]( sw::string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback1Count;
    } ) );

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback2Count]( sw::string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback2Count;
    } ) );

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback3Count]( sw::string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback3Count;
    } ) );

    // 비동기 태스크 완료 대기 및 완료 큐 틱 디스패치
    const int32 maxAttempts = 100;
    for ( int32 attempt = 0; attempt < maxAttempts; ++attempt )
    {
        queue.update();
        if ( callback1Count > 0 && callback2Count > 0 && callback3Count > 0 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }

    SW_EXPECT_EQUAL( 1, callback1Count );
    SW_EXPECT_EQUAL( 1, callback2Count );
    SW_EXPECT_EQUAL( 1, callback3Count );

    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] TaskFuture 및 LockFreeQueue 기반 에셋 스트리밍 완료 검증
 */
SW_TEST_CASE( AssetStreamingTest, StreamingFutureAndLockFreeQueue )
{
    const sw::string tempFile = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_streaming_asset.dat" );
    const sw::string testData = "STREAMING_TEST_PAYLOAD";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( tempFile,
                                             reinterpret_cast<const uint8*>( testData.data() ),
                                             static_cast<uint64>( testData.size() ) ) );

    sw::AssetStreamingQueue queue;
    queue.initialize();

    sw::TaskFuture<bool> futSuccess = queue.requestAssetFuture( tempFile, sw::StreamingPriority::High );
    SW_ASSERT_TRUE( futSuccess.isValid() );

    bool bCallbackRan = false;
    futSuccess.then( [&bCallbackRan]( bool bResult )
    {
        if ( bResult )
            bCallbackRan = true;
    } );

    // 동기/비동기 태스크 완료 대기 및 락-프리 큐 펌프
    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    queue.update( 32 );

    SW_EXPECT_TRUE( futSuccess.isReady() );
    SW_EXPECT_TRUE( futSuccess.get() );
    SW_EXPECT_TRUE( bCallbackRan );
    SW_EXPECT_TRUE( queue.isLoaded( tempFile ) );

    queue.shutdown();
    sw::FileUtil::removeFile( tempFile );
}

/**
 * @brief [AssetStreamingTest] 다중 스레드 동시 스트리밍 및 락-프리 큐 펌프 스트레스 테스트
 */
SW_TEST_CASE( AssetStreamingTest, MultiThreadedConcurrentStreamingStress )
{
    constexpr int32 kFileCount = 8;
    constexpr int32 kWorkers   = 4;

    sw::vector<sw::string> listTempFile;
    listTempFile.reserve( kFileCount );

    const sw::string tempDir = sw::FileUtil::getTempDirectory();
    for ( int32 fileIndex = 0; fileIndex < kFileCount; ++fileIndex )
    {
        const sw::string path = sw::FileUtil::joinPath( tempDir, sw::string( "sw_stress_asset_" ) + sw::string( std::to_string( fileIndex ).c_str() ) + ".dat" );
        const sw::string data = "STRESS_DATA_BLOCK";
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( data.data() ), data.size() ) );
        listTempFile.push_back( path );
    }

    sw::AssetStreamingQueue queue;
    queue.initialize();

    sw::atomic<int32> countCallback{ 0 };

    // 4개 생산자 스레드가 동시에 8개 파일에 대해 비동기 Future 요청 발행
    sw::vector<std::thread> listThread;
    listThread.reserve( kWorkers );

    for ( int32 threadIndex = 0; threadIndex < kWorkers; ++threadIndex )
    {
        listThread.emplace_back( [&queue, &listTempFile, &countCallback]()
        {
            for ( int32 cycle = 0; cycle < 10; ++cycle )
            {
                for ( const sw::string& path : listTempFile )
                {
                    sw::TaskFuture<bool> fut = queue.requestAssetFuture( path );
                    fut.then( [&countCallback]( bool bSuccess )
                    {
                        if ( bSuccess )
                            countCallback.fetch_add( 1, std::memory_order_relaxed );
                    } );
                }
            }
        } );
    }

    for ( auto& workerThread : listThread )
        workerThread.join();

    // 백그라운드 태스크 대기 및 락-프리 완료 큐 드레인
    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    for ( int32 drainStep = 0; drainStep < 50; ++drainStep )
    {
        queue.update( 64 );
        std::this_thread::yield();
    }

    for ( const sw::string& path : listTempFile )
    {
        SW_EXPECT_TRUE( queue.isLoaded( path ) );
        sw::FileUtil::removeFile( path );
    }

    SW_EXPECT_TRUE( countCallback.load() > 0 );
    queue.shutdown();
}
