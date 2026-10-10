#include "pch.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/DSP/AudioBiquad.h"
#include "Engine/Audio/DSP/AudioEffect.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioDSPTest — 버스 이펙트(바이쿼드 · 컴프레서 · 리미터 · 리버브 · 딜레이)를 버퍼에서 숫자로 잰다. 장치 없음.
// ------------------------------------------------------------------------------

namespace
{
    struct AudioDSPTestInternal
    {
        /** @brief 이름의 이펙트를 만들고 파라미터를 이름으로 겁니다. */
        static sw::unique_ptr<sw::IAudioEffect> createEffect( const utf8* pType, std::initializer_list<std::pair<const utf8*, float32>> listParameter )
        {
            sw::unique_ptr<sw::IAudioEffect> pEffect = sw::AudioEffectRegistry::createEffect( sw::hashed_string( pType ) );
            if ( pEffect == nullptr )
                return nullptr;
            for ( const std::pair<const utf8*, float32>& parameter : listParameter )
            {
                const int32 parameterIndex = pEffect->findParameterIndex( sw::hashed_string( parameter.first ) );
                if ( parameterIndex < 0 )
                    return nullptr;
                pEffect->setParameter( static_cast<uint32>( parameterIndex ), parameter.second );
            }
            return pEffect;
        }

        /** @brief 두 채널 같은 사인 버퍼(스테레오 교차)입니다. */
        static sw::vector<float32> makeStereoSine( float32 frequencyHz, float32 amplitude, uint32 frameCount )
        {
            sw::vector<float32> listSample( static_cast<size_t>( frameCount ) * 2 );
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                const float64 phase            = 2.0 * 3.14159265358979323846 * static_cast<float64>( frequencyHz ) * static_cast<float64>( frameIndex ) / 48000.0;
                const float32 value            = amplitude * static_cast<float32>( std::sin( phase ) );
                listSample[frameIndex * 2]     = value;
                listSample[frameIndex * 2 + 1] = value;
            }
            return listSample;
        }

        /** @brief 사인을 이펙트에 통과시켜 정상 상태(앞 0.1 초를 버린 4800 프레임)의 그 주파수 이득(dB)입니다. */
        static float32 measureGainDb( sw::IAudioEffect& effect, float32 frequencyHz )
        {
            effect.reset();
            constexpr uint32    kFrameCount = 9600;
            sw::vector<float32> listSample  = makeStereoSine( frequencyHz, 0.25f, kFrameCount );
            for ( uint32 offset = 0; offset < kFrameCount; offset += sw::audio::kBlockFrameCount )
            {
                effect.process( listSample.data() + static_cast<size_t>( offset ) * 2, sw::MathUtil::min( sw::audio::kBlockFrameCount, kFrameCount - offset ) );
            }
            const float32 amplitude = test::AudioTestUtil::computeToneAmplitude( listSample, 0, 4800, 4800, frequencyHz );
            return sw::AudioMath::linearToDb( amplitude / 0.25f );
        }

        /** @brief 버퍼 전체를 블록으로 나눠 처리합니다. */
        static void processAll( sw::IAudioEffect& effect, sw::vector<float32>& inoutListSample )
        {
            const uint32 frameCount = static_cast<uint32>( inoutListSample.size() / 2 );
            for ( uint32 offset = 0; offset < frameCount; offset += sw::audio::kBlockFrameCount )
            {
                effect.process( inoutListSample.data() + static_cast<size_t>( offset ) * 2, sw::MathUtil::min( sw::audio::kBlockFrameCount, frameCount - offset ) );
            }
        }

        /** @brief 임펄스(첫 샘플 1) 버퍼입니다. */
        static sw::vector<float32> makeImpulse( uint32 frameCount )
        {
            sw::vector<float32> listSample( static_cast<size_t>( frameCount ) * 2, 0.0f );
            listSample[0] = 1.0f;
            listSample[1] = 1.0f;
            return listSample;
        }
    };
} // namespace

