#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioVoice.h"

using namespace sw;

namespace
{
    struct AudioVoiceTestUtil
    {
        static constexpr float32 kTolerance = 0.0001f;

        static shared_ptr<const AudioClipData> makeClip( const vector<float32>& listSample, uint16 channelCount )
        {
            shared_ptr<AudioClipData> pClip = sw::make_shared<AudioClipData>();
            pClip->_listSample              = listSample;
            pClip->_frameCount              = static_cast<uint32>( listSample.size() ) / channelCount;
            pClip->_sampleRate              = audio::kSampleRate;
            pClip->_channelCount            = channelCount;
            return pClip;
        }

        /** @brief 두 채널이 모두 @p value 인 스테레오 클립입니다. */
        static shared_ptr<const AudioClipData> makeConstantStereo( uint32 frameCount, float32 value )
        {
            return makeClip( vector<float32>( static_cast<size_t>( frameCount ) * 2u, value ), 2 );
        }

        /** @brief 프레임마다 두 채널이 @p listValue 의 값인 스테레오 클립입니다. */
        static shared_ptr<const AudioClipData> makeStereoFrom( const vector<float32>& listValue )
        {
            vector<float32> listSample;
            for ( const float32 value : listValue )
            {
                listSample.push_back( value );
                listSample.push_back( value );
            }
            return makeClip( listSample, 2 );
        }

        static AudioPanDirection makeCenter() { return AudioPanDirection::makeStereoPan( 0.0f ); }

        /** @brief 게인 1 · 가운데로 한 블록(`kBlockFrameCount`)을 섞습니다. 버퍼는 먼저 비웁니다. */
        static void mixCenter( AudioVoice& voice, AudioBlockBuffer& buffer, uint32 frameCount = audio::kBlockFrameCount )
        {
            voice.setTarget( 1.0f, makeCenter(), AudioChannelLayout::Stereo );
            buffer.clear( frameCount );
            voice.mix( buffer.makeView( frameCount ) );
        }

        static float32* getLeft( AudioBlockBuffer& buffer ) { return buffer.makeView( audio::kBlockFrameCount ).getChannel( 0 ); }
        static float32* getRight( AudioBlockBuffer& buffer ) { return buffer.makeView( audio::kBlockFrameCount ).getChannel( 1 ); }

        /** @brief @p listSample(스테레오 교차) 를 스트리밍 클립으로 만듭니다(32 비트 float WAV). 샘플을 메모리 클립과 그대로 맞춰 견줄 수 있습니다. */
        static shared_ptr<const AudioClipData> makeStreamedStereo( const vector<float32>& listSample )
        {
            const uint32  dataBytes = static_cast<uint32>( listSample.size() * sizeof( float32 ) );
            vector<uint8> listByte;
            ByteWriter    writer{ listByte };
            writer.writeBytes( reinterpret_cast<const uint8*>( "RIFF" ), 4 );
            writer.writeUint32( 36u + dataBytes );
            writer.writeBytes( reinterpret_cast<const uint8*>( "WAVE" ), 4 );
            writer.writeBytes( reinterpret_cast<const uint8*>( "fmt " ), 4 );
            writer.writeUint32( 16 );
            writer.writeUint16( 3 );
            writer.writeUint16( 2 );
            writer.writeUint32( audio::kSampleRate );
            writer.writeUint32( audio::kSampleRate * 8u );
            writer.writeUint16( 8 );
            writer.writeUint16( 32 );
            writer.writeBytes( reinterpret_cast<const uint8*>( "data" ), 4 );
            writer.writeUint32( dataBytes );
            for ( const float32 sample : listSample )
            {
                writer.writeFloat32( sample );
            }

            shared_ptr<AudioClipData> pClip = sw::make_shared<AudioClipData>();
            pClip->_pStream                 = AudioStreamSource::tryCreate( "voice.wav", listByte, 0.0f );
            pClip->_frameCount              = static_cast<uint32>( listSample.size() / 2u );
            pClip->_sampleRate              = audio::kSampleRate;
            pClip->_channelCount            = 2;
            return pClip;
        }
    };
    using Util = AudioVoiceTestUtil;
} // namespace

