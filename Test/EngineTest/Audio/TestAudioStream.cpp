#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioMixerDesc.h"
#include "Engine/Audio/AudioStream.h"
#include "Engine/Audio/AudioVoice.h"
#include "Engine/Resource/ResourceIO.h"

#include "EngineTest/AudioTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioStreamTest — 긴 소리 스트리밍(압축 바이트 한 벌 + 보이스마다 디코더 창)이 통째로 푼 소리와 같은 결과를 낸다
// ------------------------------------------------------------------------------

namespace
{
    struct AudioStreamTestInternal
    {
        static constexpr float32 kTwoPi = 6.283185307179586f;

        /** @brief 채널마다 다른 주파수의 사인(진폭 12000)을 16 비트 PCM 으로 쓴 WAV 바이트입니다. @p bitsPerSample 8 이면 스트리밍하지 않는 형식을 만듭니다. */
        static sw::vector<uint8> makeWav( uint32 sampleRate, uint16 channelCount, uint32 frameCount, uint16 bitsPerSample = 16 )
        {
            const uint32      bytesPerSample = bitsPerSample / 8u;
            const uint32      dataBytes      = frameCount * channelCount * bytesPerSample;
            sw::vector<uint8> bytes;
            bytes.reserve( 44u + dataBytes );
            const auto appendUint32 = [&bytes]( uint32 value )
            {
                for ( int32 shift = 0; shift < 32; shift += 8 )
                {
                    bytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
                }
            };
            const auto appendUint16 = [&bytes]( uint16 value )
            {
                bytes.push_back( static_cast<uint8>( value & 0xFFu ) );
                bytes.push_back( static_cast<uint8>( value >> 8 ) );
            };
            bytes.insert( bytes.end(), { 'R', 'I', 'F', 'F' } );
            appendUint32( 36u + dataBytes );
            bytes.insert( bytes.end(), { 'W', 'A', 'V', 'E' } );
            bytes.insert( bytes.end(), { 'f', 'm', 't', ' ' } );
            appendUint32( 16u );
            appendUint16( 1u );
            appendUint16( channelCount );
            appendUint32( sampleRate );
            appendUint32( sampleRate * channelCount * bytesPerSample );
            appendUint16( static_cast<uint16>( channelCount * bytesPerSample ) );
            appendUint16( bitsPerSample );
            bytes.insert( bytes.end(), { 'd', 'a', 't', 'a' } );
            appendUint32( dataBytes );
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                for ( uint16 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
                {
                    if ( bitsPerSample == 8 )
                    {
                        bytes.push_back( 128u );
                        continue;
                    }
                    appendUint16( static_cast<uint16>( makeSample( sampleRate, channelIndex, frameIndex ) ) );
                }
            }
            return bytes;
        }

        static int16 makeSample( uint32 sampleRate, uint16 channelIndex, uint32 frameIndex )
        {
            const float32 frequency = 220.0f + 110.0f * static_cast<float32>( channelIndex );
            const float32 phase     = kTwoPi * frequency * static_cast<float32>( frameIndex ) / static_cast<float32>( sampleRate );
            return static_cast<int16>( 12000.0f * std::sin( phase ) );
        }

        /** @brief 바이트를 스트리밍 클립으로 만듭니다(바이트는 비워집니다). */
        static sw::shared_ptr<const sw::AudioClipData> makeStreamedClip( sw::vector<uint8>& inoutBytes )
        {
            sw::shared_ptr<const sw::AudioStreamSource> pSource = sw::AudioStreamSource::tryCreate( "test.wav", inoutBytes, 0.0f );
            if ( pSource == nullptr )
                return nullptr;
            sw::shared_ptr<sw::AudioClipData> pClip = sw::make_shared<sw::AudioClipData>();
            pClip->_pStream                         = pSource;
            pClip->_frameCount                      = pSource->getInfo()._frameCount;
            pClip->_sampleRate                      = pSource->getInfo()._sampleRate;
            pClip->_channelCount                    = pSource->getInfo()._channelCount;
            return pClip;
        }

