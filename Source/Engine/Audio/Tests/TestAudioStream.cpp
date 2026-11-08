#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Audio/AudioStream.h"

using namespace sw;

namespace
{
    /** @brief 프레임 번호를 값으로 내는 디코더입니다. 채널 c 의 값은 프레임 + c × 0.25 입니다. */
    class AudioStreamRampDecoder final : public IAudioStreamDecoder
    {
    public:
        AudioStreamRampDecoder( uint32 frameCount, uint32 channelCount )
            : _frameCount{ frameCount }
            , _channelCount{ channelCount }
            , _position{ 0 }
        {
        }

        uint32 readFrames( float32* pOut, uint32 frameCount ) override
        {
            const uint32 count = MathUtil::min( frameCount, _frameCount - _position );
            for ( uint32 frameIndex = 0; frameIndex < count; ++frameIndex )
            {
                for ( uint32 channelIndex = 0; channelIndex < _channelCount; ++channelIndex )
                {
                    pOut[frameIndex * _channelCount + channelIndex] = static_cast<float32>( _position + frameIndex ) + static_cast<float32>( channelIndex ) * 0.25f;
                }
            }
            _position += count;
            return count;
        }

        bool seekToFrame( uint32 frame ) override
        {
            if ( _frameCount < frame )
                return false;
            _position = frame;
            return true;
        }

    private:
        uint32 _frameCount;
        uint32 _channelCount;
        uint32 _position;
    };

    struct AudioStreamTestUtil
    {
        static constexpr uint16 kTagPCM   = 1;
        static constexpr uint16 kTagFloat = 3;
        static constexpr uint16 kTagAdpcm = 2;
        static constexpr uint16 kTagExt   = 0xFFFE;

        static void writeTag( ByteWriter& writer, const utf8* pTag ) { writer.writeBytes( reinterpret_cast<const uint8*>( pTag ), 4 ); }

        /** @brief fmt(16 바이트) · data 청크만 있는 WAV 입니다. */
        static vector<uint8> makeWAV( uint16 tag, uint16 channelCount, uint32 sampleRate, uint16 bitsPerSample, const vector<uint8>& listData )
        {
            vector<uint8> listByte;
            ByteWriter    writer{ listByte };
            writeTag( writer, "RIFF" );
            writer.writeUint32( static_cast<uint32>( 36u + listData.size() ) );
            writeTag( writer, "WAVE" );
            writeTag( writer, "fmt " );
            writer.writeUint32( 16 );
            writer.writeUint16( tag );
            writer.writeUint16( channelCount );
            writer.writeUint32( sampleRate );
            writer.writeUint32( sampleRate * channelCount * ( bitsPerSample / 8u ) );
            writer.writeUint16( static_cast<uint16>( channelCount * ( bitsPerSample / 8u ) ) );
            writer.writeUint16( bitsPerSample );
            writeTag( writer, "data" );
            writer.writeUint32( static_cast<uint32>( listData.size() ) );
            writer.writeBytes( listData.data(), listData.size() );
            return listByte;
        }

        /** @brief 확장형 fmt(40 바이트, 하위 형식 PCM) 의 16 비트 WAV 입니다. 채널 마스크를 적습니다. */
        static vector<uint8> makeExtensibleWAV( uint16 channelCount, uint32 channelMask, const vector<uint8>& listData )
        {
            static constexpr uint8 kArrSubFormatTail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
            vector<uint8>          listByte;
            ByteWriter             writer{ listByte };
            writeTag( writer, "RIFF" );
            writer.writeUint32( static_cast<uint32>( 60u + listData.size() ) );
            writeTag( writer, "WAVE" );
            writeTag( writer, "fmt " );
            writer.writeUint32( 40 );
            writer.writeUint16( kTagExt );
            writer.writeUint16( channelCount );
            writer.writeUint32( 48000 );
            writer.writeUint32( 48000u * channelCount * 2u );
            writer.writeUint16( static_cast<uint16>( channelCount * 2u ) );
            writer.writeUint16( 16 );
            writer.writeUint16( 22 );
            writer.writeUint16( 16 );
            writer.writeUint32( channelMask );
            writer.writeUint32( kTagPCM );
            writer.writeBytes( kArrSubFormatTail, sizeof( kArrSubFormatTail ) );
            writeTag( writer, "data" );
            writer.writeUint32( static_cast<uint32>( listData.size() ) );
            writer.writeBytes( listData.data(), listData.size() );
            return listByte;
        }

