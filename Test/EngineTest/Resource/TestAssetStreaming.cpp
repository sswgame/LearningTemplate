#include "pch.h"

#include "Core/File/AsyncFileIO.h"
#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetPinSet.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/PreloadSet.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/TestFramework.h"

// AssetStreamingQueue — 비동기 요청 큐의 완료 통지 · 인플라이트 멀티캐스트 · 퓨처와 동시성 스트레스.

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
    // 이미 결과가 적혔거나(`getCompletedCount`) 둘 중 하나다. 뒷항을 `isLoaded` 로 물으면 안 된다 — 실패한 경로는
    // 로드된 것이 아니다.
    SW_EXPECT_TRUE( queue.isStreaming( "Resource/common/shaders/forward_lit.hlsl" ) || queue.getCompletedCount() > 0 );
    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 실패한 요청은 "로드됨" 이 아니며, 다시 요청하면 재시도된다
 * @details 결과 표에는 성공도 실패도 들어간다. 키의 존재만 보면 둘을 구별하지 못한다 —
 *          그러면 아직 쿠킹하지 않은 셰이더나 늦게 마운트되는 팩을 한 번 헛읽은 뒤로 영원히
 *          "이미 로드됨, 성공" 이라고 답하고 다시는 디스크를 보지 않는다.
 */
SW_TEST_CASE( AssetStreamingTest, FailedRequestIsNotLoadedAndRetries )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    // 존재하지 않는 경로. 파일을 만들지 않으므로 워커는 반드시 실패한다.
    const sw::string missingPathDisk = test::makeTempPath( "sw_test_missing_asset.dat" );
    const sw::string missingPath     = test::makeTempResourceID( missingPathDisk );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( missingPathDisk ) );

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

    // 파일을 만든 뒤 다시 요청하면 기록된 실패를 넘어 **다시 읽어야** 한다.
    const sw::string payload = "RETRY_PAYLOAD";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( missingPathDisk,
                                             reinterpret_cast<const uint8*>( payload.data() ),
                                             static_cast<uint64>( payload.size() ) ) );
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

/**
 * @brief [AssetStreamingTest] 프리로드 묶음은 예산(동시 요청 1) 안에서 경로를 차례로 걸고, 실패를 건너뛰며 끝까지 가서 진행률 1 · 실패 목록을 낸다
 * @details 없는 경로 셋 — 모두 실패로 끝난다. 경로가 없는 묶음은 처음부터 끝났다(진행률 1).
 */
