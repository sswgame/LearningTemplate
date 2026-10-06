#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/HostTargetTestUtil.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioSystemTest — 오디오 시스템 수명주기 및 재생 기능 검증
// ------------------------------------------------------------------------------

namespace
{
    /**
     * @brief PCM 16비트 모노 44.1kHz WAV 파일을 하나 만듭니다.
     * @details 두 케이스가 같이 쓴다.
     */
    [[nodiscard]] bool writeTestWav( const sw::string& path, uint32 dataByteCount )
    {
        sw::vector<uint8> bytes;
        bytes.reserve( 44 + dataByteCount );

        auto appendUint32 = [&bytes]( uint32 value )
        {
            bytes.push_back( static_cast<uint8>( value & 0xFFu ) );
            bytes.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
            bytes.push_back( static_cast<uint8>( ( value >> 16 ) & 0xFFu ) );
            bytes.push_back( static_cast<uint8>( ( value >> 24 ) & 0xFFu ) );
        };
        auto appendUint16 = [&bytes]( uint16 value )
        {
            bytes.push_back( static_cast<uint8>( value & 0xFFu ) );
            bytes.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
        };

        bytes.insert( bytes.end(), { 'R', 'I', 'F', 'F' } );
        appendUint32( 36u + dataByteCount );
        bytes.insert( bytes.end(), { 'W', 'A', 'V', 'E' } );

        bytes.insert( bytes.end(), { 'f', 'm', 't', ' ' } );
        appendUint32( 16u );
        appendUint16( 1u );     // WAVE_FORMAT_PCM
        appendUint16( 1u );     // 모노
        appendUint32( 44100u ); // 샘플레이트
        appendUint32( 88200u ); // 바이트레이트
        appendUint16( 2u );     // 블록 정렬
        appendUint16( 16u );    // 샘플당 비트

        bytes.insert( bytes.end(), { 'd', 'a', 't', 'a' } );
        appendUint32( dataByteCount );
        for ( uint32 byteIndex = 0; byteIndex < dataByteCount; ++byteIndex )
            bytes.push_back( static_cast<uint8>( byteIndex ) );

        return sw::FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    /**
     * @brief 부동소수(IEEE float) 또는 확장형(WAVE_FORMAT_EXTENSIBLE) WAV 를 만듭니다. DAW 가 흔히 내보내는 형식입니다.
     * @param subFormatTag 확장형의 하위 형식(1 PCM · 3 float). 0 이면 확장형이 아닌 `formatTag` 그대로입니다.
     */
    [[nodiscard]] bool writeFormattedWav( const sw::string& path, uint16 formatTag, uint16 channelCount, uint16 bitsPerSample, uint16 subFormatTag )
    {
        const bool        bExtensible = subFormatTag != 0;
        const uint32      fmtBytes    = bExtensible ? 40u : 16u;
        const uint16      blockAlign  = static_cast<uint16>( channelCount * bitsPerSample / 8 );
        const uint32      dataBytes   = static_cast<uint32>( blockAlign ) * 64u;
        sw::vector<uint8> bytes;
        auto              appendUint32 = [&bytes]( uint32 value )
        {
            for ( uint32 shift = 0; shift < 32; shift += 8 )
                bytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
        };
        auto appendUint16 = [&bytes]( uint16 value )
        {
            bytes.push_back( static_cast<uint8>( value & 0xFFu ) );
            bytes.push_back( static_cast<uint8>( ( value >> 8 ) & 0xFFu ) );
        };
        bytes.insert( bytes.end(), { 'R', 'I', 'F', 'F' } );
        appendUint32( 4u + 8u + fmtBytes + 8u + dataBytes );
        bytes.insert( bytes.end(), { 'W', 'A', 'V', 'E' } );
        bytes.insert( bytes.end(), { 'f', 'm', 't', ' ' } );
        appendUint32( fmtBytes );
        appendUint16( bExtensible ? uint16{ 0xFFFE } : formatTag );
        appendUint16( channelCount );
        appendUint32( 48000u );
        appendUint32( 48000u * blockAlign );
        appendUint16( blockAlign );
        appendUint16( bitsPerSample );
        if ( bExtensible )
        {
            appendUint16( 22u );                         // cbSize
            appendUint16( bitsPerSample );               // wValidBitsPerSample
            appendUint32( ( 1u << channelCount ) - 1u ); // dwChannelMask
            appendUint32( subFormatTag );                // SubFormat.Data1
            const uint8 arrTail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
            bytes.insert( bytes.end(), arrTail, arrTail + sizeof( arrTail ) );
        }
        bytes.insert( bytes.end(), { 'd', 'a', 't', 'a' } );
        appendUint32( dataBytes );
        bytes.insert( bytes.end(), dataBytes, uint8{ 0 } );
        return sw::FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }
} // namespace

SW_TEST_CASE( AudioSystemTest, LifecycleAndState )
{
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    const bool                       initOk       = pAudioSystem->initialize();
    SW_EXPECT_TRUE( initOk );
    // 중복 initialize 호출 안전성
    SW_EXPECT_TRUE( pAudioSystem->initialize() );

    // 빈 delta time update 호출
    pAudioSystem->update( 0.016f );

    pAudioSystem->shutdown();
}

SW_TEST_CASE( AudioSystemTest, PlaybackHandling )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing non-existent audio file handling" );
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_EXPECT_TRUE( pAudioSystem->initialize() );

