#include "pch.h"

#include "Engine/Resource/AssetStreamingQueue.h"

#include "TestFramework/TestFramework.h"

// Engine_Streaming · Engine_Resource — 비동기 로드 큐의 완료 통지와 인플라이트 멀티캐스트.
// ------------------------------------------------------------------------------
// 3) AssetStreamingQueue 비동기 요청 큐 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( Engine_Resource, AssetStreamingQueueAsyncOperations )
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
SW_TEST_CASE( Engine_Streaming, AssetStreamingQueueInFlightMulticastCallbacks )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    int32 callback1Count = 0;
    int32 callback2Count = 0;
    int32 callback3Count = 0;

    // 동일한 가상 에셋 경로에 대해 연속으로 3회 요청
    const utf8* pTestAsset = "Resource/test_dummy_asset.png";

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback1Count]( string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback1Count;
    } ) );

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback2Count]( string_view path, bool bSuccess )
    {
        (void)path;
        (void)bSuccess;
        ++callback2Count;
    } ) );

    queue.requestAsset( pTestAsset, sw::StreamingPriority::Normal,
                        SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&callback3Count]( string_view path, bool bSuccess )
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