SW_TEST_CASE( AssetStreamingTest, PreloadSetWalksEveryPathWithinTheBudget )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();
    sw::vector<sw::string> listPath;
    for ( const utf8* pName : { "sw_preload_missing_a.dat", "sw_preload_missing_b.dat", "sw_preload_missing_c.dat" } )
    {
        const sw::string diskPath = test::makeTempPath( pName );
        (void)sw::FileUtil::removeFile( diskPath ); // 없는 파일이어야 한다 — 이미 없으면 지우지 못해도 된다
        listPath.push_back( test::makeTempResourceID( diskPath ) );
    }
    sw::PreloadSet set( listPath, sw::StreamingPriority::Normal, 1u );
    SW_EXPECT_EQUAL( 3u, set.getTotalCount() );
    SW_EXPECT_FALSE( set.isComplete() );
    for ( int32 attempt = 0; attempt < 300 && set.isComplete() == false; ++attempt )
    {
        set.update( queue );
        queue.update();
        if ( set.isComplete() )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
    SW_EXPECT_TRUE( set.isComplete() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, set.getProgress(), 1e-6f );
    SW_EXPECT_EQUAL( size_t( 3 ), set.getFailedAssets().size() );

    const sw::PreloadSet empty( {}, sw::StreamingPriority::Normal, 1u );
    SW_EXPECT_TRUE( empty.isComplete() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, empty.getProgress(), 0.0f );
    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 고정 묶음은 예산을 넘으면 마지막으로 쓴 때가 가장 오래된 것부터 놓고, 혼자 예산보다 큰 것은 붙들지 않는다
 * @details 토큰은 놓이면 세는 삭제자를 단 값이다 — 묶음이 놓은 순간이 보인다.
 */
SW_TEST_CASE( AssetStreamingTest, PinSetEvictsLeastRecentlyUsedOverBudget )
{
    uint32     releasedCount = 0;
    const auto makePin       = [&releasedCount]()
    {
        return sw::shared_ptr<const void>( &releasedCount, [&releasedCount]( const uint32* )
        { ++releasedCount; } );
    };
    sw::AssetPinSet pins( 100 );
    SW_EXPECT_TRUE( pins.add( "a.mesh", makePin(), 40 ) );
    SW_EXPECT_TRUE( pins.add( "b.mesh", makePin(), 40 ) );
    pins.touch( "a.mesh" ); // 이제 b 가 가장 오래 안 썼다
    SW_EXPECT_TRUE( pins.add( "c.mesh", makePin(), 40 ) );
    SW_EXPECT_TRUE( pins.contains( "a.mesh" ) );
    SW_EXPECT_FALSE( pins.contains( "b.mesh" ) );
    SW_EXPECT_TRUE( pins.contains( "c.mesh" ) );
    SW_EXPECT_EQUAL( 1u, releasedCount );
    SW_EXPECT_EQUAL( uint64( 80 ), pins.getPinnedBytes() );

    // 이미 붙든 경로는 토큰을 바꾸지 않는다(새 토큰은 바로 놓인다).
    SW_EXPECT_TRUE( pins.add( "A.MESH", makePin(), 40 ) );
    SW_EXPECT_EQUAL( 2u, releasedCount );
    SW_EXPECT_EQUAL( size_t( 2 ), pins.getCount() );

    {
        test::ScopedDefensiveTestLog expected( "an asset larger than the whole pin budget is not pinned" );
        SW_EXPECT_FALSE( pins.add( "huge.mesh", makePin(), 101 ) );
    }
    SW_EXPECT_EQUAL( size_t( 2 ), pins.getCount() );
    SW_EXPECT_FALSE( pins.add( "empty.mesh", sw::shared_ptr<const void>{}, 1 ) );

    pins.clear();
    SW_EXPECT_EQUAL( 5u, releasedCount );
    SW_EXPECT_EQUAL( uint64( 0 ), pins.getPinnedBytes() );
}

/**
 * @brief [AssetStreamingTest] 고정을 켠 프리로드 묶음은 읽은 메시를 캐시에 붙들고, 고정하지 않는 종류는 읽기만 하며, 묶음이 사라지면 놓는다
 * @details 메시 캐시는 약한 참조라 묶음이 붙들지 않으면 읽은 메시가 쓰기 전에 사라진다(계획 PreloadAndLod P4).
 */
SW_TEST_CASE( AssetStreamingTest, PreloadSetPinsMeshesUntilReleased )
{
    const sw::string          meshDisk = test::makeTempPath( "sw_preload_pin.mesh" );
    const sw::string          dataDisk = test::makeTempPath( "sw_preload_pin.dat" );
    sw::vector<sw::RHIVertex> listVertex( 3 );
    listVertex[1]._arrPosition[1] = 1.0f;
    listVertex[2]._arrPosition[0] = 1.0f;
    for ( sw::RHIVertex& vertex : listVertex )
    {
        vertex._arrNormal[2] = 1.0f;
    }
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( meshDisk, listVertex ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( dataDisk, "bytes only" ) );
    const sw::string meshPath = test::makeTempResourceID( meshDisk );
    const sw::string dataPath = test::makeTempResourceID( dataDisk );

    sw::AssetStreamingQueue queue;
    queue.initialize();
    sw::MeshCache cache;
    {
        sw::PreloadSet set( { meshPath, dataPath }, sw::StreamingPriority::Normal, 2u );
        set.enablePinning( sw::engine::getSubsystem<sw::AssetManager>(), 1024u * 1024u );
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 5000 );
        while ( set.isComplete() == false && deadline.isExpired() == false )
        {
            set.update( queue );
            queue.update();
            std::this_thread::yield();
        }
        SW_ASSERT_TRUE( set.isComplete() );
        SW_EXPECT_TRUE( set.getFailedAssets().empty() );
        SW_ASSERT_NOT_NULL( set.findPins() );
        SW_EXPECT_TRUE( set.findPins()->contains( meshPath ) );
        SW_EXPECT_FALSE( set.findPins()->contains( dataPath ) ); // 고정하는 캐시가 없는 종류는 읽기만 한다
        sw::engine::getSubsystem<sw::TaskManager>().waitAll();
        SW_EXPECT_TRUE( cache.isCached( meshPath ) );
    }
    sw::engine::getSubsystem<sw::TaskManager>().waitAll();
    SW_EXPECT_FALSE( cache.isCached( meshPath ) );
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
        const bool bAllCallbacksDone = callback1Count > 0 && callback2Count > 0 && callback3Count > 0;
        if ( bAllCallbacksDone )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }

    SW_EXPECT_EQUAL( 1, callback1Count );
    SW_EXPECT_EQUAL( 1, callback2Count );
    SW_EXPECT_EQUAL( 1, callback3Count );

    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] TaskFuture 및 SpscQueue 기반 에셋 스트리밍 완료 검증
 */