        static vector<uint8> makePCM16Data( const vector<int16>& listSample )
        {
            vector<uint8> listByte;
            ByteWriter    writer{ listByte };
            for ( const int16 sample : listSample )
            {
                writer.writeUint16( static_cast<uint16>( sample ) );
            }
            return listByte;
        }

        static vector<uint8> makeFloatData( const vector<float32>& listSample )
        {
            vector<uint8> listByte;
            ByteWriter    writer{ listByte };
            for ( const float32 sample : listSample )
            {
                writer.writeFloat32( sample );
            }
            return listByte;
        }

        /** @brief 무음 16 비트 모노 @p frameCount 프레임 WAV 입니다. */
        static vector<uint8> makeSilentMonoWAV( uint32 frameCount, uint32 sampleRate ) { return makeWAV( kTagPCM, 1, sampleRate, 16, vector<uint8>( static_cast<size_t>( frameCount ) * 2u, 0 ) ); }

        static void patchUint32( vector<uint8>& inoutBytes, size_t offset, uint32 value ) { Memory::copy( inoutBytes.data() + offset, &value, sizeof( value ) ); }
    };
    using Util = AudioStreamTestUtil;
} // namespace

/** @brief [AudioStreamTest] WAV 는 신뢰 경계다 — 어느 길이로 잘려도 소스를 만들지 않고 호출자의 바이트를 남긴다 */
SW_TEST_CASE( AudioStreamTest, WAVRejectsEveryTruncatedLength )
{
    const vector<uint8> listFull = Util::makeSilentMonoWAV( 8, 1000 );
    for ( size_t length = 0; length < listFull.size(); ++length )
    {
        vector<uint8> listPrefix( listFull.begin(), listFull.begin() + static_cast<ptrdiff_t>( length ) );
        SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listPrefix, 0.0f ) );
        SW_EXPECT_EQUAL( length, listPrefix.size() );
    }
    vector<uint8> listWhole = listFull;
    SW_EXPECT_NOT_NULL( AudioStreamSource::tryCreate( "a.wav", listWhole, 0.0f ) );
}

/** @brief [AudioStreamTest] 청크 길이가 파일 끝을 넘으면(data · fmt) 믿지 않는다 */
SW_TEST_CASE( AudioStreamTest, ChunkSizePastTheEndIsRejected )
{
    static constexpr size_t kFmtSizeOffset  = 16;
    static constexpr size_t kDataSizeOffset = 40;
    const vector<uint8>     listFull        = Util::makeSilentMonoWAV( 8, 1000 );
    vector<uint8>           listData        = listFull;
    Util::patchUint32( listData, kDataSizeOffset, 0xFFFFFFFFu );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listData, 0.0f ) );
    vector<uint8> listFmt = listFull;
    Util::patchUint32( listFmt, kFmtSizeOffset, 0xFFFFFFFFu );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listFmt, 0.0f ) );
}

/** @brief [AudioStreamTest] 스트리밍하지 않는 형식과 틀린 머리말은 만들지 않는다(호출자가 통째로 푼다) */
SW_TEST_CASE( AudioStreamTest, UnsupportedFormatsAreRejected )
{
    const vector<uint8> listData( 72, 0 );
    vector<uint8>       listAdpcm = Util::makeWAV( Util::kTagAdpcm, 1, 1000, 16, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listAdpcm, 0.0f ) );
    vector<uint8> listPCM8 = Util::makeWAV( Util::kTagPCM, 1, 1000, 8, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listPCM8, 0.0f ) );
    vector<uint8> listPCM24 = Util::makeWAV( Util::kTagPCM, 1, 1000, 24, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listPCM24, 0.0f ) );
    vector<uint8> listFloat16 = Util::makeWAV( Util::kTagFloat, 1, 1000, 16, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listFloat16, 0.0f ) );
    vector<uint8> listNoChannel = Util::makeWAV( Util::kTagPCM, 0, 1000, 16, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listNoChannel, 0.0f ) );
    vector<uint8> listNineChannel = Util::makeWAV( Util::kTagPCM, 9, 1000, 16, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listNineChannel, 0.0f ) );
    vector<uint8> listNoRate = Util::makeWAV( Util::kTagPCM, 1, 0, 16, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listNoRate, 0.0f ) );

    vector<uint8> listWave = Util::makeSilentMonoWAV( 8, 1000 );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.mp3", listWave, 0.0f ) );
    listWave[0] = 'X';
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listWave, 0.0f ) );
}

