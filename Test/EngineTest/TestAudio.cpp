#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioSystemTest — 오디오 시스템 수명주기 및 재생 기능 검증
// ------------------------------------------------------------------------------

namespace
{
    /**
     * @brief PCM 16비트 모노 44.1kHz WAV 파일을 하나 만듭니다.
     * @details 예전에는 이 27줄이 두 케이스에 글자 그대로 복사돼 있었다.
     */
    bool writeTestWav( const sw::string& path, uint32 dataByteCount )
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
    sw::FileUtil::removeFile( validWavPath );
    sw::FileUtil::removeFile( malformedPath );
    sw::FileUtil::removeFile( truncatedPath );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 볼륨 설정과 클램프를 IAudioSystem 으로 읽어 검증
 * @details 예전에는 getter 가 XAudio2System 에만 있어서 **IAudioSystem 을 든 누구도 읽을 수
 *          없었다** — 그래서 "설정한 값이 실제로 들어갔는지" 를 아무도 확인하지 못했다.
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

    sw::FileUtil::removeFile( musicPath );
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

    sw::FileUtil::removeFile( wavA );
    sw::FileUtil::removeFile( wavB );

    pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 디코드 태스크가 아직 도는 중에 내려도 무너지지 않는다
 * @details **실제 엔진의 순서가 이렇다.** `EngineLoop::shutdown` 은 오디오를 TaskManager 보다
 *          **먼저** 내리므로, `XAudio2System::shutdown()` 이 도는 동안 워커가 아직
 *          `playDecodedClipTask` 안에 있을 수 있다.
 *
 *          그런데 `shutdown()` 만 `_voiceMutex` 를 잡지 않고 보이스 목록을 훑었다 —
 *          `_voiceMutex` 의 주석이 처음부터 "보이스를 지킨다" 고 적고 있었는데 여기만 어겼다.
 *          워커의 `push_back` 이 `_listActiveVoice` 를 재할당하면 그 순회 참조가 그대로
 *          **해제된 메모리**를 가리킨다. 게다가 뒤늦게 잠금을 얻은 워커는 이미 `Release()` 한
 *          `_pXAudio` 로 보이스를 만들려 든다.
 *
 *          바로 위 `MultithreadedAudioDecodeAndPlayback` 은 `waitAll()` 을 **먼저** 부르고
 *          내려서 이 구간을 통째로 비껴갔다 — 그래서 오래 안 보였다. 여기서는 일부러 안 기다린다.
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

    for ( const sw::string& path : listWavPath )
        sw::FileUtil::removeFile( path );
}
