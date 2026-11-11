// AudioOutputLossTest — 장치 출력을 잃으면 서비스가 오프라인 렌더로 넘기고 백오프 간격으로 기본 장치를 다시 연다(감사 P0-13). 가짜 백엔드 — 장치 스레드 없음.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/IAudioService.h"

namespace
{
    /** @brief 장치가 있는지 테스트가 정하는 백엔드입니다. 스트림은 열린 척만 한다(엔진을 당기지 않는다). */
    class FakeOutputBackend final : public sw::IAudioService
    {
    public:
        int32 _openCount{ 0 };
        bool  _bDeviceAvailable{ true }; // NOLINT(sw-bool-flag): 테스트 고정 장치의 손잡이다

        ~FakeOutputBackend() override { shutdown(); }

    protected:
        bool openDevice( sw::AudioOutputDevice& outDevice ) override
        {
            ++_openCount;
            if ( _bDeviceAvailable == false )
                return false;
            outDevice._channelCount = 2;
            outDevice._channelMask  = sw::AudioChannelLayoutUtil::getChannelMask( sw::AudioChannelLayout::Stereo );
            return true;
        }
        bool startOutput( sw::AudioChannelLayout layout ) override
        {
            (void)layout;
            return true;
        }
    };
} // namespace

/**
 * @brief [AudioOutputLossTest] 잃은 출력은 오프라인으로 넘기고, 장치가 돌아오면 다시 연다
 * @details 예전에는 ALSA 가 복원에 지면 쉬지 않고 다시 썼고 XAudio2 는 유실을 몰라 렌더가 멈췄다 — 재생 끝남을 기다리는 게임 로직이 영영 기다렸다.
 *          변이: `closeLostOutput` 을 빼면 둘째 단언이, `retryOpenOutput` 을 빼면 마지막 단언이 진다.
 */
SW_TEST_CASE( AudioOutputLossTest, LostOutputRendersOfflineAndReopens )
{
    FakeOutputBackend service;
    SW_REQUIRE_TRUE( service.initialize() );
    SW_REQUIRE_TRUE( service.isOutputOpen() );

    service._bDeviceAvailable       = false;
    const int32 openCountBeforeLoss = service._openCount;
    SW_EXPECT_LOG( "AudioService.OutputLost" );
    service.notifyOutputLost(); // 장치 스레드가 알린다
    service.update( 0.1f );     // 닫고 오프라인으로
    SW_EXPECT_FALSE( service.isOutputOpen() );
    for ( int32 tick = 0; tick < 12 && service._openCount == openCountBeforeLoss; ++tick )
        service.update( 0.1f ); // 첫 다시 열기는 [0, 첫 지연(1 초)] 안에 해 본다(지터 — 장치가 아직 없다)
    SW_EXPECT_TRUE( service._openCount > openCountBeforeLoss );
    SW_EXPECT_FALSE( service.isOutputOpen() );

    service._bDeviceAvailable = true; // 헤드폰을 다시 꽂았다
    for ( int32 tick = 0; tick < 40 && service.isOutputOpen() == false; ++tick )
        service.update( 0.1f ); // 백오프(1 · 2 초 …) 안에 다시 연다
    SW_EXPECT_TRUE( service.isOutputOpen() );
}
