// AudioSurroundTest — 5.1 · 7.1 패너(에너지 보존 · 방위 연속 · 스테레오 회귀), 다채널 클립의 명시 다운믹스, 레이아웃 협상, 바이노럴. 엔진 렌더 없음.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioBinaural.h"
#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioDownmix.h"
#include "Engine/Audio/AudioPanner.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/UserSettings/UserSettingsEngineHandlers.h"

namespace
{
    struct AudioSurroundTestInternal
    {
        /** @brief 앞 기준 방위 @p degrees(오른쪽 +)의 점 음원 방향입니다. */
        static sw::AudioPanDirection makeAzimuth( float32 degrees )
        {
            sw::AudioPanDirection direction;
            direction._right = sw::MathUtil::sin( degrees * sw::MathUtil::kDegreeToRadian );
            direction._front = sw::MathUtil::cos( degrees * sw::MathUtil::kDegreeToRadian );
            direction._mode  = sw::AudioPanMode::Surround;
            return direction;
        }

        /** @brief 게인 제곱의 합입니다. */
        static float32 computePower( const float32 ( &arrGain )[sw::audio::kMaxChannelCount] )
        {
            float32 power = 0.0f;
            for ( const float32 gain : arrGain )
            {
                power += gain * gain;
            }
            return power;
        }
    };
} // namespace

/**
 * @brief [AudioSurroundTest] 패너는 방위를 한 바퀴 돌려도 에너지(게인² 합)가 1 이고, LFE 는 0 이며, 1° 걸음마다 게인이 튀지 않는다
 */
SW_TEST_CASE( AudioSurroundTest, PannerKeepsEnergyAndIsContinuous )
{
    using Internal                           = AudioSurroundTestInternal;
    const sw::AudioChannelLayout arrLayout[] = { sw::AudioChannelLayout::Surround51, sw::AudioChannelLayout::Surround71 };
    for ( const sw::AudioChannelLayout layout : arrLayout )
    {
        float32 arrPrevious[sw::audio::kMaxChannelCount]{};
        sw::AudioPanner::computeGains( layout, Internal::makeAzimuth( -180.0f ), arrPrevious );
        float32 maxStep = 0.0f;
        for ( int32 degrees = -179; degrees <= 180; ++degrees )
        {
            float32 arrGain[sw::audio::kMaxChannelCount]{};
            sw::AudioPanner::computeGains( layout, Internal::makeAzimuth( static_cast<float32>( degrees ) ), arrGain );
            SW_EXPECT_NEAR( 1.0f, Internal::computePower( arrGain ), 1e-4f );
            SW_EXPECT_NEAR( 0.0f, arrGain[sw::audio::kLfeChannelIndex], 1e-7f );
            for ( uint32 channelIndex = 0; channelIndex < sw::audio::kMaxChannelCount; ++channelIndex )
            {
                maxStep                   = sw::MathUtil::max( maxStep, sw::MathUtil::abs( arrGain[channelIndex] - arrPrevious[channelIndex] ) );
                arrPrevious[channelIndex] = arrGain[channelIndex];
            }
        }
        // 가장 좁은 쌍(FC–FL 30°)에서 1° 는 사분원의 1/30 이다 — sin(π/60) ≈ 0.052 보다 크게 뛰면 경계에서 끊긴 것이다.
        SW_EXPECT_TRUE( maxStep < 0.06f );
    }
}

/**
 * @brief [AudioSurroundTest] 레이아웃별 행렬 — 스피커 방위에 놓인 소리는 그 스피커 하나에서만 나고, 퍼진 소리(길이 0)는 LFE 를 뺀 모든 채널에 같다
 */