/**
 * @brief [AudioDSPTest] 바이쿼드의 크기 응답 — 세 점(100 Hz · 1 kHz · 10 kHz)에서 처리 결과가 해석식 · 교과서 값과 맞는다
 * @details 2 차 버터워스 로우패스(Q = 1/√2)는 컷오프에서 -3.01 dB, 통과대역 0 dB, 10 배 위에서 약 -40 dB(쌍선형 변환으로 조금 더 깊다).
 *          하이패스는 그 거울, 피킹 +6 dB 는 중심에서 +6 dB 이고 멀리서는 0 dB 다.
 */
SW_TEST_CASE( AudioDSPTest, BiquadFrequencyResponseAtPoints )
{
    sw::unique_ptr<sw::IAudioEffect> pLowPass = AudioDSPTestInternal::createEffect( "LowPass", {
                                                                                                   {"frequencyHz",     1000.0f},
                                                                                                   {          "q", 0.70710678f}
    } );
    SW_ASSERT_TRUE( pLowPass != nullptr );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioDSPTestInternal::measureGainDb( *pLowPass, 100.0f ), 0.1f );
    SW_EXPECT_NEAR_EQUAL( -3.01f, AudioDSPTestInternal::measureGainDb( *pLowPass, 1000.0f ), 0.1f );
    const float32 lowPassAt10k = AudioDSPTestInternal::measureGainDb( *pLowPass, 10000.0f );
    SW_EXPECT_TRUE( -46.0f < lowPassAt10k && lowPassAt10k < -39.0f );
    const sw::AudioBiquadCoefficients lowPassCoefficients = sw::AudioBiquadCoefficients::make( sw::AudioBiquadType::LowPass, 1000.0f, 0.70710678f, 0.0f, 48000.0f );
    SW_EXPECT_NEAR_EQUAL( sw::AudioMath::linearToDb( lowPassCoefficients.computeMagnitude( 10000.0f, 48000.0f ) ), lowPassAt10k, 0.2f );

    sw::unique_ptr<sw::IAudioEffect> pHighPass = AudioDSPTestInternal::createEffect( "HighPass", {
                                                                                                     {"frequencyHz",     1000.0f},
                                                                                                     {          "q", 0.70710678f}
    } );
    SW_ASSERT_TRUE( pHighPass != nullptr );
    const float32 highPassAt100 = AudioDSPTestInternal::measureGainDb( *pHighPass, 100.0f );
    SW_EXPECT_TRUE( -42.0f < highPassAt100 && highPassAt100 < -38.0f );
    SW_EXPECT_NEAR_EQUAL( -3.01f, AudioDSPTestInternal::measureGainDb( *pHighPass, 1000.0f ), 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioDSPTestInternal::measureGainDb( *pHighPass, 10000.0f ), 0.1f );

    sw::unique_ptr<sw::IAudioEffect> pPeaking = AudioDSPTestInternal::createEffect( "Peaking", {
                                                                                                   {"frequencyHz", 1000.0f},
                                                                                                   {          "q",    1.0f},
                                                                                                   {     "gainDb",    6.0f}
    } );
    SW_ASSERT_TRUE( pPeaking != nullptr );
    SW_EXPECT_NEAR_EQUAL( 6.0f, AudioDSPTestInternal::measureGainDb( *pPeaking, 1000.0f ), 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioDSPTestInternal::measureGainDb( *pPeaking, 100.0f ), 0.3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioDSPTestInternal::measureGainDb( *pPeaking, 10000.0f ), 0.3f );

    // 로우패스를 활짝 열면(20 kHz) 신호를 건드리지 않는다 — 스냅샷이 평소엔 이 상태로 둔다.
    sw::unique_ptr<sw::IAudioEffect> pOpen = AudioDSPTestInternal::createEffect( "LowPass", {
                                                                                                { "frequencyHz", 20000.0f }
    } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, AudioDSPTestInternal::measureGainDb( *pOpen, 10000.0f ), 1e-3f );
}

