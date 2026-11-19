#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"

#include "Engine/Asset/AssetStreamingQueue.h"

// AssetStreamingQueueTest — 에셋 스트리밍 큐: 취소는 요청자 단위다(같은 경로를 기다리는 다른 요청자는 결과를 받는다). 엔진 콘텐츠의 기본 머티리얼을 읽는다(nogpu).

namespace
{
    /** @brief 요청자 하나가 받은 결과입니다. 완료 콜백은 `update` 를 부른 이 스레드에서 돈다. */
    struct RequestResult
    {
        uint32 _callCount{ 0 };
        bool   _bSuccess{ false }; ///< NOLINT(sw-bool-flag): 테스트 지역 기록이다
        size_t _byteCount{ 0 };
    };

    sw::OnStreamingDataCompleteDelegate makeRecorder( RequestResult& result )
    {
        return SW_DELEGATE_LAMBDA( sw::OnStreamingDataCompleteDelegate, [&result]( sw::string_view, bool bSuccess, const sw::vector<uint8>& bytes )
        {
            ++result._callCount;
            result._bSuccess  = bSuccess;
            result._byteCount = bytes.size();
        } );
    }
} // namespace

/**
 * @brief [AssetStreamingQueueTest] 한 요청자의 취소는 그 요청자만 실패로 끝내고, 같은 경로를 기다리는 다른 요청자는 읽은 바이트를 받는다
 * @details 예전에는 `cancelRequest( 경로 )` 가 그 경로의 모든 대기자를 실패로 끝냈다 — 프리로드 세트 하나를 거두면 같은 에셋을 기다리던 로더도 실패를 받았다.
 */
SW_TEST_CASE( AssetStreamingQueueTest, CancelLeavesOtherRequesters )
{
    constexpr const utf8*   kPath = "materials/defaultmaterial.material";
    sw::AssetStreamingQueue queue;
    queue.initialize();

    RequestResult              cancelled;
    RequestResult              kept;
    const sw::StreamingRequest first  = queue.requestAssetData( kPath, sw::StreamingPriority::Normal, makeRecorder( cancelled ) );
    const sw::StreamingRequest second = queue.requestAssetData( kPath, sw::StreamingPriority::Normal, makeRecorder( kept ) );
    SW_REQUIRE_TRUE( first.isValid() && second.isValid() );
    SW_EXPECT_TRUE( first._requestID != second._requestID );
    queue.cancelRequest( first );

    SW_REQUIRE_TRUE( sw::test::waitUntil( [&]()
    {
        queue.update();
        return cancelled._callCount > 0 && kept._callCount > 0;
    } ) );
    queue.shutdown();

    SW_EXPECT_EQUAL( 1u, cancelled._callCount );
    SW_EXPECT_FALSE( cancelled._bSuccess );
    SW_EXPECT_EQUAL( 1u, kept._callCount );
    SW_EXPECT_TRUE( kept._bSuccess );
    SW_EXPECT_TRUE( kept._byteCount > 0 );
    SW_EXPECT_FALSE( queue.requestAssetData( "", sw::StreamingPriority::Normal, {} ).isValid() ); // 받지 못한 요청은 무효다
}

/**
 * @brief [AssetStreamingQueueTest] 프리페치의 완료는 `TaskFuture<bool>` 로도 받는다 — 있는 에셋은 `update` 뒤 true, 받지 못한 요청(빈 경로)은 바로 false 다
 * @details 콜백 대신 값을 기다리는 쪽(`whenAll` · 로딩 화면)이 쓰는 입구다. 무효 요청이 영영 안 끝나는 future 를 돌려주면 기다리는 쪽이 멈춘다 — 바로 끝난 값이어야 한다.
 */
SW_TEST_CASE( AssetStreamingQueueTest, FutureCarriesTheCompletion )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    const sw::TaskFuture<bool> rejected = queue.requestAssetFuture( "" );
    SW_REQUIRE_TRUE( rejected.isReady() );
    SW_EXPECT_FALSE( rejected.get() );

    const sw::TaskFuture<bool> found = queue.requestAssetFuture( "materials/defaultmaterial.material" );
    SW_REQUIRE_TRUE( found.isValid() );
    SW_REQUIRE_TRUE( sw::test::waitUntil( [&]()
    {
        queue.update();
        return found.isReady();
    } ) );
    queue.shutdown();
    SW_EXPECT_TRUE( found.get() );
}