/** @brief [AudioVoiceTest] 클립이 없거나 길이가 0 이면 시작하자마자 끝이고 섞어도 아무것도 하지 않는다 */
SW_TEST_CASE( AudioVoiceTest, MissingOrEmptyClipFinishesAtStart )
{
    AudioVoice voice;
    SW_EXPECT_FALSE( voice.isActive() );
    SW_EXPECT_FALSE( voice.isFinished() );

    voice.start( shared_ptr<const AudioClipData>{}, false, 0, true, 0 );
    SW_EXPECT_TRUE( voice.isFinished() );
    SW_EXPECT_FALSE( voice.isActive() );

    voice.start( Util::makeClip( vector<float32>{}, 2 ), true, 0, true, 0 );
    SW_EXPECT_TRUE( voice.isFinished() );
    SW_EXPECT_FALSE( voice.isActive() );
}

/** @brief [AudioVoiceTest] 첫 블록은 0 에서 목표로 램프하고(클릭 없음), 다음 블록은 목표 게인 그대로다 */
SW_TEST_CASE( AudioVoiceTest, RampInStartsFromZero )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, true, 0 );

    Util::mixCenter( voice, buffer );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 1.0f / static_cast<float32>( audio::kBlockFrameCount ), pLeft[0], Util::kTolerance );
    SW_EXPECT_NEAR( 0.5f, pLeft[audio::kBlockFrameCount / 2u - 1u], Util::kTolerance );
    SW_EXPECT_NEAR( 1.0f, pLeft[audio::kBlockFrameCount - 1u], Util::kTolerance );

    Util::mixCenter( voice, buffer );
    SW_EXPECT_NEAR( 1.0f, pLeft[0], Util::kTolerance );
    SW_EXPECT_NEAR( 1.0f, pLeft[audio::kBlockFrameCount - 1u], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 램프를 끄면 첫 샘플부터 목표 게인이다(박자에 맞춘 시작) */
SW_TEST_CASE( AudioVoiceTest, NoRampAppliesTheTargetFromTheFirstSample )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    voice.setTarget( 0.5f, Util::makeCenter(), AudioChannelLayout::Stereo );
    buffer.clear( audio::kBlockFrameCount );
    voice.mix( buffer.makeView( audio::kBlockFrameCount ) );
    SW_EXPECT_NEAR( 0.5f, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( 0.5f, Util::getRight( buffer )[0], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 블록 사이의 볼륨 변화는 블록 안에서 선형으로 옮겨 간다 */
SW_TEST_CASE( AudioVoiceTest, VolumeChangeRampsAcrossTheBlock )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    Util::mixCenter( voice, buffer );

    voice.setTarget( 0.0f, Util::makeCenter(), AudioChannelLayout::Stereo );
    buffer.clear( audio::kBlockFrameCount );
    voice.mix( buffer.makeView( audio::kBlockFrameCount ) );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 0.5f, pLeft[audio::kBlockFrameCount / 2u - 1u], Util::kTolerance );
    SW_EXPECT_NEAR( 0.0f, pLeft[audio::kBlockFrameCount - 1u], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 모노 소스의 가운데는 등전력이라 채널마다 -3 dB 다 */
SW_TEST_CASE( AudioVoiceTest, MonoCenterIsEqualPower )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeClip( vector<float32>( 2048, 1.0f ), 1 ), false, 0, false, 0 );
    Util::mixCenter( voice, buffer );
    SW_EXPECT_NEAR( MathUtil::kInvSqrt2, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( MathUtil::kInvSqrt2, Util::getRight( buffer )[0], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 스테레오 소스는 밸런스다 — 한쪽으로 가면 반대쪽만 준다. 범위 밖 팬은 자른다 */
SW_TEST_CASE( AudioVoiceTest, StereoSourceBalance )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );

    voice.setTarget( 1.0f, AudioPanDirection::makeStereoPan( -0.5f ), AudioChannelLayout::Stereo );
    buffer.clear( audio::kBlockFrameCount );
    voice.mix( buffer.makeView( audio::kBlockFrameCount ) );
    SW_EXPECT_NEAR( 1.0f, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( 0.5f, Util::getRight( buffer )[0], Util::kTolerance );

    AudioVoice hardRight;
    hardRight.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    AudioPanDirection direction = AudioPanDirection::makeStereoPan( 1.0f );
    direction._right            = 5.0f;
    hardRight.setTarget( 1.0f, direction, AudioChannelLayout::Stereo );
    buffer.clear( audio::kBlockFrameCount );
    hardRight.mix( buffer.makeView( audio::kBlockFrameCount ) );
    SW_EXPECT_NEAR( 0.0f, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( 1.0f, Util::getRight( buffer )[0], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 스테레오 소스는 어느 레이아웃에서나 앞 두 채널에만 나간다 */
SW_TEST_CASE( AudioVoiceTest, StereoSourceUsesOnlyTheFrontPairInSurround )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Surround51 );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    voice.setTarget( 1.0f, Util::makeCenter(), AudioChannelLayout::Surround51 );
    buffer.clear( audio::kBlockFrameCount );
    const AudioBlockView view = buffer.makeView( audio::kBlockFrameCount );
    voice.mix( view );
    SW_EXPECT_NEAR( 1.0f, view.getChannel( 0 )[0], Util::kTolerance );
    SW_EXPECT_NEAR( 1.0f, view.getChannel( 1 )[0], Util::kTolerance );
    for ( uint32 channelIndex = 2; channelIndex < view._channelCount; ++channelIndex )
    {
        SW_EXPECT_NEAR( 0.0f, view.getChannel( channelIndex )[0], 0.0f );
    }
}

/** @brief [AudioVoiceTest] 바이노럴이어도 스테레오 소스는 HRTF 를 타지 않고 그대로 나간다 */
SW_TEST_CASE( AudioVoiceTest, BinauralLayoutLeavesStereoSourcesAlone )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Binaural );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 0.5f ), false, 0, false, 0 );
    AudioPanDirection direction = Util::makeCenter();
    direction._mode             = AudioPanMode::Surround;
    voice.setTarget( 1.0f, direction, AudioChannelLayout::Binaural );
    buffer.clear( audio::kBlockFrameCount );
    voice.mix( buffer.makeView( audio::kBlockFrameCount ) );
    SW_EXPECT_NEAR( 0.5f, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( 0.5f, Util::getRight( buffer )[0], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 시작 지연 동안은 소리도 위치 진행도 없고, 블록을 넘는 지연은 다음 블록까지 이어진다 */
SW_TEST_CASE( AudioVoiceTest, StartDelay )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 100, false, 0 );
    Util::mixCenter( voice, buffer );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 0.0f, pLeft[99], 0.0f );
    SW_EXPECT_NEAR( 1.0f, pLeft[100], Util::kTolerance );
    SW_EXPECT_NEAR( 156.0, voice.getPosition(), 0.0 );

    AudioVoice longDelay;
    longDelay.start( Util::makeConstantStereo( 2048, 1.0f ), false, audio::kBlockFrameCount + 44u, false, 0 );
    Util::mixCenter( longDelay, buffer );
    SW_EXPECT_NEAR( 0.0f, pLeft[audio::kBlockFrameCount - 1u], 0.0f );
    SW_EXPECT_NEAR( 0.0, longDelay.getPosition(), 0.0 );
    Util::mixCenter( longDelay, buffer );
    SW_EXPECT_NEAR( 0.0f, pLeft[43], 0.0f );
    SW_EXPECT_NEAR( 1.0f, pLeft[44], Util::kTolerance );
    SW_EXPECT_NEAR( 212.0, longDelay.getPosition(), 0.0 );
}