/**
 * @brief [AudioDSPTest] 컴프레서 — 문턱 위는 (입력 − 문턱)·(1 − 1/비율) 만큼 줄이고, 문턱 아래는 건드리지 않는다
 * @details -6.02 dBFS 사인 · 문턱 -24 dB · 비율 4 · 니 0 이면 출력 피크는 -24 + 18.02/4 = -19.5 dB, 감소량 13.5 dB 다.
 */
SW_TEST_CASE( AudioDSPTest, CompressorGainReduction )
{
    sw::unique_ptr<sw::IAudioEffect> pCompressor = AudioDSPTestInternal::createEffect(
        "Compressor", {
                          {"thresholdDb", -24.0f},
                          {      "ratio",   4.0f},
                          {     "kneeDb",   0.0f},
                          {   "attackMs",   1.0f},
                          {  "releaseMs", 200.0f},
                          {   "makeupDb",   0.0f}
    } );
    SW_ASSERT_TRUE( pCompressor != nullptr );

    sw::vector<float32> listLoud = AudioDSPTestInternal::makeStereoSine( 1000.0f, 0.5f, 48000 );
    AudioDSPTestInternal::processAll( *pCompressor, listLoud );
    const float32 loudPeakDb = sw::AudioMath::linearToDb( test::AudioTestUtil::computePeak( listLoud, 0, 43200, 4800 ) );
    SW_EXPECT_NEAR_EQUAL( -19.5f, loudPeakDb, 0.75f );

    pCompressor->reset();
    sw::vector<float32> listQuiet = AudioDSPTestInternal::makeStereoSine( 1000.0f, 0.01f, 48000 );
    AudioDSPTestInternal::processAll( *pCompressor, listQuiet );
    SW_EXPECT_NEAR_EQUAL( -40.0f, sw::AudioMath::linearToDb( test::AudioTestUtil::computePeak( listQuiet, 0, 43200, 4800 ) ), 0.1f );

    // 메이크업 게인은 그대로 더해진다.
    const int32 makeupIndex = pCompressor->findParameterIndex( sw::hashed_string( "makeupDb" ) );
    SW_ASSERT_TRUE( makeupIndex >= 0 );
    pCompressor->setParameter( static_cast<uint32>( makeupIndex ), 6.0f );
    pCompressor->reset();
    listQuiet = AudioDSPTestInternal::makeStereoSine( 1000.0f, 0.01f, 48000 );
    AudioDSPTestInternal::processAll( *pCompressor, listQuiet );
    SW_EXPECT_NEAR_EQUAL( -34.0f, sw::AudioMath::linearToDb( test::AudioTestUtil::computePeak( listQuiet, 0, 43200, 4800 ) ), 0.1f );
}

/**
 * @brief [AudioDSPTest] 리미터 — +6 dBFS 입력에서도 출력 피크가 천장(-1 dBFS)을 한 샘플도 넘지 않고, 작은 신호는 늦춰지기만 한다
 */
SW_TEST_CASE( AudioDSPTest, LimiterHoldsTheCeiling )
{
    sw::unique_ptr<sw::IAudioEffect> pLimiter = AudioDSPTestInternal::createEffect( "Limiter", {
                                                                                                   {  "ceilingDb", -1.0f},
                                                                                                   {"lookaheadMs",  2.0f},
                                                                                                   {  "releaseMs", 50.0f}
    } );
    SW_ASSERT_TRUE( pLimiter != nullptr );
    const float32 ceiling = sw::AudioMath::dbToLinear( -1.0f );

    sw::vector<float32> listLoud = AudioDSPTestInternal::makeStereoSine( 220.0f, 2.0f, 48000 );
    // 갑자기 튀는 샘플(트랜지언트)도 섞는다.
    listLoud[20000 * 2] = 4.0f;
    AudioDSPTestInternal::processAll( *pLimiter, listLoud );
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( listLoud, 0, 0, 48000 ) <= ceiling + 1e-5f );
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( listLoud, 0, 24000, 24000 ) > ceiling * 0.9f );

    pLimiter->reset();
    sw::vector<float32>       listQuiet    = AudioDSPTestInternal::makeStereoSine( 220.0f, 0.1f, 9600 );
    const sw::vector<float32> listOriginal = listQuiet;
    AudioDSPTestInternal::processAll( *pLimiter, listQuiet );
    // 미리 보기 2 ms = 96 프레임 늦춘 같은 소리다.
    for ( uint32 frameIndex = 96; frameIndex < 9600; frameIndex += 37 )
    {
        SW_EXPECT_NEAR_EQUAL( listOriginal[( frameIndex - 96 ) * 2], listQuiet[frameIndex * 2], 1e-5f );
    }
}