SW_TEST_CASE( AssetStreamingTest, StreamingFutureAndSpscQueue )
{
    const sw::string tempFileDisk = test::makeTempPath( "sw_test_streaming_asset.dat" );
    const sw::string tempFile     = test::makeTempResourceID( tempFileDisk );
    const sw::string testData     = "STREAMING_TEST_PAYLOAD";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( tempFileDisk,
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
        sw::engine::getSubsystem<sw::TaskManager>().waitAll();

    queue.update( 32 );

    SW_EXPECT_TRUE( futSuccess.isReady() );
    SW_EXPECT_TRUE( futSuccess.get() );
    SW_EXPECT_TRUE( bCallbackRan );
    SW_EXPECT_TRUE( queue.isLoaded( tempFile ) );

    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 다중 스레드 동시 스트리밍 및 락-프리 큐 펌프 스트레스 테스트
 */
SW_TEST_CASE( AssetStreamingTest, MultiThreadedConcurrentStreamingStress )
{
    constexpr int32 kFileCount = 8;
    constexpr int32 kWorkers   = 4;

    sw::vector<sw::string> listTempFile; // 쓰고 지우는 디스크 경로
    sw::vector<sw::string> listTempID;   // 스트리밍 큐가 읽는 리소스 id
    listTempFile.reserve( kFileCount );

    for ( int32 fileIndex = 0; fileIndex < kFileCount; ++fileIndex )
    {
        const sw::string path =
            test::makeTempPath( ( sw::string( "sw_stress_asset_" ) + sw::string( std::to_string( fileIndex ).c_str() ) + ".dat" ).c_str() );
        const sw::string data = "STRESS_DATA_BLOCK";
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( data.data() ), data.size() ) );
        listTempFile.push_back( path );
        listTempID.push_back( test::makeTempResourceID( path ) );
    }

    sw::AssetStreamingQueue queue;
    queue.initialize();

    sw::atomic<int32> countCallback{ 0 };

    // 4개 생산자 스레드가 동시에 8개 파일에 대해 비동기 Future 요청 발행
    sw::vector<std::thread> listThread;
    listThread.reserve( kWorkers );

    for ( int32 threadIndex = 0; threadIndex < kWorkers; ++threadIndex )
    {
        listThread.emplace_back( [&queue, &listTempID, &countCallback]()
        {
            for ( int32 cycle = 0; cycle < 10; ++cycle )
            {
                for ( const sw::string& path : listTempID )
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
    {
        workerThread.join();
    }

    // 백그라운드 태스크 대기 및 락-프리 완료 큐 드레인
    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getSubsystem<sw::TaskManager>().waitAll();

    for ( int32 drainStep = 0; drainStep < 50; ++drainStep )
    {
        queue.update( 64 );
        std::this_thread::yield();
    }

    for ( const sw::string& path : listTempFile )
    {
        SW_EXPECT_TRUE( queue.isLoaded( test::makeTempResourceID( path ) ) );
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( path ) );
    }

    SW_EXPECT_TRUE( countCallback.load() > 0 );
    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 데이터 요청이 존재 확인 요청에 편승해 빈 버퍼를 받지 않는지 검증
 * @details `requestAsset`(있는지만 본다)과 `requestAssetData`(바이트를 읽는다)가 진행 중 목록
 *          하나를 공유하면, 존재 확인이 아직 돌고 있을 때 들어온 데이터 요청이 그 태스크에 **편승**하는데
 *          그 태스크는 파일을 읽지 않는다 — 데이터 콜백이 `bSuccess = true` 와 **빈 버퍼**를 받는다.
 *          성공이라고 말하면서 아무것도 주지 않는, 가장 나쁜 모양의 틀린 답이다.
 *
 *          창이 좁은 레이스라 라운드를 여러 번 돌린다. 편승이 일어나지 않은 라운드는 어차피
 *          올바른 길을 타므로 통과한다 — 이 케이스는 **편승했을 때만** 진다.
 */
SW_TEST_CASE( AssetStreamingTest, DataRequestDoesNotPiggybackOnAnExistenceCheck )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    // **진짜로 읽히는 경로여야 한다.** 없는 경로를 쓰면 데이터 콜백이 `bSuccess = false` 로
    // 오고, "성공인데 바이트가 없다" 는 이 케이스의 단언을 지나가 버린다(변이가 통과한다).
    // 프리셋마다 리소스가 어디에 있는지 다르므로 — Shipping 은 팩만 읽는다 —
    // **직접 만든 파일을 절대 경로로** 준다. `ResourcePaths` 은 절대 경로를 디스크에서 그대로
    // 읽으므로 어느 프리셋에서나 같은 답이 나온다.
    const sw::string assetPathDisk = test::makeTempPath( "sw_stream_piggyback.bin" );
    const sw::string assetPath     = test::makeTempResourceID( assetPathDisk );

    const sw::vector<uint8> payload( 4096, uint8{ 0xAB } );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( assetPathDisk, payload.data(), payload.size() ) );

    const utf8* pAssetPath = assetPath.c_str();

    sw::vector<uint8> directBytes;
    SW_ASSERT_TRUE( sw::ResourceIO::readBinaryResource( pAssetPath, directBytes ) );
    SW_ASSERT_TRUE( directBytes.empty() == false );

    constexpr uint32 kRound = 32;

    for ( uint32 round = 0; round < kRound; ++round )
    {
        bool   bExistenceDone = false;
        bool   bDataDone      = false;
        bool   bDataSuccess   = false;
        size_t dataByteCount  = 0;

        // 존재 확인을 먼저 띄우고, 그것이 끝나기 전에 데이터를 요청한다.
        queue.requestAsset( pAssetPath, sw::StreamingPriority::Normal,
                            SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&bExistenceDone]( sw::string_view, bool )
        {
            bExistenceDone = true;
        } ) );

        queue.requestAssetData( pAssetPath, sw::StreamingPriority::Normal,
                                SW_DELEGATE_LAMBDA( sw::OnStreamingDataCompleteDelegate,
                                                    [&bDataDone, &bDataSuccess, &dataByteCount]( sw::string_view, bool bSuccess, const sw::vector<uint8>& bytes )
        {
            bDataDone     = true;
            bDataSuccess  = bSuccess;
            dataByteCount = bytes.size();
        } ) );

        for ( int32 attempt = 0; attempt < 200; ++attempt )
        {
            queue.update();
            if ( bExistenceDone && bDataDone )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        }

        SW_ASSERT_TRUE( bDataDone );

        // 위에서 직접 읽어 봤으므로 이 경로는 반드시 성공이고 바이트가 있어야 한다.
        // 편승했으면 여기서 성공인데 0 바이트가 나온다.
        SW_EXPECT_TRUE( bDataSuccess );
        SW_EXPECT_EQUAL( directBytes.size(), dataByteCount );

        // 다음 라운드가 `_mapAssetResult` 지름길로 빠지지 않도록 기록을 비운다.
        queue.clearCompletionRecord();
    }

    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 스트리밍 우선순위는 태스크 우선순위로 실린다 — High 는 Normal 줄, Low · Normal 은 백그라운드 줄, 어느 것도 High 줄이 아니다
 */
SW_TEST_CASE( AssetStreamingTest, StreamingPriorityMapsOntoTaskPriority )
{
    SW_EXPECT_TRUE( sw::AssetStreamingQueue::toTaskPriority( sw::StreamingPriority::Low ) == sw::TaskPriority::Low );
    SW_EXPECT_TRUE( sw::AssetStreamingQueue::toTaskPriority( sw::StreamingPriority::Normal ) == sw::TaskPriority::Low );
    SW_EXPECT_TRUE( sw::AssetStreamingQueue::toTaskPriority( sw::StreamingPriority::High ) == sw::TaskPriority::Normal );
    SW_EXPECT_TRUE( sw::AssetStreamingQueue::toTaskPriority( sw::StreamingPriority::Immediate ) == sw::TaskPriority::Normal );
}

/**
 * @brief [AssetStreamingTest] 나중에 낸 High 요청이 먼저 낸 Normal 요청보다 먼저 처리된다
 * @details 워커를 모두 문 앞에 세운 채 Normal 요청 하나, High 요청 하나를 차례로 낸다. 문을 **워커 하나에게만** 열면 그 워커가 두 요청을
 *          차례로 처리하므로 완료 순서가 곧 처리 순서다. 우선순위를 싣지 않으면 둘 다 같은 줄(FIFO)이라 Normal 이 먼저 끝난다.
 */
SW_TEST_CASE( AssetStreamingTest, HighPriorityRequestOvertakesAnEarlierNormalOne )
{
    SW_ASSERT_TRUE( sw::engine::areEngineServicesBound() );
    sw::TaskManager& taskMgr = sw::engine::getSubsystem<sw::TaskManager>();
    taskMgr.initialize();
    const uint32 workerCount = taskMgr.getWorkerCount();
    SW_ASSERT_TRUE( workerCount >= 1 );

    const sw::string normalPathDisk = test::makeTempPath( "sw_test_streaming_normal.dat" );
    const sw::string normalPath     = test::makeTempResourceID( normalPathDisk );
    const sw::string highPathDisk   = test::makeTempPath( "sw_test_streaming_high.dat" );
    const sw::string highPath       = test::makeTempResourceID( highPathDisk );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( normalPathDisk, "normal" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( highPathDisk, "high" ) );

    // 지역 구조체는 정적 멤버를 못 가진다 — 함수 지역 static 으로 둔다.
    static sw::atomic<uint32> s_ticketCount{ 0 };
    static sw::atomic<uint32> s_releasedCount{ 0 };
    static sw::atomic<uint32> s_runningCount{ 0 };
    s_ticketCount   = 0;
    s_releasedCount = 0;
    s_runningCount  = 0;
    struct GateContext
    {
        /** @brief 표를 받고, 풀린 수가 그 표를 넘을 때까지 워커를 붙든다. 끝은 시간으로 잡는다(고장 나도 CI 가 매달리지 않게). */
        static void hold()
        {
            const uint32 ticket = s_ticketCount.fetch_add( 1, std::memory_order_acq_rel );
            s_runningCount.fetch_add( 1, std::memory_order_acq_rel );
            const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 5000 );
            while ( s_releasedCount.load( std::memory_order_acquire ) <= ticket && deadline.isExpired() == false )
            {
                std::this_thread::yield();
            }
        }
    };
    for ( uint32 index = 0; index < workerCount; ++index )
    {
        sw::TaskHandle handle = taskMgr.emplaceTask( "StreamingGate", SW_DELEGATE_FUNCTION( sw::TaskDelegate, GateContext::hold ) );
        SW_ASSERT_TRUE( handle.isValid() );
        handle.submit();
    }
    {
        const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 2000 );
        while ( s_runningCount.load( std::memory_order_acquire ) < workerCount && deadline.isExpired() == false )
        {
            std::this_thread::yield();
        }
    }
    SW_ASSERT_EQUAL( workerCount, s_runningCount.load() );

    sw::AssetStreamingQueue queue;
    queue.initialize();
    sw::vector<sw::string> listCompleted;
    const auto             recordCompletion = SW_DELEGATE_LAMBDA( sw::OnStreamingCompleteDelegate, [&listCompleted]( sw::string_view path, bool )
                { listCompleted.push_back( sw::string( path ) ); } );
    SW_EXPECT_TRUE( queue.requestAsset( normalPath, sw::StreamingPriority::Normal, recordCompletion ) );
    SW_EXPECT_TRUE( queue.requestAsset( highPath, sw::StreamingPriority::High, recordCompletion ) );

    // 워커 하나만 푼다 — 그 워커가 두 요청을 차례로 처리한다.
    s_releasedCount.store( 1, std::memory_order_release );
    const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 5000 );
    while ( listCompleted.size() < 2 && deadline.isExpired() == false )
    {
        queue.update();
        std::this_thread::yield();
    }

    s_releasedCount.store( workerCount, std::memory_order_release );
    const bool bAllDone = taskMgr.waitAll( 5000 );
    queue.shutdown();
    if ( bAllDone == false )
        taskMgr.clear(); // 남은 태스크를 다음 테스트가 영원히 기다리면 안 된다
    SW_ASSERT_TRUE( bAllDone );

    SW_ASSERT_EQUAL( size_t( 2 ), listCompleted.size() );
    SW_EXPECT_EQUAL( highPath, listCompleted[0] );
    SW_EXPECT_EQUAL( normalPath, listCompleted[1] );
}