/** @brief [AudioVoiceTest] 루프가 아니면 끝에서 끝난다 — 뒤 프레임은 더하지 않고, 끝난 보이스는 더 섞이지 않는다 */
SW_TEST_CASE( AudioVoiceTest, NonLoopingVoiceEndsAtTheLastFrame )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 10, 1.0f ), false, 0, false, 0 );
    SW_EXPECT_TRUE( voice.isActive() );
    Util::mixCenter( voice, buffer );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 1.0f, pLeft[9], Util::kTolerance );
    SW_EXPECT_NEAR( 0.0f, pLeft[10], 0.0f );
    SW_EXPECT_TRUE( voice.isFinished() );
    SW_EXPECT_FALSE( voice.isActive() );

    Util::mixCenter( voice, buffer );
    SW_EXPECT_NEAR( 0.0f, pLeft[0], 0.0f );
    SW_EXPECT_NEAR( 10.0, voice.getPosition(), 0.0 );
}

/** @brief [AudioVoiceTest] 루프는 끝나지 않고 위치를 감으며, 값은 클립을 되풀이한다 */
SW_TEST_CASE( AudioVoiceTest, LoopingVoiceWraps )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeStereoFrom( { 0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f } ), true, 0, false, 0 );
    Util::mixCenter( voice, buffer );
    SW_EXPECT_FALSE( voice.isFinished() );
    SW_EXPECT_NEAR( static_cast<float64>( audio::kBlockFrameCount % 10u ), voice.getPosition(), 0.000001 );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 0.3f, pLeft[13], Util::kTolerance );
    SW_EXPECT_NEAR( 0.9f, pLeft[29], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 소수 위치는 선형 보간이고, 끝 프레임의 다음 자리는 루프면 첫 프레임 · 아니면 끝 프레임 그대로다 */
SW_TEST_CASE( AudioVoiceTest, FractionalRateInterpolates )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    const float32* pLeft = Util::getLeft( buffer );

    AudioVoice once;
    once.start( Util::makeStereoFrom( { 0.0f, 1.0f, 2.0f, 3.0f } ), false, 0, false, 0 );
    once.setPlaybackRate( 0.5 );
    Util::mixCenter( once, buffer );
    SW_EXPECT_NEAR( 0.5f, pLeft[1], Util::kTolerance );
    SW_EXPECT_NEAR( 3.0f, pLeft[6], Util::kTolerance );
    SW_EXPECT_NEAR( 3.0f, pLeft[7], Util::kTolerance ); // 다음 자리가 없어 끝 값을 잡는다
    SW_EXPECT_TRUE( once.isFinished() );

    AudioVoice loop;
    loop.start( Util::makeStereoFrom( { 0.0f, 1.0f, 2.0f, 3.0f } ), true, 0, false, 0 );
    loop.setPlaybackRate( 0.5 );
    Util::mixCenter( loop, buffer );
    SW_EXPECT_NEAR( 1.5f, pLeft[7], Util::kTolerance ); // 3 과 첫 프레임 0 의 가운데
    SW_EXPECT_NEAR( 0.0f, pLeft[8], Util::kTolerance );

    AudioVoice fast;
    fast.start( Util::makeStereoFrom( { 0.0f, 1.0f, 2.0f, 3.0f } ), false, 0, false, 0 );
    fast.setPlaybackRate( 2.0 );
    Util::mixCenter( fast, buffer );
    SW_EXPECT_NEAR( 2.0f, pLeft[1], Util::kTolerance );
    SW_EXPECT_NEAR( 0.0f, pLeft[2], 0.0f );
    SW_EXPECT_TRUE( fast.isFinished() );
}