/**
 * @brief [AudioDSPTest] 리버브 — 임펄스 뒤에 꼬리가 남고, 방이 클수록 길다. wet 0 · dry 1 이면 입력 그대로다
 */
SW_TEST_CASE( AudioDSPTest, ReverbTailGrowsWithRoomSize )
{
    float32       arrLateRms[2]  = { 0.0f, 0.0f };
    const float32 arrRoomSize[2] = { 0.3f, 0.9f };
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::unique_ptr<sw::IAudioEffect> pReverb = AudioDSPTestInternal::createEffect( "Reverb", {
                                                                                                     {"roomSize", arrRoomSize[caseIndex]},
                                                                                                     {     "wet",                   1.0f},
                                                                                                     {     "dry",                   0.0f}
        } );
        SW_ASSERT_TRUE( pReverb != nullptr );
        sw::vector<float32> listSample = AudioDSPTestInternal::makeImpulse( 72000 );
        AudioDSPTestInternal::processAll( *pReverb, listSample );
        // 첫 반사(가장 짧은 콤 ≈ 24 ms) 전에는 조용하다.
        SW_EXPECT_NEAR_EQUAL( 0.0f, test::AudioTestUtil::computePeak( listSample, 0, 1, 1000 ), 1e-6f );
        arrLateRms[caseIndex] = test::AudioTestUtil::computeRms( listSample, 0, 48000, 24000 );
        SW_EXPECT_TRUE( test::AudioTestUtil::computeRms( listSample, 0, 2400, 9600 ) > 1e-3f );
    }
    // 1 초 뒤의 꼬리: 큰 방이 훨씬 남는다.
    SW_EXPECT_TRUE( arrLateRms[1] > arrLateRms[0] * 10.0f );

    sw::unique_ptr<sw::IAudioEffect> pDryOnly = AudioDSPTestInternal::createEffect( "Reverb", {
                                                                                                  {"wet", 0.0f},
                                                                                                  {"dry", 1.0f}
    } );
    sw::vector<float32>              listSine = AudioDSPTestInternal::makeStereoSine( 440.0f, 0.3f, 4800 );
    const sw::vector<float32>        listCopy = listSine;
    AudioDSPTestInternal::processAll( *pDryOnly, listSine );
    SW_EXPECT_TRUE( listSine == listCopy );
}

/**
 * @brief [AudioDSPTest] 딜레이 — 첫 에코는 지연 시간에 wet 크기로, 다음은 두 배 시간에 wet·feedback 크기로 온다
 */
SW_TEST_CASE( AudioDSPTest, DelayEchoLandsAtTheDelayTime )
{
    sw::unique_ptr<sw::IAudioEffect> pDelay = AudioDSPTestInternal::createEffect( "Delay", {
                                                                                               {  "timeMs", 100.0f},
                                                                                               {"feedback",   0.5f},
                                                                                               {     "wet",   0.5f},
                                                                                               {     "dry",   1.0f}
    } );
    SW_ASSERT_TRUE( pDelay != nullptr );
    sw::vector<float32> listSample = AudioDSPTestInternal::makeImpulse( 14400 );
    AudioDSPTestInternal::processAll( *pDelay, listSample );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listSample[0], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listSample[4800 * 2], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, listSample[9600 * 2], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, test::AudioTestUtil::computePeak( listSample, 0, 1, 4799 ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, test::AudioTestUtil::computePeak( listSample, 0, 4801, 4799 ), 1e-6f );
}