        static sw::shared_ptr<const sw::AudioClipData> makeMemoryClip( const sw::vector<uint8>& bytes )
        {
            sw::AudioPcm pcm;
            if ( sw::AudioClipDecoder::decodeWav( bytes.data(), bytes.size(), pcm ) == false )
                return nullptr;
            sw::shared_ptr<sw::AudioClipData> pClip = sw::make_shared<sw::AudioClipData>();
            if ( sw::AudioClipData::convertPcm( pcm, *pClip ) == false )
                return nullptr;
            return pClip;
        }

        /** @brief 두 버퍼의 최대 차이입니다. */
        static float32 computeMaxDifference( const sw::vector<float32>& listLeft, const sw::vector<float32>& listRight )
        {
            float32 maxDifference = 0.0f;
            for ( size_t index = 0; index < listLeft.size() && index < listRight.size(); ++index )
            {
                maxDifference = sw::MathUtil::max( maxDifference, sw::MathUtil::abs( listLeft[index] - listRight[index] ) );
            }
            return maxDifference;
        }

        /** @brief 두 평면 블록의 최대 차이입니다(모든 채널, 유효 프레임). */
        static float32 computeMaxDifference( const sw::AudioBlockView& left, const sw::AudioBlockView& right )
        {
            float32 maxDifference = 0.0f;
            for ( uint32 channelIndex = 0; channelIndex < left._channelCount; ++channelIndex )
            {
                for ( uint32 frameIndex = 0; frameIndex < left._frameCount; ++frameIndex )
                {
                    maxDifference = sw::MathUtil::max( maxDifference, sw::MathUtil::abs( left.getChannel( channelIndex )[frameIndex] - right.getChannel( channelIndex )[frameIndex] ) );
                }
            }
            return maxDifference;
        }
    };
} // namespace

/**
 * @brief [AudioStreamTest] 창은 앞으로 읽는 동안 되감지 않고 필요한 만큼만 풀며, 창 밖(뒤로 가기)은 되감아 같은 값을 읽고, 끝을 넘으면 무음이다
 */
SW_TEST_CASE( AudioStreamTest, WindowDecodesIncrementallyAndSeeksBack )
{
    constexpr uint32                            kRate       = 44100;
    constexpr uint32                            kFrameCount = 100000;
    sw::vector<uint8>                           bytes       = AudioStreamTestInternal::makeWav( kRate, 1, kFrameCount );
    const size_t                                fileBytes   = bytes.size();
    sw::shared_ptr<const sw::AudioStreamSource> pSource     = sw::AudioStreamSource::tryCreate( "a.wav", bytes, 0.0f );
    SW_ASSERT_NOT_NULL( pSource.get() );
    SW_EXPECT_TRUE( bytes.empty() ); // 만들었으므로 바이트를 가져갔다
    SW_EXPECT_EQUAL( fileBytes, pSource->getByteCount() );
    SW_EXPECT_EQUAL( kFrameCount, pSource->getInfo()._frameCount );

    sw::AudioStreamWindow window;
    SW_ASSERT_TRUE( window.open( *pSource ) );
    float32 left{ 0.0f };
    float32 right{ 0.0f };
    for ( uint32 frame = 0; frame < 5000; ++frame )
    {
        SW_ASSERT_TRUE( window.readFrame( frame, left, right ) );
        SW_EXPECT_NEAR_EQUAL( static_cast<float32>( AudioStreamTestInternal::makeSample( kRate, 0, frame ) ) / 32768.0f, left, 1e-6f );
        SW_EXPECT_NEAR_EQUAL( left, right, 1e-9f ); // 모노는 두 값이 같다
    }
    SW_EXPECT_EQUAL( 1u, window.getSeekCount() );           // 맨 처음 자리를 잡는 한 번뿐이다
    SW_EXPECT_TRUE( window.getDecodedFrameCount() < 8192 ); // 5000 프레임을 읽으려고 세 덩어리만 풀었다
    SW_EXPECT_TRUE( 5000 <= window.getDecodedFrameCount() );

    // 같은 프레임을 다시 읽어도(보간이 앞 프레임을 다시 읽는다) 되감지 않는다.
    SW_ASSERT_TRUE( window.readFrame( 4999, left, right ) );
    SW_EXPECT_EQUAL( 1u, window.getSeekCount() );

    // 창 밖으로 뒤로 가면 되감는다.
    SW_ASSERT_TRUE( window.readFrame( 10, left, right ) );
    SW_EXPECT_EQUAL( 2u, window.getSeekCount() );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( AudioStreamTestInternal::makeSample( kRate, 0, 10 ) ) / 32768.0f, left, 1e-6f );

    SW_ASSERT_TRUE( window.readFrame( kFrameCount - 1, left, right ) );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( AudioStreamTestInternal::makeSample( kRate, 0, kFrameCount - 1 ) ) / 32768.0f, left, 1e-6f );
    SW_EXPECT_FALSE( window.readFrame( kFrameCount, left, right ) ); // 끝을 넘었다
    SW_EXPECT_NEAR_EQUAL( 0.0f, left, 1e-9f );
}

