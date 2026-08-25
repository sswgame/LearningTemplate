#include "pch.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Task/TaskManager.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioSystemTest — 오디오 시스템 수명주기 및 재생 기능 검증
// ------------------------------------------------------------------------------

SW_TEST_CASE( AudioSystemTest, LifecycleAndState )
{
	sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
	const bool						 initOk		  = pAudioSystem->initialize();
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

	const sw::string validWavPath  = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_valid.wav" );
	const sw::string malformedPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_malformed.wav" );
	const sw::string truncatedPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_truncated.wav" );

	// 1) 유효한 PCM 16-bit Mono 44.1kHz WAV 생성
	{
		sw::vector<uint8> wavBytes;
		wavBytes.reserve( 44 + 64 );

		// RIFF header
		wavBytes.insert( wavBytes.end(), { 'R', 'I', 'F', 'F' } );
		const uint32 totalSizeMinus8 = 36 + 64;
		wavBytes.push_back( static_cast<uint8>( totalSizeMinus8 & 0xFF ) );
		wavBytes.push_back( static_cast<uint8>( ( totalSizeMinus8 >> 8 ) & 0xFF ) );
		wavBytes.push_back( static_cast<uint8>( ( totalSizeMinus8 >> 16 ) & 0xFF ) );
		wavBytes.push_back( static_cast<uint8>( ( totalSizeMinus8 >> 24 ) & 0xFF ) );
		wavBytes.insert( wavBytes.end(), { 'W', 'A', 'V', 'E' } );

		// fmt chunk
		wavBytes.insert( wavBytes.end(), { 'f', 'm', 't', ' ' } );
		const uint32 fmtChunkSize = 16;
		wavBytes.push_back( static_cast<uint8>( fmtChunkSize & 0xFF ) );
		wavBytes.push_back( static_cast<uint8>( ( fmtChunkSize >> 8 ) & 0xFF ) );
		wavBytes.push_back( 0 );
		wavBytes.push_back( 0 );

		// format tag = 1 (PCM), channels = 1
		wavBytes.push_back( 1 );
		wavBytes.push_back( 0 );
		wavBytes.push_back( 1 );
		wavBytes.push_back( 0 );

		// sample rate = 44100 (0xAC44)
		wavBytes.push_back( 0x44 );
		wavBytes.push_back( 0xAC );
		wavBytes.push_back( 0x00 );
		wavBytes.push_back( 0x00 );

		// byte rate = 88200 (0x015888)
		wavBytes.push_back( 0x88 );
		wavBytes.push_back( 0x58 );
		wavBytes.push_back( 0x01 );
		wavBytes.push_back( 0x00 );

		// block align = 2, bits per sample = 16
		wavBytes.push_back( 2 );
		wavBytes.push_back( 0 );
		wavBytes.push_back( 16 );
		wavBytes.push_back( 0 );

		// data chunk
		wavBytes.insert( wavBytes.end(), { 'd', 'a', 't', 'a' } );
		const uint32 dataSize = 64;
		wavBytes.push_back( static_cast<uint8>( dataSize & 0xFF ) );
		wavBytes.push_back( 0 );
		wavBytes.push_back( 0 );
		wavBytes.push_back( 0 );

		for ( uint32 index = 0; index < 64; ++index )
			wavBytes.push_back( static_cast<uint8>( index ) );

		SW_EXPECT_TRUE( sw::FileUtil::writeFile( validWavPath, wavBytes.data(), wavBytes.size() ) );
	}

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
 * @brief [AudioSystemTest] 마스터 볼륨 및 BGM 볼륨 설정 검증
 */
SW_TEST_CASE( AudioSystemTest, VolumeControls )
{
	sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
	SW_EXPECT_TRUE( pAudioSystem->initialize() );

	pAudioSystem->setMasterVolume( 0.75f );
	pAudioSystem->setMusicVolume( 0.5f );

	// 클램핑 및 음수/오버플로 안전성
	pAudioSystem->setMasterVolume( 1.5f );
	pAudioSystem->setMasterVolume( -0.5f );
	pAudioSystem->setMusicVolume( 2.0f );
	pAudioSystem->setMusicVolume( -1.0f );

	pAudioSystem->shutdown();
}

/**
 * @brief [AudioSystemTest] 다중 워커 스레드 동시 오디오 디코딩/재생 및 COM/Media Foundation 안전성 검증
 */
SW_TEST_CASE( AudioSystemTest, MultithreadedAudioDecodeAndPlayback )
{
	sw::unique_ptr<sw::IAudioSystem> pAudioSystem = sw::IAudioSystem::create();
	SW_EXPECT_TRUE( pAudioSystem->initialize() );

	const sw::string tempDir = sw::FileUtil::getTempDirectory();
	const sw::string wavA	 = sw::FileUtil::joinPath( tempDir, "test_mt_audio_a.wav" );
	const sw::string wavB	 = sw::FileUtil::joinPath( tempDir, "test_mt_audio_b.wav" );

	auto generateWav = []( const sw::string& path )
	{
		sw::vector<uint8> bytes;
		bytes.insert( bytes.end(), { 'R', 'I', 'F', 'F' } );
		const uint32 total = 36 + 32;
		bytes.push_back( static_cast<uint8>( total & 0xFF ) );
		bytes.push_back( static_cast<uint8>( ( total >> 8 ) & 0xFF ) );
		bytes.push_back( 0 );
		bytes.push_back( 0 );
		bytes.insert( bytes.end(), { 'W', 'A', 'V', 'E' } );
		bytes.insert( bytes.end(), { 'f', 'm', 't', ' ' } );
		bytes.push_back( 16 );
		bytes.push_back( 0 );
		bytes.push_back( 0 );
		bytes.push_back( 0 );
		bytes.push_back( 1 ); // PCM
		bytes.push_back( 0 );
		bytes.push_back( 1 ); // Mono
		bytes.push_back( 0 );
		bytes.push_back( 0x44 ); // 44100
		bytes.push_back( 0xAC );
		bytes.push_back( 0 );
		bytes.push_back( 0 );
		bytes.push_back( 0x88 ); // 88200
		bytes.push_back( 0x58 );
		bytes.push_back( 0x01 );
		bytes.push_back( 0 );
		bytes.push_back( 2 ); // block align
		bytes.push_back( 0 );
		bytes.push_back( 16 ); // 16 bits
		bytes.push_back( 0 );
		bytes.insert( bytes.end(), { 'd', 'a', 't', 'a' } );
		bytes.push_back( 32 );
		bytes.push_back( 0 );
		bytes.push_back( 0 );
		bytes.push_back( 0 );
		for ( uint32 i = 0; i < 32; ++i )
			bytes.push_back( static_cast<uint8>( i ) );
		sw::FileUtil::writeFile( path, bytes.data(), bytes.size() );
	};

	generateWav( wavA );
	generateWav( wavB );

	constexpr int32 threadCount = 4;
	std::vector<std::thread> workers;
	workers.reserve( threadCount );

	for ( int32 i = 0; i < threadCount; ++i )
	{
		workers.emplace_back( [&, i]()
		{
			for ( int32 iter = 0; iter < 10; ++iter )
			{
				pAudioSystem->play( ( i % 2 == 0 ) ? wavA : wavB );
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
