#include "pch.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioDownmix.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioPanner.h"
#include "Engine/Audio/DSP/AudioEffect.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioSurroundTest — 5.1 · 7.1 패너(에너지 보존 · 방위 연속 · 스테레오 회귀), 다채널 이펙트, 다채널 클립의 명시 다운믹스
// ------------------------------------------------------------------------------

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

        /** @brief 교차 버퍼(@p channelCount 채널)의 한 채널 평균입니다. */
        static float32 computeChannelMean( const sw::vector<float32>& listSample, uint32 channelCount, uint32 channel )
        {
            const size_t frameCount = listSample.size() / channelCount;
            float64      sum        = 0.0;
            for ( size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                sum += static_cast<float64>( listSample[frameIndex * channelCount + channel] );
            }
            return static_cast<float32>( sum / static_cast<float64>( frameCount ) );
        }

        /** @brief 리버브 · 리미터가 없는 sfx · master 그래프와 감쇠 프리셋 하나(최소 거리 10 m — 가까운 소리가 줄지 않는다)입니다. */
        static sw::AudioMixerDesc makeMixer()
        {
            sw::AudioMixerDesc desc;
            (void)desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master" /><AudioBusDesc _name="sfx" _parent="master" /></_listBus></AudioMixerDesc>)" ); // 아래 initialize 가 실패로 알린다
            sw::AudioAttenuationDesc attenuation;
            attenuation._name        = sw::hashed_string( "Near" );
            attenuation._minDistance = 10.0f;
            attenuation._maxDistance = 30.0f;
            desc._listAttenuation.push_back( attenuation );
            return desc;
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
            SW_EXPECT_NEAR_EQUAL( 1.0f, Internal::computePower( arrGain ), 1e-4f );
            SW_EXPECT_NEAR_EQUAL( 0.0f, arrGain[sw::audio::kLfeChannelIndex], 1e-7f );
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
        SW_EXPECT_NEAR_EQUAL( 1.0f, arrGain[speakerCase._channelIndex], 1e-4f );
    }

    sw::AudioPanDirection spread;
    spread._mode = sw::AudioPanMode::Surround;
    float32 arrSpread[sw::audio::kMaxChannelCount]{};
    sw::AudioPanner::computeGains( sw::AudioChannelLayout::Surround51, spread, arrSpread );
    for ( uint32 channelIndex = 0; channelIndex < 6; ++channelIndex )
    {
        const float32 expected = channelIndex == sw::audio::kLfeChannelIndex ? 0.0f : sw::MathUtil::sqrt( 1.0f / 5.0f );
        SW_EXPECT_NEAR_EQUAL( expected, arrSpread[channelIndex], 1e-5f );
    }

    // 2D(FrontStereo)는 5.1 에서도 FL · FR 만 쓰고 스테레오와 같은 등전력 값이다.
    float32 arrFront[sw::audio::kMaxChannelCount]{};
    float32 arrStereo[sw::audio::kMaxChannelCount]{};
    sw::AudioPanner::computeGains( sw::AudioChannelLayout::Surround51, sw::AudioPanDirection::makeStereoPan( 0.3f ), arrFront );
    sw::AudioPanner::computeGains( sw::AudioChannelLayout::Stereo, sw::AudioPanDirection::makeStereoPan( 0.3f ), arrStereo );
    SW_EXPECT_NEAR_EQUAL( arrStereo[0], arrFront[0], 1e-7f );
    SW_EXPECT_NEAR_EQUAL( arrStereo[1], arrFront[1], 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrFront[2] + arrFront[4] + arrFront[5], 1e-7f );
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
        SW_EXPECT_NEAR_EQUAL( sw::MathUtil::cos( theta ), arrGain[0], 1e-7f );
        SW_EXPECT_NEAR_EQUAL( sw::MathUtil::sin( theta ), arrGain[1], 1e-7f );
    }
}

/**
 * @brief [AudioSurroundTest] 5.1 엔진 렌더 — 뒤 오른쪽(110°)의 모노 직류는 BR 채널에서만 나고, 2D 소리는 FL · FR 에서만 난다
 */