/** @brief [AudioVoiceTest] 위치 옮기기: 음수는 0 으로, 끝 이상은 루프면 감고 아니면 끝낸다. 클립이 없으면 아무것도 하지 않는다 */
SW_TEST_CASE( AudioVoiceTest, SetPositionBoundaries )
{
    AudioVoice empty;
    empty.setPosition( 5.0 );
    SW_EXPECT_NEAR( 0.0, empty.getPosition(), 0.0 );

    AudioVoice loop;
    loop.start( Util::makeConstantStereo( 10, 1.0f ), true, 0, false, 0 );
    loop.setPosition( -3.0 );
    SW_EXPECT_NEAR( 0.0, loop.getPosition(), 0.0 );
    loop.setPosition( 9.5 );
    SW_EXPECT_NEAR( 9.5, loop.getPosition(), 0.0 );
    loop.setPosition( 10.0 );
    SW_EXPECT_NEAR( 0.0, loop.getPosition(), 0.0 );
    loop.setPosition( 25.0 );
    SW_EXPECT_NEAR( 5.0, loop.getPosition(), 0.0 );
    SW_EXPECT_FALSE( loop.isFinished() );

    AudioVoice once;
    once.start( Util::makeConstantStereo( 10, 1.0f ), false, 0, false, 0 );
    once.setPosition( 9.5 );
    SW_EXPECT_FALSE( once.isFinished() );
    once.setPosition( 10.0 );
    SW_EXPECT_TRUE( once.isFinished() );
}