SW_TEST_CASE( AudioSurroundTest, PannerPlacesSpeakersAndSpreads )
{
    using Internal = AudioSurroundTestInternal;
    struct SpeakerCase
    {
        sw::AudioChannelLayout _layout;
        float32                _degrees;
        uint32                 _channelIndex;
    };
    const SpeakerCase arrCase[] = {
        {sw::AudioChannelLayout::Surround51,    0.0f, 2},
        {sw::AudioChannelLayout::Surround51,  -30.0f, 0},
        {sw::AudioChannelLayout::Surround51,   30.0f, 1},
        {sw::AudioChannelLayout::Surround51, -110.0f, 4},
        {sw::AudioChannelLayout::Surround51,  110.0f, 5},
        {sw::AudioChannelLayout::Surround71,  -90.0f, 6},
        {sw::AudioChannelLayout::Surround71,   90.0f, 7},
        {sw::AudioChannelLayout::Surround71,  150.0f, 5},
    };
    for ( const SpeakerCase& speakerCase : arrCase )
    {
        float32 arrGain[sw::audio::kMaxChannelCount]{};
        sw::AudioPanner::computeGains( speakerCase._layout, Internal::makeAzimuth( speakerCase._degrees ), arrGain );
        SW_EXPECT_NEAR( 1.0f, arrGain[speakerCase._channelIndex], 1e-4f );
    }

    sw::AudioPanDirection spread;
    spread._mode = sw::AudioPanMode::Surround;
    float32 arrSpread[sw::audio::kMaxChannelCount]{};
    sw::AudioPanner::computeGains( sw::AudioChannelLayout::Surround51, spread, arrSpread );
    for ( uint32 channelIndex = 0; channelIndex < 6; ++channelIndex )
    {
        const float32 expected = channelIndex == sw::audio::kLfeChannelIndex ? 0.0f : sw::MathUtil::sqrt( 1.0f / 5.0f );
        SW_EXPECT_NEAR( expected, arrSpread[channelIndex], 1e-5f );
    }

    // 2D(FrontStereo)는 5.1 에서도 FL · FR 만 쓰고 스테레오와 같은 등전력 값이다.
    float32 arrFront[sw::audio::kMaxChannelCount]{};
    float32 arrStereo[sw::audio::kMaxChannelCount]{};
    sw::AudioPanner::computeGains( sw::AudioChannelLayout::Surround51, sw::AudioPanDirection::makeStereoPan( 0.3f ), arrFront );
    sw::AudioPanner::computeGains( sw::AudioChannelLayout::Stereo, sw::AudioPanDirection::makeStereoPan( 0.3f ), arrStereo );
    SW_EXPECT_NEAR( arrStereo[0], arrFront[0], 1e-7f );
    SW_EXPECT_NEAR( arrStereo[1], arrFront[1], 1e-7f );
    SW_EXPECT_NEAR( 0.0f, arrFront[2] + arrFront[4] + arrFront[5], 1e-7f );
}

/**
 * @brief [AudioSurroundTest] 스테레오 회귀 — 3D 방향의 오른쪽 성분이 예전 팬 값이라 스테레오 게인은 등전력 cos · sin 그대로다(앞뒤 구분 없음)
 */
SW_TEST_CASE( AudioSurroundTest, StereoLayoutKeepsTheOldPanLaw )
{
    using Internal             = AudioSurroundTestInternal;
    const float32 arrDegrees[] = { -90.0f, -45.0f, 0.0f, 30.0f, 135.0f, 180.0f };
    for ( const float32 degrees : arrDegrees )
    {
        const sw::AudioPanDirection direction = Internal::makeAzimuth( degrees );
        float32                     arrGain[sw::audio::kMaxChannelCount]{};
        sw::AudioPanner::computeGains( sw::AudioChannelLayout::Stereo, direction, arrGain );
        const float32 theta = ( direction._right + 1.0f ) * sw::MathUtil::kPi * 0.25f;
        SW_EXPECT_NEAR( sw::MathUtil::cos( theta ), arrGain[0], 1e-7f );
        SW_EXPECT_NEAR( sw::MathUtil::sin( theta ), arrGain[1], 1e-7f );
    }
}

/**
 * @brief [AudioSurroundTest] 다운믹스 행렬 — ITU 계수(센터 · 서라운드 0.707, LFE 버림)이고, 마스크가 모자라거나 높이 채널이면 오류로 멈춘다
 */
