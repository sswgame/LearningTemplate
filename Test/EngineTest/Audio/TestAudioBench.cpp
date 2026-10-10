#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDesc.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioBenchTest — 보이스 N 개의 렌더 CPU 비용(실시간 대비). 숫자는 로그로 남기고, 시험은 실제로 그만큼 섞었는지만 본다.
// Release 에서 잰다: EngineTest.exe --test_filter=AudioBenchTest.*
// ------------------------------------------------------------------------------

namespace
{
    struct AudioBenchTestInternal
    {
        static constexpr const utf8* kLibraryXML = R"(
<AudioEventLibrary>
	<_listParameter><AudioParameterDesc _name="Rpm" _minValue="0" _maxValue="1" _defaultValue="0.5" /></_listParameter>
	<_listEvent>
		<AudioEventDesc _name="Engine" _bus="sfx" _attenuation="Small" _bLoop="true" _pitchMin="-3" _pitchMax="3" _virtual="Virtualize">
			<_listClip><AudioClipEntry _path="bench/tone" /></_listClip>
			<_listParameterMap>
				<AudioParameterMapping _parameter="Rpm" _target="Pitch"><_listPoint><AudioCurvePoint _x="0" _y="-5" /><AudioCurvePoint _x="1" _y="5" /></_listPoint></AudioParameterMapping>
			</_listParameterMap>
		</AudioEventDesc>
	</_listEvent>
</AudioEventLibrary>)";

        /** @brief 기본 데이터 그래프(리미터 · 리버브 리턴 · 센드 · 스냅샷)를 읽고 실제 보이스 상한을 정합니다. */
        static bool initializeEngine( sw::AudioEngine& engine, uint32 realVoiceCount )
        {
            sw::AudioMixerDesc mixerDesc;
            if ( mixerDesc.loadFromResource( "engine/audio/default.audiomixer.xml" ) == false )
                return false;
            mixerDesc._maxRealVoiceCount = realVoiceCount;
            mixerDesc._maxVoiceCount     = 1024;
            if ( engine.initialize( mixerDesc ) == false )
                return false;
            // 44.1 kHz 모노 톤 — 리샘플 경로를 탄다.
            engine.getClipStore().addClip( sw::hashed_string( "bench/tone" ), test::AudioTestUtil::makeSineClip( 220.0f, 0.05f, 44100, 44100 ) );
            sw::AudioEventLibrary library;
            return library.loadFromXMLText( kLibraryXML ) && engine.loadEventLibrary( sw::hashed_string( "bench" ), library );
        }
    };
} // namespace

/**
 * @brief [AudioBenchTest] 보이스 N 개(공간화 · 피치 리샘플 · 파라미터 · 가림 로우패스 + 리버브 · 리미터 버스)를 1 초 렌더하는 비용
 * @details 에미터는 리스너 둘레 원 위에 두고 반은 가림(로우패스가 걸린다)이다. 실제 보이스 상한을 N 으로 두어 모두 섞게 한다.
 */
SW_TEST_CASE( AudioBenchTest, RenderCostPerVoice )
{
    const uint32 arrVoiceCount[3] = { 32, 64, 128 };
    for ( const uint32 voiceCount : arrVoiceCount )
    {
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( AudioBenchTestInternal::initializeEngine( engine, voiceCount ) );
        sw::AudioListenerState listener;
        listener._bActive = true;
        engine.setListener( 0, listener );
        for ( uint32 voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex )
        {
            const float32            angle     = static_cast<float32>( voiceIndex ) * 0.37f;
            const sw::AudioEmitterID emitterID = voiceIndex + 1;
            engine.setEmitter( emitterID, sw::float3( 4.0f * std::cos( angle ), 0.0f, 4.0f * std::sin( angle ) ), sw::float3( 0.0f, 0.0f, 0.0f ) );
            engine.setEmitterOcclusion( emitterID, ( voiceIndex % 2u ) == 0u ? 0.0f : 0.7f );
            (void)engine.postEvent( sw::hashed_string( "Engine" ), emitterID );
        }
        (void)test::AudioTestUtil::render( engine, 4096 ); // 클립 시작 · 첫 블록 램프

        constexpr uint32    kFrameCount = sw::audio::kSampleRate;
        sw::vector<float32> listOutput( static_cast<size_t>( kFrameCount ) * 2 );
        const sw::Stopwatch stopwatch;
        engine.render( listOutput.data(), kFrameCount );
        [[maybe_unused]] const int64 elapsedMicro = stopwatch.getElapsedMicroseconds();

        const sw::AudioEngineStats stats = engine.getStats();
        SW_EXPECT_EQUAL( voiceCount, stats._realVoiceCount );
        SW_EXPECT_TRUE( test::AudioTestUtil::computeRms( listOutput, 0, 0, kFrameCount ) > 1e-3f );
        SW_LOG_INFO( "[AudioBench] %# voices: %# us per 1 s of audio = %#%% of one core, %# us per 256-frame block (%# ns per voice-frame)", voiceCount, elapsedMicro,
                     static_cast<float32>( elapsedMicro ) / 10000.0f, elapsedMicro * 256 / kFrameCount,
                     static_cast<float32>( elapsedMicro ) * 1000.0f / static_cast<float32>( static_cast<uint64>( voiceCount ) * kFrameCount ) );
    }
}
