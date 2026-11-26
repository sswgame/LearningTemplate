// AudioSubsystemTest — 장치 출력을 잃으면 서비스가 오프라인 렌더로 넘기고 백오프 간격으로 기본 장치를 다시 연다(감사 P0-13). 가짜 출력 장치 — 장치 스레드 없음.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioSubsystem.h"
#include "Engine/Config/EngineConfig.h"

namespace
{
    /**
     * @brief 장치가 있는지 테스트가 정하는 출력 장치입니다. 스트림은 열린 척만 한다(엔진을 당기지 않는다).
     * @details 서비스가 장치를 만들어 들어(합성) 테스트는 손잡이를 정적 칸으로 쥔다 — 케이스마다 `reset`.
     */
    class FakeOutputDevice final : public sw::IAudioOutputDevice
    {
    public:
        static inline int32 s_openCount        = 0;
        static inline bool  s_bDeviceAvailable = true;

        static inline sw::AudioChannelLayout s_deviceLayout = sw::AudioChannelLayout::Stereo; ///< 장치가 알리는 채널 배치다

        static void reset()
        {
            s_openCount        = 0;
            s_bDeviceAvailable = true;
            s_deviceLayout     = sw::AudioChannelLayout::Stereo;
        }
        static sw::unique_ptr<sw::IAudioOutputDevice> create( sw::AudioSubsystem& /*service*/ ) { return sw::make_unique<FakeOutputDevice>(); }

        bool openDevice( sw::AudioOutputDevice& outDevice ) override
        {
            ++s_openCount;
            if ( s_bDeviceAvailable == false )
                return false;
            outDevice._channelCount = sw::AudioChannelLayoutUtil::getChannelCount( s_deviceLayout );
            outDevice._channelMask  = sw::AudioChannelLayoutUtil::getChannelMask( s_deviceLayout );
            return true;
        }
        bool startOutput( sw::AudioChannelLayout layout ) override
        {
            (void)layout;
            return true;
        }
        void   stopOutput() override {}
        void   closeDevice() override {}
        uint64 getUnderrunCount() const override { return 0; }
    };

    /** @brief 엔진 설정 파일과 같은 다시 열기 간격 · 캐시 예산입니다(첫 1 초, 상한 30 초, 192 MB — `Config/Engine/EngineConfig.json`). */
    sw::AudioConfig makeOutputConfig()
    {
        sw::AudioConfig config;
        config._reopenFirstDelayMs    = 1000;
        config._reopenMaxDelayMs      = 30000;
        config._clipCacheMegabytes    = 192;
        config._musicCrossfadeSeconds = 0.25f;
        return config;
    }
} // namespace

/**
 * @brief [AudioSubsystemTest] 잃은 출력은 오프라인 렌더로 넘기고(흐른 시간만큼만), 장치가 돌아오면 다시 연다
 * @details 예전에는 ALSA 가 복원에 지면 쉬지 않고 다시 썼고(오디오 시계가 실시간보다 빨리 흘렀다) XAudio2 는 유실을 몰라 렌더가 멈췄다 — 재생 끝남을 기다리는 게임 로직이 영영 기다렸다.
 *          변이: `closeLostOutput` 을 빼면 오프라인 단언이, 오프라인 렌더를 빼면 프레임 아래 끝이, `retryOpenOutput` 을 빼면 마지막 단언이 진다.
 */
SW_TEST_CASE( AudioSubsystemTest, OutputLossFallsBackToOfflineRender )
{
    FakeOutputDevice::reset();
    sw::AudioSubsystem service{ &FakeOutputDevice::create };
    SW_REQUIRE_TRUE( service.initialize( makeOutputConfig() ) );
    SW_REQUIRE_TRUE( service.isOutputOpen() );

    FakeOutputDevice::s_bDeviceAvailable = false;

    const int32  openCountBeforeLoss = FakeOutputDevice::s_openCount;
    const uint64 framesBeforeLoss    = service.getEngine().getStats()._renderedFrameCount;
    SW_EXPECT_LOG( "AudioSubsystem.OutputLost" );
    service.notifyOutputLost(); // 장치 스레드가 알린다
    service.update( 0.1f );     // 닫고 오프라인으로
    SW_EXPECT_FALSE( service.isOutputOpen() );
    int32 offlineTickCount = 1;
    for ( int32 tick = 0; tick < 12 && FakeOutputDevice::s_openCount == openCountBeforeLoss; ++tick, ++offlineTickCount )
        service.update( 0.1f ); // 첫 다시 열기는 [0, 첫 지연(1 초)] 안에 해 본다(지터 — 장치가 아직 없다)
    SW_EXPECT_TRUE( FakeOutputDevice::s_openCount > openCountBeforeLoss );
    SW_EXPECT_FALSE( service.isOutputOpen() );

    // 오프라인 렌더는 흐른 시간만큼이다 — 넘으면 시계가 빨리 흐르고(옛 ALSA), 블록 하나보다 모자라면 렌더가 멈춘 것이다(옛 XAudio2).
    const uint64 renderedFrames = service.getEngine().getStats()._renderedFrameCount - framesBeforeLoss;
    const uint64 elapsedFrames  = static_cast<uint64>( offlineTickCount ) * ( sw::audio::kSampleRate / 10 );
    SW_EXPECT_TRUE( renderedFrames <= elapsedFrames );
    SW_EXPECT_TRUE( renderedFrames + sw::audio::kBlockFrameCount > elapsedFrames );

    FakeOutputDevice::s_bDeviceAvailable = true; // 헤드폰을 다시 꽂았다
    for ( int32 tick = 0; tick < 40 && service.isOutputOpen() == false; ++tick )
        service.update( 0.1f ); // 백오프(1 · 2 초 …) 안에 다시 연다
    SW_EXPECT_TRUE( service.isOutputOpen() );
}