/** @brief [AudioStreamTest] 한 프레임도 못 되는 data 청크는 길이 0 의 소리라 만들지 않는다 */
SW_TEST_CASE( AudioStreamTest, DataShorterThanOneFrameIsRejected )
{
    vector<uint8> listByte = Util::makeWAV( Util::kTagPCM, 2, 1000, 16, vector<uint8>( 3, 0 ) );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f ) );
    SW_EXPECT_FALSE( listByte.empty() );
}

/** @brief [AudioStreamTest] 깨진 OGG 는 호출자의 바이트를 남기고 만들지 않는다 */
SW_TEST_CASE( AudioStreamTest, BrokenOggIsRejected )
{
    vector<uint8> listEmpty;
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.ogg", listEmpty, 0.0f ) );
    vector<uint8> listJunk = { 'O', 'g', 'g', 'S', 0, 0, 0, 0, 1, 2, 3 };
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.ogg", listJunk, 0.0f ) );
    SW_EXPECT_EQUAL( 11u, static_cast<uint32>( listJunk.size() ) );
}

/** @brief [AudioStreamTest] 만들면 바이트를 가져가고 형식을 머리말대로 적는다. 스테레오는 재생도 2 채널이고 다운믹스가 없다 */
SW_TEST_CASE( AudioStreamTest, CreateTakesTheBytesAndReadsTheFormat )
{
    const vector<uint8> listData = Util::makePCM16Data( { 1, 2, 3, 4, 5, 6 } );
    vector<uint8>       listByte = Util::makeWAV( Util::kTagPCM, 2, 44100, 16, listData );
    const size_t        byteSize = listByte.size();

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "Music.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    SW_EXPECT_TRUE( listByte.empty() );
    SW_EXPECT_EQUAL( byteSize, pSource->getByteCount() );
    SW_EXPECT_EQUAL( 44100u, pSource->getInfo()._sampleRate );
    SW_EXPECT_EQUAL( 3u, pSource->getInfo()._frameCount );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( pSource->getInfo()._channelCount ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( pSource->getPlaybackChannelCount() ) );
    SW_EXPECT_EQUAL( 0u, pSource->getDownmix()._channelCount );
}

/** @brief [AudioStreamTest] 길이 기준은 경계값을 받는다 — 정확히 그 길이는 만들고, 한 프레임 모자라면 만들지 않는다. 0 은 길이를 보지 않는다 */
SW_TEST_CASE( AudioStreamTest, MinSecondsBoundary )
{
    vector<uint8> listExact = Util::makeSilentMonoWAV( 500, 1000 );
    SW_EXPECT_NOT_NULL( AudioStreamSource::tryCreate( "a.wav", listExact, 0.5f ) );
    vector<uint8> listShort = Util::makeSilentMonoWAV( 499, 1000 );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listShort, 0.5f ) );
    SW_EXPECT_FALSE( listShort.empty() );
    SW_EXPECT_NOT_NULL( AudioStreamSource::tryCreate( "a.wav", listShort, 0.0f ) );
}