/**
 * @brief [AssetStreamingTest] 한 경로에 몰린 데이터 콜백이 1024 개를 넘어도 모두 불린다
 * @details 완료 줄이 고정 용량(1024)이면 같은 경로에 편승한 콜백이 한꺼번에 들어갈 때 넘친 것을 말없이 버린다 — 요청한 쪽은 영영 답을 받지 못한다.
 */
SW_TEST_CASE( AssetStreamingTest, ManyCallbacksOnOnePathAreAllDelivered )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    const sw::string        assetPathDisk = test::makeTempPath( "sw_stream_fanout.bin" );
    const sw::string        assetPath     = test::makeTempResourceID( assetPathDisk );
    const sw::vector<uint8> payload( 1024, uint8{ 0x5A } );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( assetPathDisk, payload.data(), payload.size() ) );

    constexpr uint32    kCallbackCount = 1500;
    std::atomic<uint32> okCount{ 0 };
    std::atomic<uint32> callbackCount{ 0 };
    for ( uint32 index = 0; index < kCallbackCount; ++index )
    {
        SW_ASSERT_TRUE( queue.requestAssetData( assetPath, sw::StreamingPriority::Normal,
                                                SW_DELEGATE_LAMBDA( sw::OnStreamingDataCompleteDelegate, [&]( sw::string_view, bool bSuccess, const sw::vector<uint8>& bytes )
        {
            callbackCount.fetch_add( 1 );
            const bool bReadSucceeded = bSuccess && bytes.size() == 1024 && bytes[0] == 0x5A;
            if ( bReadSucceeded )
                okCount.fetch_add( 1 );
        } ) ) );
    }

    const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 10000 );
    while ( callbackCount.load() < kCallbackCount && deadline.isExpired() == false )
    {
        queue.update( kCallbackCount );
        std::this_thread::yield();
    }
    SW_EXPECT_EQUAL( callbackCount.load(), kCallbackCount );
    SW_EXPECT_EQUAL( okCount.load(), kCallbackCount );
    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 데이터 요청을 취소하면 실패로 한 번만 끝나고, 늦게 끝난 읽기는 다시 부르지 않는다
 */