SW_TEST_CASE( AudioSurroundTest, DownmixMatrixFollowsTheChannelMask )
{
    SW_EXPECT_LOG( "Audio.DownmixMaskShort" );          // 마스크 비트 셋에 채널 여섯
    SW_EXPECT_LOG( "Audio.DownmixSpeakerUnplaceable" ); // 높이 채널
    sw::AudioDownmix downmix;
    SW_REQUIRE_TRUE( sw::AudioDownmix::makeForWaveMask( 6, sw::AudioChannelLayoutUtil::getChannelMask( sw::AudioChannelLayout::Surround51 ), downmix ) );
    const float32 arrFrame[6] = { 0.1f, 0.2f, 0.4f, 0.8f, 0.3f, 0.5f }; // FL FR FC LFE BL BR
    float32       left{ 0.0f };
    float32       right{ 0.0f };
    downmix.apply( arrFrame, left, right );
    SW_EXPECT_NEAR( 0.1f + sw::MathUtil::kInvSqrt2 * ( 0.4f + 0.3f ), left, 1e-6f );
    SW_EXPECT_NEAR( 0.2f + sw::MathUtil::kInvSqrt2 * ( 0.4f + 0.5f ), right, 1e-6f );

    // Vorbis 5.1 순서(FL FC FR BL BR LFE)도 같은 배치다.
    uint32 arrSpeakerBit[sw::AudioDownmix::kMaxSourceChannelCount]{};
    SW_REQUIRE_TRUE( sw::AudioDownmix::getVorbisSpeakers( 6, arrSpeakerBit ) );
    SW_REQUIRE_TRUE( sw::AudioDownmix::makeForSpeakers( arrSpeakerBit, 6, downmix ) );
    const float32 arrVorbisFrame[6] = { 0.1f, 0.4f, 0.2f, 0.3f, 0.5f, 0.8f };
    downmix.apply( arrVorbisFrame, left, right );
    SW_EXPECT_NEAR( 0.1f + sw::MathUtil::kInvSqrt2 * ( 0.4f + 0.3f ), left, 1e-6f );

    SW_EXPECT_FALSE( sw::AudioDownmix::makeForWaveMask( 6, 0x7, downmix ) );                  // 마스크 비트 셋에 채널 여섯
    SW_EXPECT_FALSE( sw::AudioDownmix::makeForWaveMask( 4, 0x3 | 0x800 | 0x1000, downmix ) ); // 높이 채널(TOP_*)
}

/**
 * @brief [AudioSurroundTest] 5.1 WAV 를 읽으면 앞 두 채널만 남기지 않고 센터 대사(FC)가 양쪽에 0.707 로 들어간다
 */
SW_TEST_CASE( AudioSurroundTest, MultichannelClipKeepsTheCenterChannel )
{
    constexpr uint32    kFrameCount = 64;
    sw::vector<float32> listSample( static_cast<size_t>( kFrameCount ) * 6, 0.0f );
    for ( uint32 frameIndex = 0; frameIndex < kFrameCount; ++frameIndex )
    {
        listSample[static_cast<size_t>( frameIndex ) * 6 + 2] = 0.5f; // FC 에만 대사
    }
    sw::vector<uint8> bytes;
    sw::AudioWAVEncoder::encodeFloat32( listSample.data(), kFrameCount, 6, sw::AudioChannelLayoutUtil::getChannelMask( sw::AudioChannelLayout::Surround51 ),
                                        sw::audio::kSampleRate, bytes );
    sw::AudioPcm pcm;
    SW_REQUIRE_TRUE( sw::AudioClipDecoder::decodeWAV( bytes.data(), bytes.size(), pcm ) );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( pcm._channelCount ) );
    sw::AudioClipData clip;
    SW_REQUIRE_TRUE( sw::AudioClipData::convertPcm( pcm, clip ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( clip._channelCount ) );
    SW_EXPECT_NEAR( 0.5f * sw::MathUtil::kInvSqrt2, clip._listSample[0], 1e-6f );
    SW_EXPECT_NEAR( 0.5f * sw::MathUtil::kInvSqrt2, clip._listSample[1], 1e-6f );
}

/**
 * @brief [AudioSurroundTest] 레이아웃 협상 — Auto 는 장치 마스크를 따르고(없으면 스테레오), 명시값은 장치 채널이 모자라면 거절하며 장치가 없으면 그대로다
 */