SW_TEST_CASE( AudioSurroundTest, EngineRendersDirectionsIntoSurroundChannels )
{
    using Internal = AudioSurroundTestInternal;
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( engine.initialize( Internal::makeMixer(), sw::AudioChannelLayout::Surround51 ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/mono" ), test::AudioTestUtil::makeConstantClip( 0.5f, 48000, 1 ) );
    sw::AudioListenerState listener;
    listener._bActive = true;
    engine.setListener( 0, listener );
    const float32 radians = 110.0f * sw::MathUtil::kDegreeToRadian;
    engine.setEmitter( 1, sw::float3( 2.0f * sw::MathUtil::sin( radians ), 0.0f, 2.0f * sw::MathUtil::cos( radians ) ), sw::float3( 0.0f, 0.0f, 0.0f ) );
    sw::AudioClipPlayParams params;
    params._emitterID   = 1;
    params._attenuation = sw::hashed_string( "Near" );
    params._bLoop       = true;
    SW_ASSERT_TRUE( engine.playClip( sw::hashed_string( "test/mono" ), sw::hashed_string( "sfx" ), params ) != 0 );
    (void)test::AudioTestUtil::render( engine, 1024 );
    const sw::vector<float32> listSample = test::AudioTestUtil::render( engine, 512 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, Internal::computeChannelMean( listSample, 6, 5 ), 1e-3f );
    for ( uint32 channelIndex = 0; channelIndex < 5; ++channelIndex )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::computeChannelMean( listSample, 6, channelIndex ), 1e-3f );
    }

    sw::AudioEngine flat;
    SW_ASSERT_TRUE( flat.initialize( Internal::makeMixer(), sw::AudioChannelLayout::Surround51 ) );
    flat.getClipStore().addClip( sw::hashed_string( "test/mono" ), test::AudioTestUtil::makeConstantClip( 0.5f, 48000, 1 ) );
    sw::AudioClipPlayParams flatParams;
    flatParams._bLoop = true;
    SW_ASSERT_TRUE( flat.playClip( sw::hashed_string( "test/mono" ), sw::hashed_string( "sfx" ), flatParams ) != 0 );
    (void)test::AudioTestUtil::render( flat, 1024 );
    const sw::vector<float32> listFlat = test::AudioTestUtil::render( flat, 512 );
    SW_EXPECT_NEAR_EQUAL( 0.5f * sw::MathUtil::kInvSqrt2, Internal::computeChannelMean( listFlat, 6, 0 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.5f * sw::MathUtil::kInvSqrt2, Internal::computeChannelMean( listFlat, 6, 1 ), 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::computeChannelMean( listFlat, 6, 2 ), 1e-6f );
}

/**
 * @brief [AudioSurroundTest] 다채널 이펙트 — 필터 · 딜레이는 채널마다 독립이고(한 채널의 입력이 다른 채널로 새지 않는다), 리버브는 LFE 에 젖은 소리를 내지 않는다
 */
SW_TEST_CASE( AudioSurroundTest, EffectsProcessEveryChannelIndependently )
{
    const utf8* arrIndependent[] = { "LowPass", "Delay" };
    for ( const utf8* pType : arrIndependent )
    {
        sw::unique_ptr<sw::IAudioEffect> pEffect = sw::AudioEffectRegistry::createEffect( sw::hashed_string( pType ), sw::AudioChannelLayout::Surround71 );
        SW_ASSERT_NOT_NULL( pEffect.get() );
        sw::AudioBlockBuffer buffer;
        buffer.allocate( sw::AudioChannelLayout::Surround71 );
        const sw::AudioBlockView block = buffer.makeView( sw::audio::kBlockFrameCount );
        block.getChannel( 6 )[0]       = 1.0f; // SL 에 임펄스 하나
        for ( uint32 blockIndex = 0; blockIndex < 400; ++blockIndex )
        {
            pEffect->process( block );
            for ( uint32 channelIndex = 0; channelIndex < 8; ++channelIndex )
            {
                if ( channelIndex == 6 )
                    continue;
                for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
                {
                    SW_ASSERT_TRUE_MSG( block.getChannel( channelIndex )[frameIndex] == 0.0f, pType );
                }
            }
            buffer.clear( sw::audio::kBlockFrameCount );
        }
    }

    sw::unique_ptr<sw::IAudioEffect> pReverb = sw::AudioEffectRegistry::createEffect( sw::hashed_string( "Reverb" ), sw::AudioChannelLayout::Surround51 );
    SW_ASSERT_NOT_NULL( pReverb.get() );
    sw::AudioBlockBuffer buffer;
    buffer.allocate( sw::AudioChannelLayout::Surround51 );
    const sw::AudioBlockView block = buffer.makeView( sw::audio::kBlockFrameCount );
    block.getChannel( 0 )[0]       = 1.0f;
    float32 wetEnergy              = 0.0f;
    for ( uint32 blockIndex = 0; blockIndex < 40; ++blockIndex )
    {
        pReverb->process( block );
        for ( uint32 frameIndex = 0; frameIndex < block._frameCount; ++frameIndex )
        {
            SW_ASSERT_TRUE( block.getChannel( sw::audio::kLfeChannelIndex )[frameIndex] == 0.0f );
            wetEnergy += block.getChannel( 5 )[frameIndex] * block.getChannel( 5 )[frameIndex];
        }
        buffer.clear( sw::audio::kBlockFrameCount );
    }
    SW_EXPECT_TRUE( wetEnergy > 0.0f ); // FL 의 소리가 BR 까지 잔향으로 퍼진다
}

/**
 * @brief [AudioSurroundTest] 다운믹스 행렬 — ITU 계수(센터 · 서라운드 0.707, LFE 버림)이고, 마스크가 모자라거나 높이 채널이면 오류로 멈춘다
 */
SW_TEST_CASE( AudioSurroundTest, DownmixMatrixFollowsTheChannelMask )
{
    sw::AudioDownmix downmix;
    SW_ASSERT_TRUE( sw::AudioDownmix::makeForWaveMask( 6, sw::AudioChannelLayoutUtil::getChannelMask( sw::AudioChannelLayout::Surround51 ), downmix ) );
    const float32 arrFrame[6] = { 0.1f, 0.2f, 0.4f, 0.8f, 0.3f, 0.5f }; // FL FR FC LFE BL BR
    float32       left{ 0.0f };
    float32       right{ 0.0f };
    downmix.apply( arrFrame, left, right );
    SW_EXPECT_NEAR_EQUAL( 0.1f + sw::MathUtil::kInvSqrt2 * ( 0.4f + 0.3f ), left, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.2f + sw::MathUtil::kInvSqrt2 * ( 0.4f + 0.5f ), right, 1e-6f );

    // Vorbis 5.1 순서(FL FC FR BL BR LFE)도 같은 배치다.
    uint32 arrSpeakerBit[sw::AudioDownmix::kMaxSourceChannelCount]{};
    SW_ASSERT_TRUE( sw::AudioDownmix::getVorbisSpeakers( 6, arrSpeakerBit ) );
    SW_ASSERT_TRUE( sw::AudioDownmix::makeForSpeakers( arrSpeakerBit, 6, downmix ) );
    const float32 arrVorbisFrame[6] = { 0.1f, 0.4f, 0.2f, 0.3f, 0.5f, 0.8f };
    downmix.apply( arrVorbisFrame, left, right );
    SW_EXPECT_NEAR_EQUAL( 0.1f + sw::MathUtil::kInvSqrt2 * ( 0.4f + 0.3f ), left, 1e-6f );

    SW_TEST_DEFENSIVE_SCOPE( "unplaceable channels stop with an error" );
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
    sw::AudioWavEncoder::encodeFloat32( listSample.data(), kFrameCount, 6, sw::AudioChannelLayoutUtil::getChannelMask( sw::AudioChannelLayout::Surround51 ),
                                        sw::audio::kSampleRate, bytes );
    sw::AudioPcm pcm;
    SW_ASSERT_TRUE( sw::AudioClipDecoder::decodeWav( bytes.data(), bytes.size(), pcm ) );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( pcm._channelCount ) );
    sw::AudioClipData clip;
    SW_ASSERT_TRUE( sw::AudioClipData::convertPcm( pcm, clip ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( clip._channelCount ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f * sw::MathUtil::kInvSqrt2, clip._listSample[0], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f * sw::MathUtil::kInvSqrt2, clip._listSample[1], 1e-6f );
}