/** @brief [AudioVoiceTest] 길이 0 페이드는 다음 프레임에 바로 닿는다. 음수 목표는 0 이다 */
SW_TEST_CASE( AudioVoiceTest, ZeroLengthFadeIsImmediate )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    voice.setFade( 0.5f, 0, 0, false );
    SW_EXPECT_NEAR( 0.5f, voice.getFadeTarget(), 0.0f );
    SW_EXPECT_NEAR( 1.0f, voice.getFadeGain(), 0.0f ); // 다음 믹스 전에는 그대로다
    Util::mixCenter( voice, buffer );
    SW_EXPECT_NEAR( 0.5f, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( 0.5f, voice.getFadeGain(), 0.0f );
    SW_EXPECT_FALSE( voice.isFinished() );

    voice.setFade( -2.0f, 0, 0, false );
    SW_EXPECT_NEAR( 0.0f, voice.getFadeTarget(), 0.0f );
}

/** @brief [AudioVoiceTest] 페이드는 프레임마다 일정하게 움직이고, 지연 동안은 그대로이며, 목표에 닿으면 넘지 않는다 */
SW_TEST_CASE( AudioVoiceTest, FadeIsLinearAfterItsDelay )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    voice.setFade( 0.0f, 100, 5, false );
    Util::mixCenter( voice, buffer );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 1.0f, pLeft[4], Util::kTolerance );
    SW_EXPECT_NEAR( 0.99f, pLeft[5], Util::kTolerance );
    SW_EXPECT_NEAR( 0.5f, pLeft[54], Util::kTolerance );
    SW_EXPECT_NEAR( 0.0f, pLeft[200], 0.0f );
    SW_EXPECT_NEAR( 0.0f, voice.getFadeGain(), 0.0f );
    SW_EXPECT_FALSE( voice.isFinished() );

    voice.setFade( 1.0f, 100, 0, false );
    Util::mixCenter( voice, buffer );
    SW_EXPECT_NEAR( 0.01f, pLeft[0], Util::kTolerance );
    SW_EXPECT_NEAR( 1.0f, pLeft[200], 0.0f );
}

/** @brief [AudioVoiceTest] 끝낼 페이드는 목표에 닿으면 보이스를 끝낸다. 이미 목표에 있으면 첫 프레임에 끝나 소리를 내지 않는다 */
SW_TEST_CASE( AudioVoiceTest, FadeOutStopsTheVoice )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    const float32* pLeft = Util::getLeft( buffer );

    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), true, 0, false, 0 );
    voice.setFade( 0.0f, 10, 0, true );
    Util::mixCenter( voice, buffer );
    SW_EXPECT_TRUE( voice.isFinished() );
    SW_EXPECT_NEAR( 0.9f, pLeft[0], Util::kTolerance );
    SW_EXPECT_NEAR( 0.0f, pLeft[20], 0.0f );
    SW_EXPECT_TRUE( voice.getPosition() <= 11.0 );

    AudioVoice atTarget;
    atTarget.start( Util::makeConstantStereo( 2048, 1.0f ), true, 0, false, 0 );
    atTarget.setFade( 1.0f, 100, 0, true );
    Util::mixCenter( atTarget, buffer );
    SW_EXPECT_TRUE( atTarget.isFinished() );
    SW_EXPECT_NEAR( 0.0f, pLeft[0], 0.0f );
    SW_EXPECT_NEAR( 0.0, atTarget.getPosition(), 0.0 );
}

/** @brief [AudioVoiceTest] 일시정지는 섞지도 진행하지도 않고(가상 진행 포함), 풀면 그 자리에서 잇는다 */
SW_TEST_CASE( AudioVoiceTest, PauseHoldsThePosition )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeStereoFrom( vector<float32>( 2048, 1.0f ) ), false, 0, false, 0 );
    voice.setPaused( true );
    SW_EXPECT_TRUE( voice.isPaused() );
    Util::mixCenter( voice, buffer );
    voice.advanceVirtual( 500 );
    SW_EXPECT_NEAR( 0.0f, Util::getLeft( buffer )[0], 0.0f );
    SW_EXPECT_NEAR( 0.0, voice.getPosition(), 0.0 );

    voice.setPaused( false );
    Util::mixCenter( voice, buffer );
    SW_EXPECT_NEAR( 1.0f, Util::getLeft( buffer )[0], Util::kTolerance );
    SW_EXPECT_NEAR( static_cast<float64>( audio::kBlockFrameCount ), voice.getPosition(), 0.0 );
}