SW_TEST_CASE( AudioSurroundTest, SpeakerModeResolvesAgainstTheDevice )
{
    using Util = sw::AudioChannelLayoutUtil;
    sw::AudioOutputDevice none;
    sw::AudioOutputDevice stereo;
    stereo._channelCount = 2;
    stereo._channelMask  = Util::getChannelMask( sw::AudioChannelLayout::Stereo );
    sw::AudioOutputDevice side51;
    side51._channelCount = 6;
    side51._channelMask  = Util::kSpeakerFrontLeft | Util::kSpeakerFrontRight | Util::kSpeakerFrontCenter | Util::kSpeakerLowFrequency | Util::kSpeakerSideLeft | Util::kSpeakerSideRight;
    sw::AudioOutputDevice full71;
    full71._channelCount = 8;
    full71._channelMask  = Util::getChannelMask( sw::AudioChannelLayout::Surround71 );

    sw::AudioChannelLayout layout{ sw::AudioChannelLayout::Stereo };
    SW_EXPECT_TRUE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Auto, none, layout ) && layout == sw::AudioChannelLayout::Stereo );
    SW_EXPECT_TRUE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Auto, side51, layout ) && layout == sw::AudioChannelLayout::Surround51 );
    SW_EXPECT_TRUE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Auto, full71, layout ) && layout == sw::AudioChannelLayout::Surround71 );
    SW_EXPECT_TRUE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Surround51, full71, layout ) && layout == sw::AudioChannelLayout::Surround51 );
    SW_EXPECT_TRUE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Surround71, none, layout ) && layout == sw::AudioChannelLayout::Surround71 );
    SW_EXPECT_FALSE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Surround51, stereo, layout ) );
    SW_EXPECT_TRUE( sw::IAudioService::resolveLayout( sw::AudioSpeakerMode::Headphones, stereo, layout ) && layout == sw::AudioChannelLayout::Binaural );

    sw::AudioSpeakerMode mode{ sw::AudioSpeakerMode::Auto };
    SW_EXPECT_TRUE( sw::UserSettingsEngineHandlers::tryParseSpeakerMode( "surround71", mode ) && mode == sw::AudioSpeakerMode::Surround71 );
    SW_EXPECT_FALSE( sw::UserSettingsEngineHandlers::tryParseSpeakerMode( "quad", mode ) );
}

/**
 * @brief [AudioSurroundTest] 바이노럴 — 오른쪽(90°) 소리는 오른쪽 귀에 먼저(지연 0), 왼쪽 귀에 (a/c)(1 + π/2) ≈ 31 프레임 늦게 오고 고음이 깎인다. 앞 소리는 두 귀가 같다
 */
SW_TEST_CASE( AudioSurroundTest, BinauralDelaysAndShadowsTheFarEar )
{
    constexpr uint32 kFrameCount = 128;
    float32          arrImpulse[kFrameCount]{};
    arrImpulse[0] = 1.0f;
    float32 arrLeft[kFrameCount]{};
    float32 arrRight[kFrameCount]{};

    sw::AudioBinauralFilter filter;
    filter.setDirection( AudioSurroundTestInternal::makeAzimuth( 90.0f ) );
    filter.process( arrImpulse, kFrameCount, arrLeft, arrRight );
    uint32 leftPeak  = 0;
    uint32 rightPeak = 0;
    for ( uint32 frameIndex = 1; frameIndex < kFrameCount; ++frameIndex )
    {
        leftPeak  = sw::MathUtil::abs( arrLeft[frameIndex] ) > sw::MathUtil::abs( arrLeft[leftPeak] ) ? frameIndex : leftPeak;
        rightPeak = sw::MathUtil::abs( arrRight[frameIndex] ) > sw::MathUtil::abs( arrRight[rightPeak] ) ? frameIndex : rightPeak;
    }
    SW_EXPECT_EQUAL( 0u, rightPeak );
    SW_EXPECT_TRUE( 30u <= leftPeak && leftPeak <= 33u );
    SW_EXPECT_TRUE( sw::MathUtil::abs( arrLeft[leftPeak] ) < sw::MathUtil::abs( arrRight[rightPeak] ) ); // 그림자 쪽 귀는 순간 응답이 작다
    SW_EXPECT_NEAR( sw::AudioBinauralFilter::computeDelayFrames( sw::MathUtil::kPi ), 0.0875f / 343.0f * ( 1.0f + sw::MathUtil::kHalfPi ) * 48000.0f, 1e-3f );

    sw::AudioBinauralFilter front;
    front.setDirection( AudioSurroundTestInternal::makeAzimuth( 0.0f ) );
    front.process( arrImpulse, kFrameCount, arrLeft, arrRight );
    for ( uint32 frameIndex = 0; frameIndex < kFrameCount; ++frameIndex )
    {
        SW_EXPECT_NEAR( arrLeft[frameIndex], arrRight[frameIndex], 1e-6f );
    }
}