    // 빈 경로 또는 존재하지 않는 파일 재생 실패 처리 검증
    SW_EXPECT_FALSE( pAudioSystem->play( "" ) );
    SW_EXPECT_FALSE( pAudioSystem->play( "NonExistentSoundFile.wav" ) );

    SW_EXPECT_FALSE( pAudioSystem->playMusic( "" ) );
    SW_EXPECT_FALSE( pAudioSystem->playMusic( "NonExistentMusicFile.mp3" ) );

    // stopMusic 호출 안전성
    pAudioSystem->stopMusic();

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    pAudioSystem->shutdown();
}

SW_TEST_CASE( AudioSystemTest, WavParsingAndMalformedData )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing malformed/truncated WAV file parsing and recovery" );
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_EXPECT_TRUE( pAudioSystem->initialize() );

    const sw::string validWavPath  = test::makeTempPath( "test_valid.wav" );
    const sw::string malformedPath = test::makeTempPath( "test_malformed.wav" );
    const sw::string truncatedPath = test::makeTempPath( "test_truncated.wav" );

    // 1) 유효한 PCM 16-bit Mono 44.1kHz WAV 생성
    SW_EXPECT_TRUE( writeTestWav( validWavPath, 64 ) );

    // 2) 손상된(Malformed) WAV 파일 생성 — 유효하지 않은 Magic ID
    {
        sw::vector<uint8> badBytes = { 'B', 'A', 'D', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' };
        badBytes.resize( 50, 0 );
        SW_EXPECT_TRUE( sw::FileUtil::writeFile( malformedPath, badBytes.data(), badBytes.size() ) );
    }

    // 3) 잘린(Truncated) WAV 파일 생성
    {
        sw::vector<uint8> truncBytes = { 'R', 'I', 'F', 'F', 30, 0, 0, 0, 'W', 'A', 'V', 'E' };
        SW_EXPECT_TRUE( sw::FileUtil::writeFile( truncatedPath, truncBytes.data(), truncBytes.size() ) );
    }

    // 로드 검증 (비동기로 제출되므로 파일이 존재하면 true 반환)
    SW_EXPECT_TRUE( pAudioSystem->play( validWavPath ) );
    SW_EXPECT_TRUE( pAudioSystem->play( malformedPath ) );
    SW_EXPECT_TRUE( pAudioSystem->play( truncatedPath ) );

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    // 정리
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( validWavPath ) );
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( malformedPath ) );
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( truncatedPath ) );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 볼륨 설정과 클램프를 IAudioSystem 으로 읽어 검증
 * @details getter 가 구현(XAudio2System)에만 있으면 **IAudioSystem 을 든 누구도 읽을 수 없어** "설정한 값이 실제로
 *          들어갔는지" 를 확인하지 못한다. 그래서 인터페이스로 읽는다.
 */