/**
 * @brief [AudioDSPTest] 버스의 이펙트 체인은 데이터다 — sfx 의 로우패스가 sfx 소리만 깎고, master 리미터가 합을 묶는다. 모르는 종류 · 파라미터는 읽기 오류다
 */
SW_TEST_CASE( AudioDSPTest, BusEffectChainsComeFromData )
{
    sw::AudioMixerDesc desc;
    SW_ASSERT_TRUE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus>
        <AudioBusDesc _name="master"><_listEffect><AudioEffectDesc _type="Limiter"><_listParameter>
            <AudioEffectParameterDesc _name="ceilingDb" _value="-6.0206" /></_listParameter></AudioEffectDesc></_listEffect></AudioBusDesc>
        <AudioBusDesc _name="sfx" _parent="master"><_listEffect><AudioEffectDesc _type="LowPass"><_listParameter>
            <AudioEffectParameterDesc _name="frequencyHz" _value="200" /></_listParameter></AudioEffectDesc></_listEffect></AudioBusDesc>
        <AudioBusDesc _name="music" _parent="master" /></_listBus></AudioMixerDesc>)" ) );
    sw::AudioEngine engine;
    SW_ASSERT_TRUE( engine.initialize( desc ) );
    engine.getClipStore().addClip( sw::hashed_string( "test/tone" ), test::AudioTestUtil::makeSineClip( 4000.0f, 0.2f, 48000 ) );

    const sw::AudioPlayingId  musicId     = engine.playClip( sw::hashed_string( "test/tone" ), sw::hashed_string( "music" ), sw::AudioClipPlayParams{} );
    const sw::vector<float32> listMusic   = test::AudioTestUtil::render( engine, 9600 );
    const float32             musicAmount = test::AudioTestUtil::computeToneAmplitude( listMusic, 0, 4800, 4800, 4000.0f );
    engine.stop( musicId, 0.0f );
    (void)engine.playClip( sw::hashed_string( "test/tone" ), sw::hashed_string( "sfx" ), sw::AudioClipPlayParams{} );
    const sw::vector<float32> listSfx   = test::AudioTestUtil::render( engine, 9600 );
    const float32             sfxAmount = test::AudioTestUtil::computeToneAmplitude( listSfx, 0, 4800, 4800, 4000.0f );
    SW_EXPECT_TRUE( musicAmount > 0.1f );
    // 200 Hz 로우패스는 4 kHz 를 약 52 dB 깎는다.
    SW_EXPECT_TRUE( sfxAmount < musicAmount * 0.01f );

    // 크게 겹쳐도 master 리미터(-6 dB)를 넘지 않는다.
    engine.getClipStore().addClip( sw::hashed_string( "test/loud" ), test::AudioTestUtil::makeConstantClip( 0.9f, 48000 ) );
    (void)engine.playClip( sw::hashed_string( "test/loud" ), sw::hashed_string( "music" ), sw::AudioClipPlayParams{} );
    (void)engine.playClip( sw::hashed_string( "test/loud" ), sw::hashed_string( "music" ), sw::AudioClipPlayParams{} );
    const sw::vector<float32> listLoud = test::AudioTestUtil::render( engine, 9600 );
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( listLoud, 0, 0, 9600 ) <= 0.5f + 1e-4f );

    SW_TEST_DEFENSIVE_SCOPE( "Unknown effect types and parameters are rejected" );
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master">
        <_listEffect><AudioEffectDesc _type="Chorus" /></_listEffect></AudioBusDesc></_listBus></AudioMixerDesc>)" ) );
    SW_EXPECT_FALSE( desc.loadFromXMLText( R"(<AudioMixerDesc><_listBus><AudioBusDesc _name="master">
        <_listEffect><AudioEffectDesc _type="Reverb"><_listParameter><AudioEffectParameterDesc _name="size" _value="1" />
        </_listParameter></AudioEffectDesc></_listEffect></AudioBusDesc></_listBus></AudioMixerDesc>)" ) );
}