/** @brief [AudioStreamTest] 3 채널 이상은 재생이 2 채널이고, 창이 프레임을 읽을 때 행렬로 줄인다(FL FR FC, 마스크 없음은 아래 비트부터) */
SW_TEST_CASE( AudioStreamTest, ThreeChannelsAreDownmixedWhenRead )
{
    const vector<uint8> listData = Util::makeFloatData( { 1.0f, 0.5f, 0.25f } );
    vector<uint8>       listByte = Util::makeWAV( Util::kTagFloat, 3, 1000, 32, listData );

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( pSource->getInfo()._channelCount ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( pSource->getPlaybackChannelCount() ) );
    SW_EXPECT_EQUAL( 3u, pSource->getDownmix()._channelCount );

    AudioStreamWindow window;
    SW_REQUIRE_TRUE( window.open( *pSource ) );
    float32 left{ 0.0f };
    float32 right{ 0.0f };
    SW_REQUIRE_TRUE( window.readFrame( 0, left, right ) );
    SW_EXPECT_NEAR( 1.0f + 0.25f * MathUtil::kInvSqrt2, left, 0.0001f );
    SW_EXPECT_NEAR( 0.5f + 0.25f * MathUtil::kInvSqrt2, right, 0.0001f );
}

/** @brief [AudioStreamTest] 채널 마스크의 비트가 채널 수보다 적으면 배치를 몰라 만들지 않는다(오류 로그). 비트가 맞으면 그 배치로 줄인다 */
SW_TEST_CASE( AudioStreamTest, ChannelMaskMustPlaceEveryChannel )
{
    SW_EXPECT_LOG( "Audio.DownmixMaskShort" );
    const vector<uint8> listData  = Util::makePCM16Data( { 16384, 0, 0 } );
    vector<uint8>       listShort = Util::makeExtensibleWAV( 3, 0x3u, listData );
    SW_EXPECT_NULL( AudioStreamSource::tryCreate( "a.wav", listShort, 0.0f ) );
    SW_EXPECT_FALSE( listShort.empty() );

    // BC(0x100) 는 양쪽 0.5 다 — 마스크 값이 실제로 쓰였는지 FC(마스크 없을 때의 셋째)와 가른다.
    const vector<uint8> listBackCenter = Util::makePCM16Data( { 0, 0, 16384 } );
    vector<uint8>       listByte       = Util::makeExtensibleWAV( 3, 0x103u, listBackCenter );

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    AudioStreamWindow window;
    SW_REQUIRE_TRUE( window.open( *pSource ) );
    float32 left{ 0.0f };
    float32 right{ 0.0f };
    SW_REQUIRE_TRUE( window.readFrame( 0, left, right ) );
    SW_EXPECT_NEAR( 0.25f, left, 0.0001f );
    SW_EXPECT_NEAR( 0.25f, right, 0.0001f );
}

/** @brief [AudioStreamTest] 16 비트 정수는 32768 로 나눈다(-32768 이 정확히 -1). 끝에서 요청보다 적게 주고, 더 없으면 0 이다 */
SW_TEST_CASE( AudioStreamTest, PCM16DecoderScalesAndStopsAtTheEnd )
{
    const vector<uint8> listData = Util::makePCM16Data( { -32768, 16384, 0 } );
    vector<uint8>       listByte = Util::makeWAV( Util::kTagPCM, 1, 1000, 16, listData );

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    unique_ptr<IAudioStreamDecoder> pDecoder = pSource->openDecoder();
    SW_REQUIRE_NOT_NULL( pDecoder );

    float32 arrOut[8]{};
    SW_REQUIRE_EQUAL( 2u, pDecoder->readFrames( arrOut, 2 ) );
    SW_EXPECT_NEAR( -1.0f, arrOut[0], 0.0f );
    SW_EXPECT_NEAR( 0.5f, arrOut[1], 0.0f );
    SW_EXPECT_EQUAL( 1u, pDecoder->readFrames( arrOut, 8 ) );
    SW_EXPECT_EQUAL( 0u, pDecoder->readFrames( arrOut, 8 ) );
}