/**
 * @brief [AudioStreamTest] 스트리밍 보이스는 통째로 푼 보이스와 샘플까지 같다 — 44.1 kHz 보간 · 스테레오 · 루프 이음매 포함
 */
SW_TEST_CASE( AudioStreamTest, StreamedVoiceMatchesMemoryVoiceAcrossTheLoopSeam )
{
    constexpr uint32                              kRate       = 44100;
    constexpr uint32                              kFrameCount = 44100 * 3; // 3 초
    sw::vector<uint8>                             bytes       = AudioStreamTestInternal::makeWav( kRate, 2, kFrameCount );
    const sw::shared_ptr<const sw::AudioClipData> pMemory     = AudioStreamTestInternal::makeMemoryClip( bytes );
    sw::shared_ptr<const sw::AudioClipData>       pStream     = AudioStreamTestInternal::makeStreamedClip( bytes );
    SW_ASSERT_NOT_NULL( pMemory.get() );
    SW_ASSERT_NOT_NULL( pStream.get() );
    SW_EXPECT_TRUE( pStream->isStreamed() );
    SW_EXPECT_TRUE( pStream->_listSample.empty() );
    SW_EXPECT_EQUAL( pMemory->_frameCount, pStream->_frameCount );

    sw::AudioVoice memoryVoice;
    sw::AudioVoice streamVoice;
    memoryVoice.start( pMemory, true, 0, false, 0 );
    streamVoice.start( pStream, true, 0, false, 0 );
    const float64 rate = static_cast<float64>( kRate ) / static_cast<float64>( sw::audio::kSampleRate );
    for ( sw::AudioVoice* pVoice : { &memoryVoice, &streamVoice } )
    {
        pVoice->setTarget( 0.8f, sw::AudioPanDirection::makeStereoPan( 0.25f ), sw::AudioChannelLayout::Stereo );
        pVoice->setPlaybackRate( rate );
    }

    sw::AudioBlockBuffer memoryBuffer;
    sw::AudioBlockBuffer streamBuffer;
    memoryBuffer.allocate( sw::AudioChannelLayout::Stereo );
    streamBuffer.allocate( sw::AudioChannelLayout::Stereo );
    const sw::AudioBlockView memoryBlock   = memoryBuffer.makeView( sw::audio::kBlockFrameCount );
    const sw::AudioBlockView streamBlock   = streamBuffer.makeView( sw::audio::kBlockFrameCount );
    float32                  maxDifference = 0.0f;
    const uint32             blockCount    = 3 * sw::audio::kSampleRate / sw::audio::kBlockFrameCount + 400; // 클립 3 초를 넘어 한 바퀴 돈다
    for ( uint32 blockIndex = 0; blockIndex < blockCount; ++blockIndex )
    {
        memoryBuffer.clear( sw::audio::kBlockFrameCount );
        streamBuffer.clear( sw::audio::kBlockFrameCount );
        memoryVoice.mix( memoryBlock );
        streamVoice.mix( streamBlock );
        maxDifference = sw::MathUtil::max( maxDifference, AudioStreamTestInternal::computeMaxDifference( memoryBlock, streamBlock ) );
    }
    SW_EXPECT_TRUE( maxDifference < 1e-6f );
    SW_EXPECT_TRUE( memoryVoice.isActive() && streamVoice.isActive() );
    // 처음 자리 잡기와 루프 한 바퀴 뒤 처음으로 가는 되감기, 둘뿐이다(이음매 보간은 처음 읽어 둔 프레임을 쓴다).
    SW_EXPECT_EQUAL( 2u, streamVoice.getStreamWindow().getSeekCount() );
}