/**
 * @brief [AudioSubsystemTest] 5.1 장치를 잃고 스테레오 장치가 기본이 되면(헤드폰을 꽂음) 자동 스피커 모드는 스테레오로 내려 다시 연다
 * @details 변이: 다시 열 때 지금 레이아웃(5.1)의 채널 수만 보면 스테레오 장치를 끝없이 거절해 출력이 열리지 않는다.
 */
SW_TEST_CASE( AudioSubsystemTest, ReopenFollowsTheNewDeviceLayout )
{
    FakeOutputDevice::reset();
    FakeOutputDevice::s_deviceLayout = sw::AudioChannelLayout::Surround51;
    sw::AudioSubsystem service{ &FakeOutputDevice::create };
    SW_REQUIRE_TRUE( service.initialize( makeOutputConfig() ) );
    SW_REQUIRE_TRUE( service.getEngine().getChannelLayout() == sw::AudioChannelLayout::Surround51 );

    FakeOutputDevice::s_deviceLayout = sw::AudioChannelLayout::Stereo;
    SW_EXPECT_LOG( "AudioSubsystem.OutputLost" );
    service.notifyOutputLost();
    for ( int32 tick = 0; tick < 40 && ( tick == 0 || service.isOutputOpen() == false ); ++tick )
        service.update( 0.1f ); // 백오프(1 · 2 초 …) 안에 다시 연다
    SW_EXPECT_TRUE( service.isOutputOpen() );
    SW_EXPECT_TRUE( service.getEngine().getChannelLayout() == sw::AudioChannelLayout::Stereo );
}

/** @brief [AudioSubsystemTest] 다시 열기 상한이 첫 간격보다 작으면 초기화가 실패한다 — 코드 기본값으로 대신하지 않는다. */
SW_TEST_CASE( AudioSubsystemTest, InvalidReopenDelayFailsInitialize )
{
    FakeOutputDevice::reset();
    sw::AudioSubsystem service{ &FakeOutputDevice::create };
    sw::AudioConfig    config = makeOutputConfig();
    config._reopenMaxDelayMs  = config._reopenFirstDelayMs - 1;
    SW_EXPECT_LOG( "AudioSubsystem.InvalidReopenDelay" );
    SW_EXPECT_FALSE( service.initialize( config ) );
    SW_EXPECT_EQUAL( 0, FakeOutputDevice::s_openCount );
}

/**
 * @brief [AudioSubsystemTest] 앱이 멈추면 출력을 닫고 멈춘 동안은 렌더 · 다시 열기를 하지 않으며, 돌아오면 다음 갱신이 바로 연다(감사 P-6)
 * @details Android onPause · 브라우저 탭 숨김에 소리가 계속 나던 자리다. 변이: `update` 의 멈춤 확인을 빼면 렌더 단언이, `resumeOutput` 의 대기 0 을 빼면 마지막 단언이 진다.
 */
SW_TEST_CASE( AudioSubsystemTest, SuspendClosesOutputUntilResume )
{
    FakeOutputDevice::reset();
    sw::AudioSubsystem service{ &FakeOutputDevice::create };
    SW_REQUIRE_TRUE( service.initialize( makeOutputConfig() ) );
    SW_REQUIRE_TRUE( service.isOutputOpen() );

    service.suspendOutput();
    SW_EXPECT_TRUE( service.isOutputSuspended() );
    SW_EXPECT_FALSE( service.isOutputOpen() );
    const int32  openCount = FakeOutputDevice::s_openCount;
    const uint64 frames    = service.getEngine().getStats()._renderedFrameCount;
    for ( int32 tick = 0; tick < 30; ++tick )
        service.update( 0.1f );
    SW_EXPECT_EQUAL( openCount, FakeOutputDevice::s_openCount );
    SW_EXPECT_EQUAL( frames, service.getEngine().getStats()._renderedFrameCount );

    service.resumeOutput();
    SW_EXPECT_FALSE( service.isOutputSuspended() );
    service.update( 0.0f ); // 백오프를 기다리지 않는다
    SW_EXPECT_TRUE( service.isOutputOpen() );
}