/** @brief [AudioVoiceTest] 가상 진행은 위치만 옮긴다 — 재생 속도를 따르고, 루프면 감고, 아니면 끝낸다. 시작 지연을 먼저 쓴다 */
SW_TEST_CASE( AudioVoiceTest, VirtualAdvanceMovesThePosition )
{
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 100, 1.0f ), false, 0, false, 0 );
    voice.setPlaybackRate( 2.0 );
    voice.advanceVirtual( 30 );
    SW_EXPECT_NEAR( 60.0, voice.getPosition(), 0.0 );
    SW_EXPECT_FALSE( voice.isFinished() );
    voice.advanceVirtual( 20 );
    SW_EXPECT_TRUE( voice.isFinished() );

    AudioVoice loop;
    loop.start( Util::makeConstantStereo( 100, 1.0f ), true, 0, false, 0 );
    loop.advanceVirtual( 230 );
    SW_EXPECT_NEAR( 30.0, loop.getPosition(), 0.0 );
    SW_EXPECT_FALSE( loop.isFinished() );

    AudioVoice delayed;
    delayed.start( Util::makeConstantStereo( 1000, 1.0f ), false, 100, false, 0 );
    delayed.advanceVirtual( 40 );
    SW_EXPECT_NEAR( 0.0, delayed.getPosition(), 0.0 );
    delayed.advanceVirtual( 110 );
    SW_EXPECT_NEAR( 50.0, delayed.getPosition(), 0.0 );
}

/** @brief [AudioVoiceTest] 가상 동안의 끝낼 페이드도 보이스를 끝내고, 페이드 중에는 프레임마다 위치가 함께 간다 */
SW_TEST_CASE( AudioVoiceTest, VirtualAdvanceRunsTheFade )
{
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 1000, 1.0f ), true, 0, false, 0 );
    voice.setFade( 0.0f, 10, 0, true );
    voice.advanceVirtual( 5 );
    SW_EXPECT_NEAR( 5.0, voice.getPosition(), 0.0 );
    SW_EXPECT_NEAR( 0.5f, voice.getFadeGain(), Util::kTolerance );
    SW_EXPECT_FALSE( voice.isFinished() );
    voice.advanceVirtual( 500 );
    SW_EXPECT_TRUE( voice.isFinished() );
    SW_EXPECT_TRUE( voice.getPosition() <= 11.0 );

    AudioVoice instant;
    instant.start( Util::makeConstantStereo( 1000, 1.0f ), true, 0, false, 0 );
    instant.setFade( 0.0f, 0, 0, false );
    instant.advanceVirtual( 100 ); // 페이드가 닿은 뒤로는 나머지를 한 번에 나아간다 — 합이 같아야 한다
    SW_EXPECT_NEAR( 100.0, instant.getPosition(), 0.0 );
    SW_EXPECT_NEAR( 0.0f, instant.getFadeGain(), 0.0f );
}

/** @brief [AudioVoiceTest] 가상에서 돌아오면 램프를 꺼 두었어도 0 에서 오른다 — 들리지 않던 소리가 튀어나오지 않는다 */
SW_TEST_CASE( AudioVoiceTest, ReturningFromVirtualRampsFromZero )
{
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( Util::makeConstantStereo( 2048, 1.0f ), false, 0, false, 0 );
    Util::mixCenter( voice, buffer );
    voice.advanceVirtual( audio::kBlockFrameCount );
    Util::mixCenter( voice, buffer );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 1.0f / static_cast<float32>( audio::kBlockFrameCount ), pLeft[0], Util::kTolerance );
    SW_EXPECT_NEAR( 1.0f, pLeft[audio::kBlockFrameCount - 1u], Util::kTolerance );
}

/** @brief [AudioVoiceTest] 로우패스 값은 20 Hz ~ 열림으로 자르고, 0.5 % 안의 변화는 무시한다(계수를 프레임마다 다시 구하지 않는다) */
SW_TEST_CASE( AudioVoiceTest, LowPassCutoffRules )
{
    AudioVoice voice;
    SW_EXPECT_NEAR( audio::kFilterOpenHz, voice.getLowPassHz(), 0.0f );
    voice.setLowPass( 1000.0f );
    SW_EXPECT_NEAR( 1000.0f, voice.getLowPassHz(), 0.0f );
    voice.setLowPass( 1004.0f );
    SW_EXPECT_NEAR( 1000.0f, voice.getLowPassHz(), 0.0f );
    voice.setLowPass( 1010.0f );
    SW_EXPECT_NEAR( 1010.0f, voice.getLowPassHz(), 0.0f );
    voice.setLowPass( 1.0f );
    SW_EXPECT_NEAR( 20.0f, voice.getLowPassHz(), 0.0f );
    voice.setLowPass( 99999.0f );
    SW_EXPECT_NEAR( audio::kFilterOpenHz, voice.getLowPassHz(), 0.0f );
}