/**
 * @brief [AudioStreamTest] 스트리밍하지 않는 길이는 그대로 통째로 푼다 — 기준 초 · 끄기(음수) · 지원하지 않는 형식(8 비트 WAV)
 */
SW_TEST_CASE( AudioStreamTest, CachePolicyChoosesStreamingByLengthAndFormat )
{
    const sw::string        path8Disk = test::makeTempPath( "stream_policy_8bit.wav" );
    const sw::string        path8     = test::makeTempResourceID( path8Disk );
    const sw::string        pathDisk  = test::makeTempPath( "stream_policy_3s.wav" );
    const sw::string        path      = test::makeTempResourceID( pathDisk );
    const sw::vector<uint8> bytes16   = AudioStreamTestInternal::makeWav( 48000, 1, 48000 * 3 );
    const sw::vector<uint8> bytes8    = AudioStreamTestInternal::makeWav( 48000, 1, 48000 * 3, 8 );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( pathDisk, bytes16.data(), bytes16.size() ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path8Disk, bytes8.data(), bytes8.size() ) );

    sw::AudioClipCache defaultCache; // 기준 20 초 — 3 초는 짧다
    SW_EXPECT_NEAR_EQUAL( sw::AudioClipCache::kDefaultStreamMinSeconds, defaultCache.getStreamMinSeconds(), 1e-6f );
    sw::shared_ptr<const sw::AudioClipData> pShort = defaultCache.loadClip( sw::hashed_string( path ) );
    SW_ASSERT_NOT_NULL( pShort.get() );
    SW_EXPECT_FALSE( pShort->isStreamed() );
    SW_EXPECT_EQUAL( size_t( 48000 ) * 3u, pShort->_listSample.size() );

    sw::AudioClipCache streamCache;
    streamCache.setStreamMinSeconds( 2.0f ); // 3 초는 길다
    sw::shared_ptr<const sw::AudioClipData> pLong = streamCache.loadClip( sw::hashed_string( path ) );
    SW_ASSERT_NOT_NULL( pLong.get() );
    SW_EXPECT_TRUE( pLong->isStreamed() );
    SW_EXPECT_TRUE( pLong->_listSample.empty() );
    SW_EXPECT_EQUAL( 48000u * 3u, pLong->_frameCount );
    SW_EXPECT_EQUAL( uint16( 1 ), pLong->_channelCount );
    SW_EXPECT_TRUE( pLong->_pStream->getByteCount() < pShort->_listSample.size() * sizeof( float32 ) ); // 압축 바이트가 float 보다 작다

    sw::AudioClipCache offCache;
    offCache.setStreamMinSeconds( -1.0f ); // 끈다
    SW_EXPECT_FALSE( offCache.loadClip( sw::hashed_string( path ) )->isStreamed() );

    sw::AudioClipCache alwaysCache;
    alwaysCache.setStreamMinSeconds( 0.0f );
    sw::shared_ptr<const sw::AudioClipData> pEight = alwaysCache.loadClip( sw::hashed_string( path8 ) ); // 8 비트는 스트리밍하지 않고 통째로 푼다
    SW_ASSERT_NOT_NULL( pEight.get() );
    SW_EXPECT_FALSE( pEight->isStreamed() );
    SW_EXPECT_EQUAL( size_t( 48000 ) * 3u, pEight->_listSample.size() );

    SW_EXPECT_TRUE( sw::FileUtil::removeFile( pathDisk ) );
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( path8Disk ) );
}

/**
 * @brief [AudioStreamTest] 엔진이 스트리밍 클립을 재생한 결과가 통째로 푼 클립을 재생한 결과와 같다(루프 · 피치 포함)
 */