/** @brief [AudioStreamTest] 디코더 되감기는 끝 자리까지 되고(그 뒤 읽기는 0), 끝 너머는 거절하고 위치를 그대로 둔다 */
SW_TEST_CASE( AudioStreamTest, DecoderSeekBoundary )
{
    const vector<uint8> listData = Util::makeFloatData( { 0.1f, 0.2f, 0.3f } );
    vector<uint8>       listByte = Util::makeWAV( Util::kTagFloat, 1, 1000, 32, listData );

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    unique_ptr<IAudioStreamDecoder> pDecoder = pSource->openDecoder();
    SW_REQUIRE_NOT_NULL( pDecoder );

    float32 arrOut[4]{};
    SW_EXPECT_TRUE( pDecoder->seekToFrame( 3 ) );
    SW_EXPECT_EQUAL( 0u, pDecoder->readFrames( arrOut, 4 ) );
    SW_EXPECT_TRUE( pDecoder->seekToFrame( 1 ) );
    SW_EXPECT_FALSE( pDecoder->seekToFrame( 4 ) );
    SW_REQUIRE_EQUAL( 2u, pDecoder->readFrames( arrOut, 4 ) );
    SW_EXPECT_NEAR( 0.2f, arrOut[0], 0.00001f );
    SW_EXPECT_NEAR( 0.3f, arrOut[1], 0.00001f );
}

/** @brief [AudioStreamTest] data 앞의 홀수 길이 청크는 채움 바이트까지 건너뛴다 — 샘플이 정렬되지 않은 자리여도 값이 같다 */
SW_TEST_CASE( AudioStreamTest, OddChunkBeforeDataIsPaddedOver )
{
    vector<uint8> listByte;
    ByteWriter    writer{ listByte };
    Util::writeTag( writer, "RIFF" );
    writer.writeUint32( 0 ); // 파서는 RIFF 길이를 보지 않는다
    Util::writeTag( writer, "WAVE" );
    Util::writeTag( writer, "LIST" );
    writer.writeUint32( 3 );
    writer.writeUint8( 0xAA );
    writer.writeUint8( 0xBB );
    writer.writeUint8( 0xCC );
    writer.writeUint8( 0 ); // 채움
    Util::writeTag( writer, "fmt " );
    writer.writeUint32( 16 );
    writer.writeUint16( Util::kTagFloat );
    writer.writeUint16( 1 );
    writer.writeUint32( 1000 );
    writer.writeUint32( 4000 );
    writer.writeUint16( 4 );
    writer.writeUint16( 32 );
    Util::writeTag( writer, "JUNK" );
    writer.writeUint32( 1 );
    writer.writeUint8( 0xDD );
    writer.writeUint8( 0 ); // 채움
    Util::writeTag( writer, "data" );
    writer.writeUint32( 4 );
    writer.writeFloat32( 0.75f );

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    unique_ptr<IAudioStreamDecoder> pDecoder = pSource->openDecoder();
    SW_REQUIRE_NOT_NULL( pDecoder );
    float32 sample{ 0.0f };
    SW_REQUIRE_EQUAL( 1u, pDecoder->readFrames( &sample, 1 ) );
    SW_EXPECT_NEAR( 0.75f, sample, 0.0f );
}

/** @brief [AudioStreamTest] 디코더는 소스보다 오래 살아도 된다 — 압축 바이트를 함께 쥔다 */
SW_TEST_CASE( AudioStreamTest, DecoderKeepsTheBytesAliveAfterTheSource )
{
    const vector<uint8> listData = Util::makeFloatData( { 0.25f, -0.25f } );
    vector<uint8>       listByte = Util::makeWAV( Util::kTagFloat, 1, 1000, 32, listData );

    shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    unique_ptr<IAudioStreamDecoder> pDecoder = pSource->openDecoder();
    SW_REQUIRE_NOT_NULL( pDecoder );
    pSource.reset();

    float32 arrOut[2]{};
    SW_REQUIRE_EQUAL( 2u, pDecoder->readFrames( arrOut, 2 ) );
    SW_EXPECT_NEAR( 0.25f, arrOut[0], 0.0f );
    SW_EXPECT_NEAR( -0.25f, arrOut[1], 0.0f );
}

