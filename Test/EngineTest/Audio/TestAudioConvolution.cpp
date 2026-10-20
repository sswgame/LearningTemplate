#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/DSP/AudioEffect.h"
#include "Engine/Audio/DSP/AudioFft.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioConvolutionTest — FFT 왕복과 컨볼루션 리버브(임펄스 응답 데이터, 한 블록 늦은 젖은 소리, 필수 자원)
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 이펙트 하나를 담은 믹서 그래프 데이터를 읽어 그 이펙트 데이터를 돌려줍니다. */
    [[nodiscard]] bool loadConvolutionTestEffectDesc( const sw::string& effectXML, sw::AudioEffectDesc& outDesc )
    {
        sw::AudioMixerDesc desc;
        sw::string         text;
        text.append( "<AudioMixerDesc><_listBus><AudioBusDesc _name=\"master\"><_listEffect>" );
        text.append( effectXML );
        text.append( "</_listEffect></AudioBusDesc></_listBus></AudioMixerDesc>" );
        const bool bLoaded    = desc.loadFromXMLText( text );
        const bool bHasEffect = bLoaded && desc._listBus.empty() == false && desc._listBus[0]._listEffect.empty() == false;
        if ( bHasEffect == false )
            return false;
        outDesc = desc._listBus[0]._listEffect[0];
        return true;
    }
} // namespace

/**
 * @brief [AudioConvolutionTest] FFT 를 갔다 오면 원래 값이고, 임펄스(첫 샘플 1)의 스펙트럼은 모든 빈이 1 이다
 */
SW_TEST_CASE( AudioConvolutionTest, FftRoundTripsAndTransformsAnImpulse )
{
    sw::AudioFft fft;
    SW_ASSERT_TRUE( fft.initialize( 64 ) );
    SW_EXPECT_FALSE( fft.initialize( 48 ) ); // 2 의 거듭제곱이 아니다
    SW_ASSERT_TRUE( fft.initialize( 64 ) );

    sw::AudioComplex arrData[64]{};
    sw::AudioComplex arrOriginal[64]{};
    for ( uint32 index = 0; index < 64; ++index )
    {
        arrData[index]._real = sw::MathUtil::sin( static_cast<float32>( index ) * 0.37f ) + 0.25f * static_cast<float32>( index % 5 );
        arrOriginal[index]   = arrData[index];
    }
    fft.forward( arrData );
    fft.inverse( arrData );
    for ( uint32 index = 0; index < 64; ++index )
    {
        SW_EXPECT_NEAR_EQUAL( arrOriginal[index]._real, arrData[index]._real, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, arrData[index]._imaginary, 1e-4f );
    }

    sw::AudioComplex arrImpulse[64]{};
    arrImpulse[0]._real = 1.0f;
    fft.forward( arrImpulse );
    for ( const sw::AudioComplex& bin : arrImpulse )
    {
        SW_EXPECT_NEAR_EQUAL( 1.0f, bin._real, 1e-5f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, bin._imaginary, 1e-5f );
    }
}

/**
 * @brief [AudioConvolutionTest] 컨볼루션 리버브 — 응답이 "300 프레임 뒤 0.5" 하나면 입력 임펄스가 블록(256) + 300 프레임 뒤에 0.5 로 나온다. 응답이 없으면 만들지 않는다
 */
SW_TEST_CASE( AudioConvolutionTest, ConvolutionReverbAppliesTheImpulseResponse )
{
    constexpr uint32    kImpulseFrames = 1024;
    sw::vector<float32> listImpulse( kImpulseFrames, 0.0f );
    listImpulse[300] = 0.5f;
    sw::vector<uint8> bytes;
    sw::AudioWavEncoder::encodeFloat32( listImpulse.data(), kImpulseFrames, 1, 0, sw::audio::kSampleRate, bytes );
    const sw::string path = test::makeTempPath( "convolution_ir.wav" );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, bytes.data(), bytes.size() ) );

    sw::string effectXML;
    effectXML.append( "<AudioEffectDesc _type=\"ConvolutionReverb\" _impulse=\"" );
    effectXML.append( path );
    effectXML.append( "\"><_listParameter><AudioEffectParameterDesc _name=\"wet\" _value=\"1\" /></_listParameter></AudioEffectDesc>" );
    sw::AudioEffectDesc desc;
    SW_ASSERT_TRUE( loadConvolutionTestEffectDesc( effectXML, desc ) );
    sw::unique_ptr<sw::IAudioEffect> pEffect = sw::AudioEffectRegistry::createEffect( desc, sw::AudioChannelLayout::Stereo );
    SW_ASSERT_NOT_NULL( pEffect.get() );

    constexpr uint32     kBlockCount = 6;
    sw::vector<float32>  listLeft;
    sw::AudioBlockBuffer buffer;
    buffer.allocate( sw::AudioChannelLayout::Stereo );
    for ( uint32 blockIndex = 0; blockIndex < kBlockCount; ++blockIndex )
    {
        buffer.clear( sw::audio::kBlockFrameCount );
        const sw::AudioBlockView block = buffer.makeView( sw::audio::kBlockFrameCount );
        if ( blockIndex == 0 )
            block.getChannel( 0 )[0] = 1.0f;
        pEffect->process( block );
        listLeft.insert( listLeft.end(), block.getChannel( 0 ), block.getChannel( 0 ) + sw::audio::kBlockFrameCount );
    }
    const uint32 expectedFrame = sw::audio::kBlockFrameCount + 300;
    SW_EXPECT_NEAR_EQUAL( 0.5f, listLeft[expectedFrame], 1e-4f );
    for ( uint32 frameIndex = 0; frameIndex < listLeft.size(); ++frameIndex )
    {
        if ( frameIndex != expectedFrame )
            SW_EXPECT_NEAR_EQUAL( 0.0f, listLeft[frameIndex], 1e-4f );
    }

    SW_TEST_DEFENSIVE_SCOPE( "a convolution reverb without an impulse response is an error" );
    // 믹서 에셋 읽기가 이 서술을 먼저 거른다(MixerLoadRejectsConvolutionReverbWithoutImpulse). 효과 생성도 같은 결함을 받지 않는지 본다.
    sw::AudioEffectDesc missing;
    missing._type = sw::hashed_string( "ConvolutionReverb" );
    SW_EXPECT_TRUE( sw::AudioEffectRegistry::createEffect( missing, sw::AudioChannelLayout::Stereo ) == nullptr );
}

SW_TEST_CASE( AudioConvolutionTest, MixerLoadRejectsConvolutionReverbWithoutImpulse )
{
    sw::AudioMixerDesc desc;
    const sw::string   xml = "<AudioMixerDesc><_listBus><AudioBusDesc _name=\"master\"><_listEffect>"
                             "<AudioEffectDesc _type=\"ConvolutionReverb\" /></_listEffect></AudioBusDesc></_listBus></AudioMixerDesc>";
    SW_EXPECT_FALSE( desc.loadFromXMLText( xml ) );
}