/** @brief [AudioVoiceTest] 로우패스는 소스에 걸린다 — 가장 높은 주파수(+1 · -1 교대)는 막고, 열어 두면 그대로 지난다 */
SW_TEST_CASE( AudioVoiceTest, LowPassAttenuatesHighFrequencies )
{
    vector<float32> listValue;
    for ( uint32 frame = 0; frame < 2048; ++frame )
    {
        listValue.push_back( ( frame % 2u ) == 0 ? 1.0f : -1.0f );
    }
    const shared_ptr<const AudioClipData> pClip = Util::makeStereoFrom( listValue );
    AudioBlockBuffer                      buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    const float32* pLeft = Util::getLeft( buffer );

    AudioVoice unfiltered;
    unfiltered.start( pClip, true, 0, false, 0 );
    Util::mixCenter( unfiltered, buffer );
    Util::mixCenter( unfiltered, buffer );
    SW_EXPECT_NEAR( 1.0f, MathUtil::abs( pLeft[100] ), Util::kTolerance );

    AudioVoice filtered;
    filtered.start( pClip, true, 0, false, 0 );
    filtered.setLowPass( 200.0f );
    Util::mixCenter( filtered, buffer );
    Util::mixCenter( filtered, buffer ); // 필터가 자리 잡은 뒤를 본다
    float32 peak{ 0.0f };
    for ( uint32 frame = 0; frame < audio::kBlockFrameCount; ++frame )
    {
        peak = MathUtil::max( peak, MathUtil::abs( pLeft[frame] ) );
    }
    SW_EXPECT_TRUE( peak < 0.01f );
}

/** @brief [AudioVoiceTest] 비우면 처음 상태다 — 클립을 놓아 수명이 끝난다 */
SW_TEST_CASE( AudioVoiceTest, ResetReleasesTheClip )
{
    shared_ptr<const AudioClipData> pClip = Util::makeConstantStereo( 100, 1.0f );
    AudioVoice                      voice;
    voice.start( pClip, true, 0, false, 0 );
    voice.setFade( 0.5f, 0, 0, false );
    voice.setLowPass( 100.0f );
    voice.setPosition( 50.0 );
    voice.setPaused( true );
    SW_EXPECT_EQUAL( pClip.get(), voice.getClip() );
    SW_EXPECT_TRUE( pClip.use_count() >= 2 );

    voice.reset();
    SW_EXPECT_NULL( voice.getClip() );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( pClip.use_count() ) );
    SW_EXPECT_FALSE( voice.isActive() );
    SW_EXPECT_FALSE( voice.isFinished() );
    SW_EXPECT_FALSE( voice.isPaused() );
    SW_EXPECT_NEAR( 0.0, voice.getPosition(), 0.0 );
    SW_EXPECT_NEAR( 1.0f, voice.getFadeTarget(), 0.0f );
    SW_EXPECT_NEAR( audio::kFilterOpenHz, voice.getLowPassHz(), 0.0f );
}