/** @brief [AudioStreamTest] 창 바로 뒤를 읽으면 되감지 않고 이어 푼다. 남긴 두 프레임은 다시 읽을 수 있고, 그보다 앞은 되감는다 */
SW_TEST_CASE( AudioStreamTest, WindowAppendsRetainsAndSeeks )
{
    AudioStreamWindow window;
    SW_REQUIRE_TRUE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 100u, 1u ), 1, 4 ) );
    float32 left{ 0.0f };
    float32 right{ 0.0f };

    SW_REQUIRE_TRUE( window.readFrame( 0, left, right ) ); // 첫 읽기는 되감기 하나로 창을 연다
    SW_EXPECT_NEAR( 0.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 1u, window.getSeekCount() );
    SW_EXPECT_EQUAL( 4ull, window.getDecodedFrameCount() );

    SW_REQUIRE_TRUE( window.readFrame( 3, left, right ) ); // 창 안: 더 풀지 않는다
    SW_EXPECT_NEAR( 3.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 4ull, window.getDecodedFrameCount() );

    SW_REQUIRE_TRUE( window.readFrame( 4, left, right ) ); // 창 바로 뒤: 이어 푼다
    SW_EXPECT_NEAR( 4.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 1u, window.getSeekCount() );
    SW_EXPECT_EQUAL( 8ull, window.getDecodedFrameCount() );

    SW_REQUIRE_TRUE( window.readFrame( 2, left, right ) ); // 남긴 `kRetainFrames` 프레임: 되감지 않는다
    SW_EXPECT_NEAR( 2.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 1u, window.getSeekCount() );

    SW_REQUIRE_TRUE( window.readFrame( 1, left, right ) ); // 그 앞: 되감는다
    SW_EXPECT_NEAR( 1.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 2u, window.getSeekCount() );

    SW_REQUIRE_TRUE( window.readFrame( 50, left, right ) ); // 창에서 먼 뒤: 되감는다
    SW_EXPECT_NEAR( 50.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 3u, window.getSeekCount() );
}

/** @brief [AudioStreamTest] 끝을 넘은 읽기는 false 와 0 이고, 창은 남아 마지막 프레임을 다시 읽을 수 있다(보간이 그 자리를 다시 읽는다) */
SW_TEST_CASE( AudioStreamTest, WindowPastTheEndKeepsTheLastFrames )
{
    AudioStreamWindow window;
    SW_REQUIRE_TRUE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 6u, 1u ), 1, 4 ) );
    float32 left{ 0.0f };
    float32 right{ 0.0f };
    for ( uint32 frame = 0; frame < 6; ++frame )
    {
        SW_REQUIRE_TRUE( window.readFrame( frame, left, right ) );
        SW_EXPECT_NEAR( static_cast<float32>( frame ), left, 0.0f );
    }
    left  = 9.0f;
    right = 9.0f;
    SW_EXPECT_FALSE( window.readFrame( 6, left, right ) ); // 바로 뒤를 풀려다 끝에 닿는다
    SW_EXPECT_NEAR( 0.0f, left, 0.0f );
    SW_EXPECT_NEAR( 0.0f, right, 0.0f );
    SW_EXPECT_FALSE( window.readFrame( 6, left, right ) ); // 두 번째도 같다
    SW_EXPECT_TRUE( window.readFrame( 5, left, right ) );
    SW_EXPECT_NEAR( 5.0f, left, 0.0f );

    left = 9.0f;
    SW_EXPECT_FALSE( window.readFrame( 7, left, right ) ); // 끝 너머: 되감기 실패
    SW_EXPECT_NEAR( 0.0f, left, 0.0f );
    SW_EXPECT_TRUE( window.readFrame( 5, left, right ) );
    SW_EXPECT_NEAR( 5.0f, left, 0.0f );
    SW_EXPECT_TRUE( window.isOpen() );
}

/** @brief [AudioStreamTest] 정확히 끝 자리로 되감으면 되감기는 되지만 풀 것이 없어 false 다 */
SW_TEST_CASE( AudioStreamTest, WindowSeekExactlyToTheEndReadsNothing )
{
    AudioStreamWindow window;
    SW_REQUIRE_TRUE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 6u, 1u ), 1, 4 ) );
    float32 left{ 9.0f };
    float32 right{ 9.0f };
    SW_EXPECT_FALSE( window.readFrame( 6, left, right ) );
    SW_EXPECT_NEAR( 0.0f, left, 0.0f );
    SW_EXPECT_EQUAL( 1u, window.getSeekCount() );
    SW_EXPECT_EQUAL( 0ull, window.getDecodedFrameCount() );
    SW_EXPECT_TRUE( window.readFrame( 0, left, right ) ); // 빈 창에서 다시 연다
}

