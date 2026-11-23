// AudioRenderThread — 장치 콜백 대신 앞서 렌더하는 스레드와 링: 켜자마자 꺼낼 것이 있고, 모자라면 무음으로 채워 언더런을 센다. 장치 없음.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Audio/AudioRenderThread.h"
#include "Engine/Audio/Tests/OfflineAudioEngine.h"

using namespace sw;

namespace
{
    /** @brief 채널 수(스테레오)입니다. */
    constexpr uint32 kChannelCount = 2;
} // namespace

/**
 * @brief [AudioRenderThreadTest] 켜면 링이 차 있어 첫 콜백이 모자라지 않고, 꺼낸 만큼 렌더 스레드가 다시 채운다
 */
SW_TEST_CASE( AudioRenderThreadTest, FirstReadsAreServedAndTheRingRefills )
{
    test::OfflineAudioEngine offline;
    offline.getEngine().releaseAudioThread();
    AudioRenderThread renderThread{ offline.getEngine() };
    renderThread.start( kChannelCount );

    vector<float32> listOutput( static_cast<size_t>( AudioRenderThread::kRenderFrameCount ) * kChannelCount, 1.0f );
    const uint32    readCount = AudioRenderThread::kRingFrameCount / AudioRenderThread::kRenderFrameCount;
    for ( uint32 index = 0; index < readCount; ++index )
    {
        renderThread.read( listOutput.data(), AudioRenderThread::kRenderFrameCount );
    }
    SW_EXPECT_EQUAL( uint64{ 0 }, renderThread.getUnderrunCount() ); // 채워 둔 링만으로 여기까지 모자라지 않는다

    // 렌더 스레드가 다시 채우면 게시한 렌더 프레임 수가 링 하나를 넘는다(기다림의 상한은 시한일 뿐이다).
    const Deadline deadline = Deadline::afterMilliseconds( 10000 );
    while ( offline.getEngine().getStats()._renderedFrameCount <= AudioRenderThread::kRingFrameCount && deadline.isExpired() == false )
    {
        ThreadUtil::yield();
    }
    SW_EXPECT_TRUE( offline.getEngine().getStats()._renderedFrameCount > AudioRenderThread::kRingFrameCount );
    renderThread.stop();
    offline.getEngine().releaseAudioThread();
}

/**
 * @brief [AudioRenderThreadTest] 링보다 많이 꺼내면 나머지는 무음이고 언더런을 한 번 센다
 */
SW_TEST_CASE( AudioRenderThreadTest, ShortReadIsSilenceAndCounted )
{
    test::OfflineAudioEngine offline;
    offline.getEngine().releaseAudioThread();
    AudioRenderThread renderThread{ offline.getEngine() };
    renderThread.start( kChannelCount );
    renderThread.stop(); // 채우는 쪽을 멈춰 모자람을 만든다

    const uint32    frameCount = AudioRenderThread::kRingFrameCount + AudioRenderThread::kRenderFrameCount;
    vector<float32> listOutput( static_cast<size_t>( frameCount ) * kChannelCount, 1.0f );
    renderThread.read( listOutput.data(), frameCount );
    SW_EXPECT_EQUAL( uint64{ 1 }, renderThread.getUnderrunCount() );
    SW_EXPECT_EQUAL( 0.0f, listOutput.back() );

    renderThread.read( listOutput.data(), AudioRenderThread::kRenderFrameCount );
    SW_EXPECT_EQUAL( uint64{ 2 }, renderThread.getUnderrunCount() );
    SW_EXPECT_EQUAL( 0.0f, listOutput[0] );
    offline.getEngine().releaseAudioThread();
}