/** @brief [AudioVoiceTest] 스트리밍 클립은 같은 샘플의 메모리 클립과 샘플까지 같다 — 소수 속도 · 루프 이음매 포함. 메모리 클립은 창을 열지 않는다 */
SW_TEST_CASE( AudioVoiceTest, StreamedClipMatchesMemoryClip )
{
    vector<float32> listSample;
    for ( uint32 frame = 0; frame < 300; ++frame )
    {
        listSample.push_back( static_cast<float32>( frame ) / 300.0f );
        listSample.push_back( 1.0f - static_cast<float32>( frame ) / 300.0f );
    }
    const shared_ptr<const AudioClipData> pStreamed = Util::makeStreamedStereo( listSample );
    SW_REQUIRE_TRUE( pStreamed->isStreamed() );

    AudioVoice memoryVoice;
    memoryVoice.start( Util::makeClip( listSample, 2 ), true, 0, false, 0 );
    memoryVoice.setPlaybackRate( 0.75 );
    SW_EXPECT_FALSE( memoryVoice.getStreamWindow().isOpen() );

    AudioVoice streamVoice;
    streamVoice.start( pStreamed, true, 0, false, 0 );
    streamVoice.setPlaybackRate( 0.75 );
    SW_REQUIRE_TRUE( streamVoice.getStreamWindow().isOpen() );

    AudioBlockBuffer memoryBuffer;
    memoryBuffer.allocate( AudioChannelLayout::Stereo );
    AudioBlockBuffer streamBuffer;
    streamBuffer.allocate( AudioChannelLayout::Stereo );
    uint32 differingCount = 0;
    for ( uint32 blockIndex = 0; blockIndex < 8; ++blockIndex )
    {
        Util::mixCenter( memoryVoice, memoryBuffer );
        Util::mixCenter( streamVoice, streamBuffer );
        for ( uint32 frame = 0; frame < audio::kBlockFrameCount; ++frame )
        {
            const bool bSameLeft  = MathUtil::abs( Util::getLeft( memoryBuffer )[frame] - Util::getLeft( streamBuffer )[frame] ) <= 0.00001f;
            const bool bSameRight = MathUtil::abs( Util::getRight( memoryBuffer )[frame] - Util::getRight( streamBuffer )[frame] ) <= 0.00001f;
            differingCount += ( bSameLeft && bSameRight ) ? 0u : 1u;
        }
    }
    SW_EXPECT_EQUAL( 0u, differingCount ); // 창이 짧은 클립을 다 담으면 루프에 되감기가 없다 - 되감기 횟수는 여기서 보지 않는다(TestAudioStream)
}

/** @brief [AudioVoiceTest] 스트리밍을 중간에서 시작하면 첫 프레임을 읽은 적이 없어 루프 이음매는 보간하지 않고 끝 값을 잡는다 */
SW_TEST_CASE( AudioVoiceTest, StreamedSeamHoldsWithoutTheFirstFrame )
{
    const shared_ptr<const AudioClipData> pStreamed = Util::makeStreamedStereo( { 1.0f, 1.0f, 0.25f, 0.25f, 0.5f, 0.5f, 0.75f, 0.75f } );
    SW_REQUIRE_TRUE( pStreamed->isStreamed() );
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );

    AudioVoice voice;
    voice.start( pStreamed, true, 0, false, 0 );
    voice.setPosition( 3.5 );
    Util::mixCenter( voice, buffer, 2 );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 0.75f, pLeft[0], Util::kTolerance );                           // 첫 프레임(1.0)과 섞지 않는다
    SW_EXPECT_NEAR( 1.0f + ( 0.25f - 1.0f ) * 0.5f, pLeft[1], Util::kTolerance );  // 감긴 뒤 0.5
    Util::mixCenter( voice, buffer, 3 );                                           // 위치 1.5, 2.5, 3.5
    SW_EXPECT_NEAR( 0.75f + ( 1.0f - 0.75f ) * 0.5f, pLeft[2], Util::kTolerance ); // 이제 첫 프레임을 읽어 두어 보간한다
}

/** @brief [AudioVoiceTest] 비루프 스트리밍도 끝에서 끝나고, 스트림 끝을 넘는 읽기로 튀지 않는다 */
SW_TEST_CASE( AudioVoiceTest, StreamedVoiceEndsAtTheLastFrame )
{
    const shared_ptr<const AudioClipData> pStreamed = Util::makeStreamedStereo( { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f } );
    SW_REQUIRE_TRUE( pStreamed->isStreamed() );
    AudioBlockBuffer buffer;
    buffer.allocate( AudioChannelLayout::Stereo );
    AudioVoice voice;
    voice.start( pStreamed, false, 0, false, 0 );
    Util::mixCenter( voice, buffer );
    const float32* pLeft = Util::getLeft( buffer );
    SW_EXPECT_NEAR( 0.5f, pLeft[2], Util::kTolerance );
    SW_EXPECT_NEAR( 0.0f, pLeft[3], 0.0f );
    SW_EXPECT_TRUE( voice.isFinished() );
}