/** @brief [AudioStreamTest] 모노는 두 값이 같고, 스테레오는 교차된 두 값이다 */
SW_TEST_CASE( AudioStreamTest, WindowChannelLayout )
{
    float32 left{ 0.0f };
    float32 right{ 0.0f };

    AudioStreamWindow mono;
    SW_REQUIRE_TRUE( mono.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 1u ), 1, 4 ) );
    SW_REQUIRE_TRUE( mono.readFrame( 2, left, right ) );
    SW_EXPECT_NEAR( 2.0f, left, 0.0f );
    SW_EXPECT_NEAR( 2.0f, right, 0.0f );

    AudioStreamWindow stereo;
    SW_REQUIRE_TRUE( stereo.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 2u ), 2, 4 ) );
    SW_REQUIRE_TRUE( stereo.readFrame( 6, left, right ) ); // 되감기 뒤 첫 프레임 — 프레임 간격을 틀리면 다른 값이 나온다
    SW_REQUIRE_TRUE( stereo.readFrame( 7, left, right ) );
    SW_EXPECT_NEAR( 7.0f, left, 0.0f );
    SW_EXPECT_NEAR( 7.25f, right, 0.0f );
}

/** @brief [AudioStreamTest] 디코더로 여는 길은 틀린 인자를 거절하고 닫힌 채로 있다. 닫힌 창의 읽기는 false 와 0 이다 */
SW_TEST_CASE( AudioStreamTest, OpenDecoderRejectsBadArguments )
{
    AudioStreamWindow window;
    SW_EXPECT_FALSE( window.isOpen() );
    float32 left{ 9.0f };
    float32 right{ 9.0f };
    SW_EXPECT_FALSE( window.readFrame( 0, left, right ) );
    SW_EXPECT_NEAR( 0.0f, left, 0.0f );
    SW_EXPECT_NEAR( 0.0f, right, 0.0f );

    SW_EXPECT_FALSE( window.openDecoder( unique_ptr<IAudioStreamDecoder>{}, 1, 4 ) );
    SW_EXPECT_FALSE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 1u ), 0, 4 ) );
    SW_EXPECT_FALSE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 3u ), 3, 4 ) );
    SW_EXPECT_FALSE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 1u ), 1, 0 ) );
    SW_EXPECT_FALSE( window.isOpen() );

    // 실패한 열기는 앞서 열린 창도 닫는다(반쯤 열린 창을 남기지 않는다).
    SW_REQUIRE_TRUE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 1u ), 1, 4 ) );
    SW_EXPECT_FALSE( window.openDecoder( unique_ptr<IAudioStreamDecoder>{}, 1, 4 ) );
    SW_EXPECT_FALSE( window.isOpen() );
}

/** @brief [AudioStreamTest] 닫으면 읽지 못하고, 다시 열면 앞 소스의 다운믹스와 창을 물려받지 않는다 */
SW_TEST_CASE( AudioStreamTest, ReopenDropsTheDownmixAndTheWindow )
{
    const vector<uint8> listData = Util::makeFloatData( { 1.0f, 0.5f, 0.25f } );
    vector<uint8>       listByte = Util::makeWAV( Util::kTagFloat, 3, 1000, 32, listData );

    const shared_ptr<const AudioStreamSource> pSource = AudioStreamSource::tryCreate( "a.wav", listByte, 0.0f );
    SW_REQUIRE_NOT_NULL( pSource );
    AudioStreamWindow window;
    SW_REQUIRE_TRUE( window.open( *pSource ) );
    float32 left{ 0.0f };
    float32 right{ 0.0f };
    SW_REQUIRE_TRUE( window.readFrame( 0, left, right ) );

    window.close();
    SW_EXPECT_FALSE( window.isOpen() );
    SW_EXPECT_FALSE( window.readFrame( 0, left, right ) );

    SW_REQUIRE_TRUE( window.openDecoder( sw::make_unique<AudioStreamRampDecoder>( 8u, 2u ), 2, 4 ) );
    SW_REQUIRE_TRUE( window.readFrame( 1, left, right ) );
    SW_EXPECT_NEAR( 1.0f, left, 0.0f );
    SW_EXPECT_NEAR( 1.25f, right, 0.0f );
}