SW_TEST_CASE( AssetStreamingTest, CanceledDataRequestCompletesOnceAsFailure )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    const sw::string        assetPathDisk = test::makeTempPath( "sw_stream_cancel.bin" );
    const sw::string        assetPath     = test::makeTempResourceID( assetPathDisk );
    const sw::vector<uint8> payload( 256 * 1024, uint8{ 0x11 } );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( assetPathDisk, payload.data(), payload.size() ) );

    std::atomic<uint32> callbackCount{ 0 };
    std::atomic<uint32> successCount{ 0 };
    SW_ASSERT_TRUE( queue.requestAssetData( assetPath, sw::StreamingPriority::Low,
                                            SW_DELEGATE_LAMBDA( sw::OnStreamingDataCompleteDelegate, [&]( sw::string_view, bool bSuccess, const sw::vector<uint8>& )
    {
        callbackCount.fetch_add( 1 );
        if ( bSuccess )
            successCount.fetch_add( 1 );
    } ) ) );
    queue.cancelRequest( assetPath );
    SW_EXPECT_FALSE( queue.isStreaming( assetPath ) );

    // 걸려 있던 읽기가 끝날 시간을 준다 — 그 완료는 세대가 지나 버려져야 한다.
    sw::engine::getSubsystem<sw::AsyncFileIO>().waitIdle();
    sw::engine::getSubsystem<sw::TaskManager>().waitAll();
    for ( uint32 pump = 0; pump < 4; ++pump )
    {
        queue.update();
    }

    SW_EXPECT_EQUAL( callbackCount.load(), 1u );
    SW_EXPECT_EQUAL( successCount.load(), 0u );
    SW_EXPECT_FALSE( queue.isLoaded( assetPath ) );
    queue.shutdown();
}