SW_TEST_CASE( AudioStreamTest, EngineRendersStreamedClipLikeMemoryClip )
{
    sw::vector<uint8>                             bytes   = AudioStreamTestInternal::makeWav( 32000, 2, 32000 * 2 );
    const sw::shared_ptr<const sw::AudioClipData> pMemory = AudioStreamTestInternal::makeMemoryClip( bytes );
    sw::shared_ptr<const sw::AudioClipData>       pStream = AudioStreamTestInternal::makeStreamedClip( bytes );
    SW_ASSERT_NOT_NULL( pMemory.get() );
    SW_ASSERT_NOT_NULL( pStream.get() );

    sw::vector<float32> arrRender[2];
    for ( uint32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::AudioEngine engine;
        SW_ASSERT_TRUE( engine.initialize( sw::AudioMixerDesc::makeDefault(), sw::AudioChannelLayout::Stereo ) );
        engine.getClipStore().addClip( sw::hashed_string( "test/long" ), caseIndex == 0 ? pMemory : pStream );
        sw::AudioClipPlayParams params;
        params._bLoop          = true;
        params._pitchSemitones = 4.5f; // 약 1.3 배
        params._volumeDb       = -3.0f;
        (void)engine.playClip( sw::hashed_string( "test/long" ), sw::hashed_string( "sfx" ), params );
        arrRender[caseIndex] = test::AudioTestUtil::render( engine, sw::audio::kSampleRate * 5 ); // 2 초 클립을 피치 1.3 으로 두 바퀴 넘게
    }
    SW_EXPECT_TRUE( AudioStreamTestInternal::computeMaxDifference( arrRender[0], arrRender[1] ) < 1e-5f );
    SW_EXPECT_TRUE( test::AudioTestUtil::computePeak( arrRender[1], 0, 0, sw::audio::kSampleRate * 5 ) > 0.05f ); // 소리가 실제로 났다
}

/**
 * @brief [AudioStreamTest] OGG 스트리밍 — 통째로 푼 소리와 같은 길이 · 같은 값(16 비트 양자화 차이 안)이고, 중간으로 되감아도 같은 값이다
 */
SW_TEST_CASE( AudioStreamTest, OggStreamMatchesFullDecode )
{
#if defined( SW_SHIPPING ) && !defined( SW_WITH_CLIENT_CODE )
    SW_TEST_REQUIRES_FEATURE( AudioAssets ); // the shipping server pack has no audio (CookContract target_excluded_asset_kinds)
#endif
    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::ResourceIO::readBinaryResource( sw::ResourceID{ "game/empty/sounds/click.ogg" }, bytes ) );
    sw::AudioPcm pcm;
    SW_ASSERT_TRUE( sw::AudioClipDecoder::decodeOgg( bytes.data(), bytes.size(), pcm ) );
    sw::AudioClipData full;
    SW_ASSERT_TRUE( sw::AudioClipData::convertPcm( pcm, full ) );

    sw::shared_ptr<const sw::AudioStreamSource> pSource = sw::AudioStreamSource::tryCreate( "click.ogg", bytes, 0.0f );
    SW_ASSERT_NOT_NULL( pSource.get() );
    SW_EXPECT_EQUAL( full._frameCount, pSource->getInfo()._frameCount );
    SW_EXPECT_EQUAL( full._sampleRate, pSource->getInfo()._sampleRate );
    SW_EXPECT_EQUAL( full._channelCount, pSource->getInfo()._channelCount );

    sw::AudioStreamWindow window;
    SW_ASSERT_TRUE( window.open( *pSource ) );
    constexpr float32 kTolerance = 2.0f / 32768.0f;
    float32           left{ 0.0f };
    float32           right{ 0.0f };
    float32           maxDifference = 0.0f;
    for ( uint32 frame = 0; frame < full._frameCount; ++frame )
    {
        SW_ASSERT_TRUE( window.readFrame( frame, left, right ) );
        const size_t index = static_cast<size_t>( frame ) * full._channelCount;
        maxDifference      = sw::MathUtil::max( maxDifference, sw::MathUtil::abs( full._listSample[index] - left ) );
    }
    SW_EXPECT_TRUE( maxDifference <= kTolerance );

    // 중간으로 되감아도 같은 값이다(표본 정확 seek).
    const uint32 middle = full._frameCount / 2;
    SW_ASSERT_TRUE( window.readFrame( middle, left, right ) );
    SW_EXPECT_TRUE( sw::MathUtil::abs( full._listSample[static_cast<size_t>( middle ) * full._channelCount] - left ) <= kTolerance );
    SW_ASSERT_TRUE( window.readFrame( 0, left, right ) );
    SW_EXPECT_TRUE( sw::MathUtil::abs( full._listSample[0] - left ) <= kTolerance );
}
