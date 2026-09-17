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

    SW_EXPECT_TRUE( queue.isStreaming( "Resource/common/shaders/forward_lit.hlsl" ) || queue.isLoaded( "Resource/common/shaders/forward_lit.hlsl" ) );
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