/**
 * @brief [AssetStreamingTest] 읽기가 끝났지만 `update` 가 아직 전달하지 않은 요청을 취소하면 실패로 한 번 오고, 로드됨으로 남지 않는다
 * @details 취소와 완료는 다른 스레드라 작은 파일은 취소보다 먼저 끝날 수 있다. 그 순서를 강제로 만든다 — 읽기를 끝까지 기다린 뒤 취소한다.
 */
SW_TEST_CASE( AssetStreamingTest, CancelAfterReadFinishedButBeforeUpdateDeliversFailure )
{
    sw::AssetStreamingQueue queue;
    queue.initialize();

    const sw::string        assetPathDisk = test::makeTempPath( "sw_stream_cancel_late.bin" );
    const sw::string        assetPath     = test::makeTempResourceID( assetPathDisk );
    const sw::vector<uint8> payload( 1024, uint8{ 0x22 } );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( assetPathDisk, payload.data(), payload.size() ) );

    std::atomic<uint32> callbackCount{ 0 };
    std::atomic<uint32> successCount{ 0 };
    SW_ASSERT_TRUE( queue.requestAssetData( assetPath, sw::StreamingPriority::High,
                                            SW_DELEGATE_LAMBDA( sw::OnStreamingDataCompleteDelegate, [&]( sw::string_view, bool bSuccess, const sw::vector<uint8>& )
    {
        callbackCount.fetch_add( 1 );
        if ( bSuccess )
            successCount.fetch_add( 1 );
    } ) ) );

    // 찾기 태스크 → 비동기 읽기 → 완료 태스크가 모두 끝나 완료가 줄에 들어갈 때까지 기다린다(`update` 는 아직 부르지 않는다).
    const sw::Deadline deadline = sw::Deadline::afterMilliseconds( 5000 );
    while ( queue.isStreaming( assetPath ) && deadline.isExpired() == false )
    {
        sw::engine::getSubsystem<sw::TaskManager>().waitAll();
        sw::engine::getSubsystem<sw::AsyncFileIO>().waitIdle();
    }
    SW_ASSERT_FALSE( queue.isStreaming( assetPath ) );
    SW_ASSERT_TRUE( queue.isLoaded( assetPath ) );

    queue.cancelRequest( assetPath );
    for ( uint32 pump = 0; pump < 4; ++pump )
    {
        queue.update();
    }

    SW_EXPECT_EQUAL( 1u, callbackCount.load() );
    SW_EXPECT_EQUAL( 0u, successCount.load() );
    SW_EXPECT_FALSE( queue.isLoaded( assetPath ) );
    queue.shutdown();
}