SW_TEST_CASE( AudioSystemTest, VolumeControls )
{
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_EXPECT_TRUE( pAudioSystem->initialize() );
    SW_EXPECT_TRUE( pAudioSystem->isInitialized() );

    // 기본값은 전부 1.0 이다.
    SW_EXPECT_NEAR_EQUAL( 1.0f, pAudioSystem->getMasterVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pAudioSystem->getMusicVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pAudioSystem->getSfxVolume(), 1e-4f );

    pAudioSystem->setMasterVolume( 0.75f );
    pAudioSystem->setMusicVolume( 0.5f );
    pAudioSystem->setSfxVolume( 0.25f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, pAudioSystem->getMasterVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pAudioSystem->getMusicVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pAudioSystem->getSfxVolume(), 1e-4f );

    // 0~1 밖의 값은 잘린다.
    pAudioSystem->setMasterVolume( 1.5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pAudioSystem->getMasterVolume(), 1e-4f );
    pAudioSystem->setMasterVolume( -0.5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pAudioSystem->getMasterVolume(), 1e-4f );
    pAudioSystem->setMusicVolume( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pAudioSystem->getMusicVolume(), 1e-4f );
    pAudioSystem->setSfxVolume( -1.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pAudioSystem->getSfxVolume(), 1e-4f );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 음소거가 볼륨 값을 먹지 않고, 마스터 한 자리에만 걸리는지 검증
 * @details 음소거를 여러 자리에 걸면 푸는 쪽이 한 자리만 되돌려 나머지가 0 으로 남는다.
 *          그래서 음소거는 **마스터 볼륨에만** 걸고, 음악·효과음 볼륨은 그대로 둔다.
 */
SW_TEST_CASE( AudioSystemTest, MuteKeepsVolumeSettings )
{
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_EXPECT_TRUE( pAudioSystem->initialize() );

    pAudioSystem->setMasterVolume( 0.8f );
    SW_EXPECT_FALSE( pAudioSystem->isMuted() );
    SW_EXPECT_NEAR_EQUAL( 0.8f, pAudioSystem->getEffectiveMasterVolume(), 1e-4f );

    pAudioSystem->setMute( true );
    SW_EXPECT_TRUE( pAudioSystem->isMuted() );
    // 음소거는 실제로 걸리는 볼륨만 0 으로 만든다 — 설정값은 그대로다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, pAudioSystem->getEffectiveMasterVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, pAudioSystem->getMasterVolume(), 1e-4f );

    // 음소거 중에 볼륨을 바꿔도 값이 먹히지 않는다.
    pAudioSystem->setMusicVolume( 0.4f );
    pAudioSystem->setSfxVolume( 0.6f );

    pAudioSystem->setMute( false );
    SW_EXPECT_FALSE( pAudioSystem->isMuted() );
    SW_EXPECT_NEAR_EQUAL( 0.8f, pAudioSystem->getEffectiveMasterVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pAudioSystem->getMusicVolume(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.6f, pAudioSystem->getSfxVolume(), 1e-4f );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 요청한 BGM 경로를 IAudioSystem 으로 되읽을 수 있는지 검증
 * @details 백엔드는 디코드를 워커로 넘기므로 재생이 실제로 시작되기 전에 이 값이 정해진다.
 *          요청 기록이 제출보다 늦으면 워커가 자기 요청을 남의 것으로 보고 버린다 — 그 순서를
 *          밖에서 확인할 수 있는 유일한 손잡이다.
 */
SW_TEST_CASE( AudioSystemTest, MusicPathTracksRequests )
{
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_EXPECT_TRUE( pAudioSystem->initialize() );
    SW_EXPECT_TRUE( pAudioSystem->getMusicPath().empty() );

    const sw::string musicPath = test::makeTempPath( "test_music_track.wav" );
    SW_EXPECT_TRUE( writeTestWav( musicPath, 64 ) );

    SW_EXPECT_TRUE( pAudioSystem->playMusic( musicPath ) );
    // playMusic 이 돌아온 시점에는 이미 이 곡이 요청되어 있어야 한다.
    SW_EXPECT_EQUAL( musicPath, pAudioSystem->getMusicPath() );

    // 실패한 요청은 이미 걸린 곡을 건드리지 않는다.
    SW_EXPECT_FALSE( pAudioSystem->playMusic( "NonExistentMusicFile.mp3" ) );
    SW_EXPECT_EQUAL( musicPath, pAudioSystem->getMusicPath() );

    // 일시정지·재개는 요청 상태를 바꾸지 않는다.
    pAudioSystem->pauseMusic();
    pAudioSystem->resumeMusic();
    SW_EXPECT_EQUAL( musicPath, pAudioSystem->getMusicPath() );

    pAudioSystem->stopMusic();
    SW_EXPECT_TRUE( pAudioSystem->getMusicPath().empty() );

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    SW_EXPECT_TRUE( sw::FileUtil::removeFile( musicPath ) );
    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 다중 워커 스레드 동시 오디오 디코딩/재생 및 COM/Media Foundation 안전성 검증
 */
SW_TEST_CASE( AudioSystemTest, MultithreadedAudioDecodeAndPlayback )
{
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_EXPECT_TRUE( pAudioSystem->initialize() );

    const sw::string wavA = test::makeTempPath( "test_mt_audio_a.wav" );
    const sw::string wavB = test::makeTempPath( "test_mt_audio_b.wav" );

    SW_EXPECT_TRUE( writeTestWav( wavA, 32 ) );
    SW_EXPECT_TRUE( writeTestWav( wavB, 32 ) );

    constexpr int32         threadCount = 4;
    sw::vector<std::thread> workers;
    workers.reserve( threadCount );

    for ( int32 workerIndex = 0; workerIndex < threadCount; ++workerIndex )
    {
        workers.emplace_back( [&, workerIndex]()
        {
            for ( int32 iter = 0; iter < 10; ++iter )
            {
                pAudioSystem->play( ( workerIndex % 2 == 0 ) ? wavA : wavB );
            }
        } );
    }

    for ( auto& t : workers )
    {
        if ( t.joinable() )
            t.join();
    }

    pAudioSystem->update( 0.016f );

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getTaskManager().waitAll();

    SW_EXPECT_TRUE( sw::FileUtil::removeFile( wavA ) );
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( wavB ) );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 디코드 태스크가 아직 도는 중에 내려도 무너지지 않는다
 * @details **실제 엔진의 순서가 이렇다.** `EngineLoop::shutdown` 은 오디오를 TaskManager 보다
 *          **먼저** 내리므로, `XAudio2System::shutdown()` 이 도는 동안 워커가 아직
 *          `playDecodedClipTask` 안에 있을 수 있다.
 *
 *          `shutdown()` 도 `_voiceMutex` 를 잡고 보이스 목록을 훑어야 한다. 잡지 않으면 워커의 `push_back` 이
 *          `_listActiveVoice` 를 재할당할 때 그 순회 참조가 **해제된 메모리**를 가리킨다. 그리고 뒤늦게 잠금을 얻은
 *          워커가 이미 `Release()` 한 `_pXAudio` 로 보이스를 만들려 들면 안 된다.
 *
 *          바로 위 `MultithreadedAudioDecodeAndPlayback` 은 `waitAll()` 을 **먼저** 부르고 내려서 이 구간을
 *          통째로 비껴간다. 여기서는 일부러 안 기다린다.
 *          (객체 자체는 `waitAll()` 뒤에 부순다. 그건 다른 이야기이고 엔진도 그 순서다.)
 */
SW_TEST_CASE( AudioSystemTest, ShutdownWhileDecodeTasksAreStillInFlight )
{

    // 경쟁 구간은 좁다 — 여러 판을 돌리고(`verify-nondeterministic-repro` 의 규칙), **파일을 매번
    // 다르게** 해서 클립 캐시를 비껴간다. 같은 파일이면 두 번째부터는 캐시에서 즉시 나와
    // 워커가 잠금 구간에 들어가기도 전에 끝나 버린다.
    constexpr int32 kRoundCount = 12;
    constexpr int32 kPlayCount  = 24;

    sw::vector<sw::string> listWavPath;
    listWavPath.reserve( kPlayCount );
    for ( int32 wavIndex = 0; wavIndex < kPlayCount; ++wavIndex )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer256> nameBuilder;
        nameBuilder.append( "test_shutdown_race_" );
        nameBuilder.append( wavIndex );
        nameBuilder.append( ".wav" );
        sw::string path = test::makeTempPath( nameBuilder.view() );
        SW_ASSERT_TRUE( writeTestWav( path, 200000 ) );
        listWavPath.push_back( std::move( path ) );
    }

    for ( int32 round = 0; round < kRoundCount; ++round )
    {
        sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
        SW_ASSERT_TRUE( pAudioSystem->initialize() );

        for ( int32 playIndex = 0; playIndex < kPlayCount; ++playIndex )
            pAudioSystem->play( listWavPath[static_cast<size_t>( playIndex )] );

        // **일부러 기다리지 않는다.** 디코드 태스크가 도는 채로 내리는 것이 이 케이스의 전부다.
        pAudioSystem->shutdown();

        // 객체를 부수기 전에는 흘려보낸다 — 태스크가 `this` 를 들고 있기 때문이다.
        if ( sw::engine::areEngineServicesBound() )
            sw::engine::getTaskManager().waitAll();

        SW_EXPECT_FALSE( pAudioSystem->isInitialized() );
    }
}

/**
 * @brief [AudioSystemTest] 부동소수 · 확장형(24 비트 다채널) WAV 도 디코드된다
 * @details 파서가 `WAVE_FORMAT_PCM` 태그만 받으면 DAW 가 흔히 내보내는 부동소수 · 확장형 WAV 가 "디코드 실패" 경고 하나만 남기고 소리가
 *          나지 않는다. 재생은 비동기라 결과를 볼 수 없어 `preload`(동기 디코드)로 잰다. 소리를 내지 않는 구성(Null)은 파일이 있는지만 본다.
 */
SW_TEST_CASE( AudioSystemTest, FloatAndExtensibleWavsDecode )
{
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_ASSERT_TRUE( pAudioSystem->initialize() );

    const sw::string floatPath      = test::makeTempPath( "test_float32.wav" );
    const sw::string extensiblePath = test::makeTempPath( "test_ext24_6ch.wav" );
    const sw::string extFloatPath   = test::makeTempPath( "test_ext_float.wav" );
    SW_ASSERT_TRUE( writeFormattedWav( floatPath, 3u, 2u, 32u, 0u ) );      // WAVE_FORMAT_IEEE_FLOAT 스테레오
    SW_ASSERT_TRUE( writeFormattedWav( extensiblePath, 1u, 6u, 24u, 1u ) ); // 확장형 24 비트 PCM 5.1
    SW_ASSERT_TRUE( writeFormattedWav( extFloatPath, 3u, 2u, 32u, 3u ) );   // 확장형 float

    SW_EXPECT_TRUE( pAudioSystem->preload( floatPath ) );
    SW_EXPECT_TRUE( pAudioSystem->preload( extensiblePath ) );
    SW_EXPECT_TRUE( pAudioSystem->preload( extFloatPath ) );
    SW_EXPECT_FALSE( pAudioSystem->preload( "NonExistentSoundFile.wav" ) );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 공통 디코더가 WAV 의 형식 칸(채널 · 비트 · float · 확장형)을 그대로 옮긴다
 * @details 재생 백엔드는 이 칸을 자기 형식 구조체로 옮기기만 한다. 여기서 틀리면 소리가 빠르거나 찢어진 채 나고, 로그에는 아무것도 남지 않는다.
 */
SW_TEST_CASE( AudioSystemTest, DecoderKeepsWavFormatFields )
{
    const sw::string extensiblePath = test::makeTempPath( "decode_ext24_6ch.wav" );
    const sw::string floatPath      = test::makeTempPath( "decode_float32.wav" );
    SW_ASSERT_TRUE( writeFormattedWav( extensiblePath, 1u, 6u, 24u, 1u ) );
    SW_ASSERT_TRUE( writeFormattedWav( floatPath, 3u, 2u, 32u, 0u ) );

    sw::vector<uint8> listBytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( extensiblePath, listBytes ) );
    sw::AudioPcm pcm;
    SW_ASSERT_TRUE( sw::AudioClipDecoder::decode( extensiblePath, listBytes.data(), listBytes.size(), pcm ) );
    SW_EXPECT_EQUAL( 6u, static_cast<uint32>( pcm._channelCount ) );
    SW_EXPECT_EQUAL( 24u, static_cast<uint32>( pcm._bitsPerSample ) );
    SW_EXPECT_EQUAL( 48000u, pcm._sampleRate );
    SW_EXPECT_EQUAL( 0x3Fu, pcm._channelMask );
    SW_EXPECT_TRUE( pcm._bExtensible == SW_TRUE );
    SW_EXPECT_TRUE( pcm._bFloat == SW_FALSE );
    SW_EXPECT_EQUAL( static_cast<size_t>( pcm.getBlockAlign() ) * 64u, pcm._listData.size() );

    SW_ASSERT_TRUE( sw::FileUtil::readFile( floatPath, listBytes ) );
    SW_ASSERT_TRUE( sw::AudioClipDecoder::decodeWav( listBytes.data(), listBytes.size(), pcm ) );
    SW_EXPECT_TRUE( pcm._bFloat == SW_TRUE );
    SW_EXPECT_TRUE( pcm._bExtensible == SW_FALSE );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( pcm._channelCount ) );

    // 잘린 파일은 거부한다(데이터 청크 길이가 파일보다 길다).
    listBytes.resize( listBytes.size() / 2 );
    SW_EXPECT_FALSE( sw::AudioClipDecoder::decodeWav( listBytes.data(), listBytes.size(), pcm ) );

    SW_EXPECT_TRUE( sw::FileUtil::removeFile( extensiblePath ) );
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( floatPath ) );
}

/**
 * @brief [AudioSystemTest] OGG Vorbis 를 16 비트 PCM 으로 푼다 — 팩 안에서도 되도록 바이트에서 푼다
 * @details Media Foundation 은 Vorbis 를 모른다. 게임 효과음(Kenney 팩)이 OGG 라 이 디코더가 없으면 재생이 조용히 실패한다.
 *          시험 자료는 `game/empty/sounds/click.ogg`(Kenney CC0)다.
 */
SW_TEST_CASE( AudioSystemTest, OggVorbisDecodesToPcm )
{
    if ( test::HostTargetTestUtil::isLeftOutOfServerPackage( "game/empty/sounds/click.ogg" ) )
        SW_TEST_SKIP( "the dedicated server package leaves textures, shader binaries and audio out (CookContract target_excluded_asset_kinds)" );
    sw::vector<uint8> listBytes;
    SW_ASSERT_TRUE( sw::ResourceUtil::readBinaryResource( "game/empty/sounds/click.ogg", listBytes ) );

    sw::AudioPcm pcm;
    SW_ASSERT_TRUE( sw::AudioClipDecoder::decode( "game/empty/sounds/click.ogg", listBytes.data(), listBytes.size(), pcm ) );
    SW_EXPECT_TRUE( pcm._channelCount == 1u || pcm._channelCount == 2u );
    SW_EXPECT_TRUE( pcm._sampleRate >= 22050u && pcm._sampleRate <= 48000u );
    SW_EXPECT_EQUAL( 16u, static_cast<uint32>( pcm._bitsPerSample ) );
    SW_EXPECT_TRUE( pcm._bFloat == SW_FALSE );
    // 클릭 하나도 수 ms 는 된다 — 프레임이 없으면 머리만 읽고 소리는 풀지 않은 것이다. (압축 크기와는 견주지 않는다: 짧은 소리는
    // Vorbis 머리의 코드북이 파일 대부분이라 풀어도 더 작을 수 있다.)
    const size_t frameCount = pcm._listData.size() / pcm.getBlockAlign();
    SW_EXPECT_TRUE( frameCount * 1000u / pcm._sampleRate >= 5u );
    SW_EXPECT_EQUAL( size_t{ 0 }, pcm._listData.size() % pcm.getBlockAlign() );

    // OGG 가 아닌 바이트 · 머리만 남은 바이트는 거부한다.
    sw::AudioPcm            rejected;
    const sw::vector<uint8> listGarbage( 256, uint8{ 0x5A } );
    SW_EXPECT_FALSE( sw::AudioClipDecoder::decodeOgg( listGarbage.data(), listGarbage.size(), rejected ) );
    SW_EXPECT_FALSE( sw::AudioClipDecoder::decodeOgg( listBytes.data(), 64, rejected ) );

    // 재생 시스템도 같은 파일을 읽는다(소리를 내지 않는 구성은 파일이 있는지만 본다).
    sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
    SW_ASSERT_TRUE( pAudioSystem->initialize() );
    SW_EXPECT_TRUE( pAudioSystem->preload( "game/empty/sounds/click.ogg" ) );
    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 주석 헤더의 길이 칸이 패킷 밖을 가리키는 OGG 는 죽지 않고 거절한다
 * @details `LoaderFuzzTest` 가 찾은 결함이다 — stb_vorbis 1.22 는 주석 수 · 길이를 패킷과 대조하지 않아, 큰 값이면 할당이 실패한 뒤 채우지 않은 칸을
 *          `free` 해 프로세스가 죽었다. 실제 리소스의 OGG 하나를 읽어 주석 수 · 공급자 길이만 바꾼다(페이지 CRC 는 stb 가 보지 않는다).
 */
SW_TEST_CASE( AudioSystemTest, OggWithOutOfPacketCommentLengthsIsRejected )
{
    const sw::string  path = sw::FileUtil::joinPath( sw::ResourceUtil::getRootFolderPath(), "game/shooter3d/sounds/footstep_concrete_000.ogg" );
    sw::vector<uint8> originalBytes;
    SW_ASSERT_TRUE_MSG( sw::FileUtil::readFile( path, originalBytes ), path.c_str() );

    sw::AudioPcm pcm;
    SW_ASSERT_TRUE( sw::AudioClipDecoder::decodeOgg( originalBytes.data(), originalBytes.size(), pcm ) );

    // 주석 헤더: 0x03 "vorbis" · 공급자 길이(4) · 공급자 · 주석 수(4)
    size_t headerOffset = 0;
    for ( size_t index = 0; index + 7 <= originalBytes.size(); ++index )
    {
        if ( originalBytes[index] == 3 && std::memcmp( originalBytes.data() + index + 1, "vorbis", 6 ) == 0 )
        {
            headerOffset = index;
            break;
        }
    }
    SW_ASSERT_TRUE( headerOffset != 0 );
    const size_t vendorLengthOffset = headerOffset + 7;
    const uint32 vendorLength       = static_cast<uint32>( originalBytes[vendorLengthOffset] ) | ( static_cast<uint32>( originalBytes[vendorLengthOffset + 1] ) << 8 );
    const size_t commentCountOffset = vendorLengthOffset + 4 + vendorLength;

    struct Corruption
    {
        size_t _offset;
        uint32 _value;
    };
    const Corruption kArrCorruption[] = {
        {commentCountOffset, 0x20000000u}, // 주석 수 × 8 바이트가 int 를 넘친다 → 0 바이트 할당 → 정리가 빈 목록을 훑는다
        {commentCountOffset, 0x00FFFFFFu}, // 패킷보다 훨씬 많은 주석
        {vendorLengthOffset, 0xFFFFFFFFu}, // 공급자 길이 + 1 이 0 이 된다
        {vendorLengthOffset, 0x7FFFFFF0u}, // 할당이 실패할 만큼 큰 공급자 길이
    };
    for ( const Corruption& corruption : kArrCorruption )
    {
        sw::vector<uint8> bytes = originalBytes;
        for ( size_t byteIndex = 0; byteIndex < 4; ++byteIndex )
            bytes[corruption._offset + byteIndex] = static_cast<uint8>( corruption._value >> ( 8 * byteIndex ) );
        sw::AudioPcm corruptPcm;
        SW_EXPECT_FALSE( sw::AudioClipDecoder::decodeOgg( bytes.data(), bytes.size(), corruptPcm ) );
    }
}
