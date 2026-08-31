#include "pch.h"

#include "Engine/Audio/XAudio2System.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Common/EngineServices.h"

namespace sw
{
	namespace
	{
		struct XAudio2SystemInternal
		{
			/**
			 * @brief 디코딩된 PCM 오디오 포맷 및 바이트 버퍼를 보관하는 구조체
			 */
			struct PcmClip
			{
				WAVEFORMATEX  _format{};
				vector<uint8> _listData;
			};

			struct VoiceBuffer
			{
				IXAudio2SourceVoice* _pVoice{ nullptr };
				shared_ptr<PcmClip>	 _pClip{ nullptr };
			};

			/**
			 * @brief 호출 스레드에서 COM 및 Media Foundation이 초기화되어 있음을 보장하는 RAII 헬퍼
			 */
			struct ScopedThreadComAndMf
			{
				uint8				   _bCoInit	 : 1;
				uint8				   _bMfInit	 : 1;
				[[maybe_unused]] uint8 _reserved : 6;

				ScopedThreadComAndMf()
					: _bCoInit{ SW_FALSE }
					, _bMfInit{ SW_FALSE }
					, _reserved{ 0 }
				{
					const HRESULT hrCo = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
					if ( SUCCEEDED( hrCo ) )
					{
						_bCoInit = SW_TRUE;
					}

					const HRESULT hrMf = MFStartup( MF_VERSION );
					if ( SUCCEEDED( hrMf ) )
					{
						_bMfInit = SW_TRUE;
					}
				}

				~ScopedThreadComAndMf()
				{
					if ( _bMfInit == SW_TRUE )
					{
						MFShutdown();
					}
					if ( _bCoInit == SW_TRUE )
					{
						CoUninitialize();
					}
				}
			};

			/**
			 * @brief Little-Endian 16비트 정수를 비정렬 안전하게 읽습니다.
			 */
			static inline uint16 readUint16LE( const uint8* pBytes )
			{
				return static_cast<uint16>( pBytes[0] ) |
					   static_cast<uint16>( static_cast<uint16>( pBytes[1] ) << 8 );
			}

			/**
			 * @brief Little-Endian 32비트 정수를 비정렬 안전하게 읽습니다.
			 */
			static inline uint32 readUint32LE( const uint8* pBytes )
			{
				return static_cast<uint32>( pBytes[0] ) |
					   ( static_cast<uint32>( pBytes[1] ) << 8 ) |
					   ( static_cast<uint32>( pBytes[2] ) << 16 ) |
					   ( static_cast<uint32>( pBytes[3] ) << 24 );
			}

			/**
			 * @brief 메모리 버퍼로부터 표준 RIFF WAV 데이터를 파싱하여 PCM 데이터를 추출합니다.
			 */
			static bool parseWavPcmMemory( const uint8* pBytes, size_t byteCount, PcmClip& out )
			{
				if ( pBytes == nullptr || byteCount < 44 )
					return false;

				if ( Memory::compare( pBytes, "RIFF", 4 ) != 0 || Memory::compare( pBytes + 8, "WAVE", 4 ) != 0 )
					return false;

				size_t		 offset = 12;
				const uint8* pData	= nullptr;
				uint32		 dataSize{ 0 };
				WAVEFORMATEX fmt{};
				bool		 bHaveFmt{ false };

				while ( offset + 8 <= byteCount )
				{
					const utf8*	 pChunkId  = reinterpret_cast<const utf8*>( pBytes + offset );
					const uint32 chunkSize = readUint32LE( pBytes + offset + 4 );
					offset += 8;

					if ( offset + chunkSize > byteCount )
						break;

					if ( Memory::compare( pChunkId, "fmt ", 4 ) == 0 && chunkSize >= 16 )
					{
						const uint8* pFmt	= pBytes + offset;
						fmt.wFormatTag		= readUint16LE( pFmt + 0 );
						fmt.nChannels		= readUint16LE( pFmt + 2 );
						fmt.nSamplesPerSec	= readUint32LE( pFmt + 4 );
						fmt.nAvgBytesPerSec = readUint32LE( pFmt + 8 );
						fmt.nBlockAlign		= readUint16LE( pFmt + 12 );
						fmt.wBitsPerSample	= readUint16LE( pFmt + 14 );
						fmt.cbSize			= 0;
						bHaveFmt			= true;
					}
					else if ( Memory::compare( pChunkId, "data", 4 ) == 0 )
					{
						pData	 = pBytes + offset;
						dataSize = chunkSize;
					}
					offset += chunkSize + ( chunkSize & 1u );
				}

				if ( bHaveFmt == false || pData == nullptr || dataSize == 0 )
					return false;
				if ( fmt.wFormatTag != WAVE_FORMAT_PCM || fmt.nChannels == 0 || fmt.nSamplesPerSec == 0 || fmt.nBlockAlign == 0 )
					return false;

				out._format = fmt;
				out._listData.assign( pData, pData + dataSize );
				return true;
			}

			/**
			 * @brief 표준 RIFF WAV 파일의 fmt 및 data 청크를 파싱하여 PCM 데이터를 추출합니다.
			 */
			static bool loadWavPcm( string_view absPath, PcmClip& out )
			{
				vector<uint8> listFile;
				if ( FileUtil::readFile( absPath, listFile ) == false )
					return false;

				return parseWavPcmMemory( listFile.data(), listFile.size(), out );
			}

			/**
			 * @brief Windows Media Foundation을 사용하여 MP3/압축 오디오를 PCM 바이트 스트림으로 디코딩합니다.
			 */
			static bool loadViaMediaFoundation( string_view absPath, PcmClip& out )
			{
				ScopedThreadComAndMf threadComScope;

				IMFSourceReader* pReader{ nullptr };
				const wstring	 wpath = StringUtil::utf8ToUtf16( string( absPath ).c_str() );
				HRESULT			 hr	   = MFCreateSourceReaderFromURL( wpath.c_str(), nullptr, &pReader );
				if ( FAILED( hr ) || pReader == nullptr )
					return false;

				IMFMediaType* pPartial{ nullptr };
				MFCreateMediaType( &pPartial );
				pPartial->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
				pPartial->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_PCM );
				pReader->SetCurrentMediaType( static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), nullptr, pPartial );
				pPartial->Release();

				IMFMediaType* pNative{ nullptr };
				hr = pReader->GetCurrentMediaType( static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), &pNative );
				if ( FAILED( hr ) || pNative == nullptr )
				{
					pReader->Release();
					return false;
				}

				const UINT32 channels	= MFGetAttributeUINT32( pNative, MF_MT_AUDIO_NUM_CHANNELS, 2 );
				const UINT32 sampleRate = MFGetAttributeUINT32( pNative, MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100 );
				const UINT32 bits		= MFGetAttributeUINT32( pNative, MF_MT_AUDIO_BITS_PER_SAMPLE, 16 );
				pNative->Release();

				WAVEFORMATEX fmt{};
				fmt.wFormatTag		= WAVE_FORMAT_PCM;
				fmt.nChannels		= static_cast<WORD>( channels );
				fmt.nSamplesPerSec	= sampleRate;
				fmt.wBitsPerSample	= static_cast<WORD>( bits );
				fmt.nBlockAlign		= static_cast<WORD>( fmt.nChannels * fmt.wBitsPerSample / 8 );
				fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

				vector<uint8> listPcm;
				for ( ;; )
				{
					DWORD			flags{ 0 };
					IMFSample*		pSample{ nullptr };
					IMFMediaBuffer* pBuffer{ nullptr };
					hr = pReader->ReadSample( static_cast<DWORD>( MF_SOURCE_READER_FIRST_AUDIO_STREAM ), 0, nullptr, &flags, nullptr, &pSample );
					if ( FAILED( hr ) || ( ( flags & MF_SOURCE_READERF_ENDOFSTREAM ) != 0 ) )
						break;
					if ( pSample == nullptr )
						continue;
					hr = pSample->ConvertToContiguousBuffer( &pBuffer );
					pSample->Release();
					if ( FAILED( hr ) || pBuffer == nullptr )
						continue;
					BYTE* pData{ nullptr };
					DWORD maxLen = 0, curLen = 0;
					pBuffer->Lock( &pData, &maxLen, &curLen );
					if ( pData != nullptr && curLen > 0 )
						listPcm.insert( listPcm.end(), pData, pData + curLen );
					pBuffer->Unlock();
					pBuffer->Release();
				}

				pReader->Release();

				if ( listPcm.empty() )
					return false;
				out._format	  = fmt;
				out._listData = std::move( listPcm );
				return true;
			}

			static bool loadClip( string_view absPath, PcmClip& out )
			{
				if ( FileUtil::hasExtension( absPath, ".wav" ) )
					return loadWavPcm( absPath, out );
				return loadViaMediaFoundation( absPath, out );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "XAudio2System" );

#if defined( SW_PLATFORM_WINDOWS )
#endif

	struct XAudio2SystemImpl
	{
#if defined( SW_PLATFORM_WINDOWS )
		IXAudio2*														  _pXAudio{ nullptr };
		IXAudio2MasteringVoice*											  _pMasterVoice{ nullptr };
		vector<XAudio2SystemInternal::VoiceBuffer>						  _listActiveVoice;
		IXAudio2SourceVoice*											  _pMusicVoice{ nullptr };
		shared_ptr<XAudio2SystemInternal::PcmClip>						  _pMusicClip{ nullptr };
		unordered_map<string, shared_ptr<XAudio2SystemInternal::PcmClip>> _mapClipCache;
		mutex															  _clipCacheMutex;
		mutex															  _voiceMutex;

		shared_ptr<XAudio2SystemInternal::PcmClip> getOrLoadClip( string_view absPath )
		{
			const string key( absPath );
			{
				std::scoped_lock<mutex> lock{ _clipCacheMutex };
				auto					it = _mapClipCache.find( key );
				if ( it != _mapClipCache.end() )
					return it->second;
			}

			XAudio2SystemInternal::PcmClip clip{};
			if ( XAudio2SystemInternal::loadClip( absPath, clip ) == false )
				return nullptr;

			auto pShared = make_shared<XAudio2SystemInternal::PcmClip>( std::move( clip ) );
			{
				std::scoped_lock<mutex> lock{ _clipCacheMutex };
				_mapClipCache[key] = pShared;
			}
			return pShared;
		}
#endif
		string				   _musicPath;
		float32				   _masterVolume{ 1.0f };
		float32				   _musicVolume{ 1.0f };
		float32				   _sfxVolume{ 1.0f };
		uint8				   _bMuted			: 1;
		uint8				   _bInitialized	: 1;
		uint8				   _bComInitialized : 1;
		uint8				   _bMfInitialized	: 1;
		[[maybe_unused]] uint8 _reservedAudio	: 4;

		XAudio2SystemImpl()
			: _musicPath{}
			, _masterVolume{ 1.0f }
			, _musicVolume{ 1.0f }
			, _sfxVolume{ 1.0f }
			, _bMuted{ SW_FALSE }
			, _bInitialized{ SW_FALSE }
			, _bComInitialized{ SW_FALSE }
			, _bMfInitialized{ SW_FALSE }
			, _reservedAudio{ 0 } {}
	};

	XAudio2System::XAudio2System()
		: _impl{ make_unique<XAudio2SystemImpl>() }
	{
	}

	XAudio2System::~XAudio2System()
	{
		shutdown();
	}

	bool XAudio2System::initialize()
	{
		if ( _impl == nullptr )
			_impl = make_unique<XAudio2SystemImpl>();
		if ( _impl->_bInitialized != 0 )
			return true;

#if defined( SW_PLATFORM_WINDOWS )
		HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
		if ( hr == S_OK || hr == S_FALSE )
			_impl->_bComInitialized = 1;
		else if ( hr == RPC_E_CHANGED_MODE )
			_impl->_bComInitialized = 0;
		else
			SW_LOG_WARNING( "CoInitializeEx failed (0x%#).", static_cast<uint32>( hr ) );

		const HRESULT mfHr = MFStartup( MF_VERSION );
		if ( SUCCEEDED( mfHr ) )
			_impl->_bMfInitialized = 1;
		else
			SW_LOG_WARNING( "MFStartup failed (0x%#).", static_cast<uint32>( mfHr ) );

		IXAudio2* pXAudio{ nullptr };
		hr = XAudio2Create( &pXAudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
		if ( FAILED( hr ) || pXAudio == nullptr )
		{
			SW_LOG_WARNING( "XAudio2Create failed (0x%#). Running null audio.", static_cast<uint32>( hr ) );
			_impl->_bInitialized = 1;
			return true;
		}

		IXAudio2MasteringVoice* pMasterVoice{ nullptr };
		hr = pXAudio->CreateMasteringVoice( &pMasterVoice );
		if ( FAILED( hr ) || pMasterVoice == nullptr )
		{
			SW_LOG_WARNING( "CreateMasteringVoice failed (0x%#).", static_cast<uint32>( hr ) );
			pXAudio->Release();
			_impl->_bInitialized = 1;
			return true;
		}

		_impl->_pXAudio		 = pXAudio;
		_impl->_pMasterVoice = pMasterVoice;
		_impl->_pMasterVoice->SetVolume( _impl->_bMuted ? 0.0f : _impl->_masterVolume );
		SW_LOG_INFO( "XAudio2 mastering voice ready." );
#else
		SW_LOG_INFO( "Null audio backend." );
#endif

		_impl->_bInitialized = 1;
		return true;
	}

	/**
	 * @brief 오디오 시스템을 종료하고 모든 활성 사운드 보이스 및 XAudio2 마스터링 보이스를 파괴합니다.
	 */
	void XAudio2System::shutdown()
	{
		if ( _impl == nullptr || _impl->_bInitialized == 0 )
			return;
		stopMusic();
#if defined( SW_PLATFORM_WINDOWS )
		for ( XAudio2SystemInternal::VoiceBuffer& voiceBuffer : _impl->_listActiveVoice )
		{
			if ( voiceBuffer._pVoice != nullptr )
			{
				voiceBuffer._pVoice->Stop( 0 );
				voiceBuffer._pVoice->DestroyVoice();
			}
		}
		_impl->_listActiveVoice.clear();
		if ( _impl->_pMasterVoice != nullptr )
		{
			_impl->_pMasterVoice->DestroyVoice();
			_impl->_pMasterVoice = nullptr;
		}
		if ( _impl->_pXAudio != nullptr )
		{
			_impl->_pXAudio->Release();
			_impl->_pXAudio = nullptr;
		}
		if ( _impl->_bMfInitialized != 0 )
		{
			MFShutdown();
			_impl->_bMfInitialized = 0;
		}
		if ( _impl->_bComInitialized != 0 )
		{
			CoUninitialize();
			_impl->_bComInitialized = 0;
		}
		{
			std::scoped_lock<mutex> lock{ _impl->_clipCacheMutex };
			_impl->_mapClipCache.clear();
		}
#endif
		_impl->_bInitialized = 0;
		SW_LOG_INFO( "Shut down." );
	}

	/**
	 * @brief 매 프레임 재생이 종료된(큐 버퍼가 0이 된) 원샷 사운드 보이스를 감지하여 정리합니다.
	 */
	void XAudio2System::update( float32 )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl == nullptr )
			return;
	#if defined( SW_PLATFORM_WINDOWS )
		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
	#endif
		vector<XAudio2SystemInternal::VoiceBuffer>& voices = _impl->_listActiveVoice;
		for ( size_t voiceIndex = 0; voiceIndex < voices.size(); )
		{
			XAudio2SystemInternal::VoiceBuffer& v = voices[voiceIndex];
			if ( v._pVoice == nullptr )
			{
				v = std::move( voices.back() );
				voices.pop_back();
				continue;
			}
			XAUDIO2_VOICE_STATE state{};
			v._pVoice->GetState( &state );
			if ( state.BuffersQueued == 0 )
			{
				v._pVoice->DestroyVoice();
				v = std::move( voices.back() );
				voices.pop_back();
				continue;
			}
			++voiceIndex;
		}
#endif
	}

	/**
	 * @brief 단발성 효과음(SFX)을 비동기 원샷으로 1회 재생합니다.
	 */
	bool XAudio2System::play( string_view path )
	{
		return playInternal( path, false );
	}

	/**
	 * @brief 배경음악(BGM)을 루프로 재생합니다. 기존 BGM이 있다면 교체합니다.
	 */
	bool XAudio2System::playMusic( string_view path )
	{
		if ( path.empty() || _impl == nullptr )
			return false;
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_musicPath == path && _impl->_pMusicVoice != nullptr )
			return true;
#else
		if ( _impl->_musicPath == path )
			return true;
#endif
		stopMusic();
		return playInternal( path, true );
	}

	/**
	 * @brief 현재 재생 중인 배경음악(BGM)을 정지하고 리소스를 해제합니다.
	 */
	void XAudio2System::stopMusic()
	{
		if ( _impl == nullptr )
			return;
#if defined( SW_PLATFORM_WINDOWS )
		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
#endif
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_pMusicVoice != nullptr )
		{
			_impl->_pMusicVoice->Stop( 0 );
			_impl->_pMusicVoice->DestroyVoice();
			_impl->_pMusicVoice = nullptr;
		}
		_impl->_pMusicClip.reset();
#endif
		_impl->_musicPath.clear();
	}

	void XAudio2System::pauseMusic()
	{
		if ( _impl == nullptr )
			return;
#if defined( SW_PLATFORM_WINDOWS )
		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
#endif
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_pMusicVoice != nullptr )
			_impl->_pMusicVoice->Stop( 0 );
#endif
	}

	void XAudio2System::resumeMusic()
	{
		if ( _impl == nullptr )
			return;
#if defined( SW_PLATFORM_WINDOWS )
		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
#endif
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_pMusicVoice != nullptr )
			_impl->_pMusicVoice->Start( 0 );
#endif
	}

	void XAudio2System::setMasterVolume( float32 volume )
	{
		if ( _impl == nullptr )
			return;
		_impl->_masterVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_pMasterVoice != nullptr )
			_impl->_pMasterVoice->SetVolume( _impl->_bMuted != 0 ? 0.0f : _impl->_masterVolume );
#endif
	}

	float32 XAudio2System::getMasterVolume() const
	{
		return _impl != nullptr ? _impl->_masterVolume : 1.0f;
	}

	void XAudio2System::setMusicVolume( float32 volume )
	{
		if ( _impl == nullptr )
			return;
		_impl->_musicVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
#if defined( SW_PLATFORM_WINDOWS )
		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
#endif
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_pMusicVoice != nullptr )
			_impl->_pMusicVoice->SetVolume( _impl->_bMuted != 0 ? 0.0f : _impl->_musicVolume );
#endif
	}

	float32 XAudio2System::getMusicVolume() const
	{
		return _impl != nullptr ? _impl->_musicVolume : 1.0f;
	}

	void XAudio2System::setSfxVolume( float32 volume )
	{
		if ( _impl == nullptr )
			return;
		_impl->_sfxVolume = MathUtil::clamp( volume, 0.0f, 1.0f );
#if defined( SW_PLATFORM_WINDOWS )
		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };
#endif
#if defined( SW_PLATFORM_WINDOWS )
		const float32 effectiveVol = _impl->_bMuted != 0 ? 0.0f : _impl->_sfxVolume;
		for ( XAudio2SystemInternal::VoiceBuffer& vb : _impl->_listActiveVoice )
		{
			if ( vb._pVoice != nullptr )
				vb._pVoice->SetVolume( effectiveVol );
		}
#endif
	}

	float32 XAudio2System::getSfxVolume() const
	{
		return _impl != nullptr ? _impl->_sfxVolume : 1.0f;
	}

	void XAudio2System::setMute( bool bMute )
	{
		if ( _impl == nullptr )
			return;
		_impl->_bMuted = bMute ? 1 : 0;
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl->_pMasterVoice != nullptr )
			_impl->_pMasterVoice->SetVolume( _impl->_bMuted ? 0.0f : _impl->_masterVolume );
#endif
	}

	bool XAudio2System::isMuted() const
	{
		return _impl != nullptr && _impl->_bMuted != 0;
	}

	bool XAudio2System::isInitialized() const
	{
		return _impl != nullptr && _impl->_bInitialized != 0;
	}

	void XAudio2System::playDecodedClipTask( const TaskArgs& args )
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _impl == nullptr || _impl->_pXAudio == nullptr )
			return;

		const string abs		   = args.get<string>( 0 );
		const bool	 loop		   = args.get<bool>( 1 );
		const string requestedPath = args.get<string>( 2 );

		shared_ptr<XAudio2SystemInternal::PcmClip> pClip = _impl->getOrLoadClip( abs );
		if ( pClip == nullptr || pClip->_listData.empty() )
		{
			SW_LOG_WARNING( "Failed to decode: %#", abs );
			return;
		}

		std::scoped_lock<mutex> lock{ _impl->_voiceMutex };

		if ( loop && _impl->_musicPath != requestedPath )
			return;

		IXAudio2SourceVoice* pVoice{ nullptr };
		HRESULT				 hr = _impl->_pXAudio->CreateSourceVoice( &pVoice, &pClip->_format );
		if ( FAILED( hr ) || pVoice == nullptr )
		{
			SW_LOG_WARNING( "CreateSourceVoice failed (0x%#)", static_cast<uint32>( hr ) );
			return;
		}

		XAUDIO2_BUFFER audioBuffer{};
		audioBuffer.AudioBytes = static_cast<UINT32>( pClip->_listData.size() );
		audioBuffer.pAudioData = std::as_const( pClip->_listData ).data();
		if ( loop )
		{
			audioBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
			_impl->_pMusicClip	  = pClip;
			_impl->_pMusicVoice	  = pVoice;
			pVoice->SetVolume( _impl->_musicVolume );
		}
		else
		{
			audioBuffer.Flags = XAUDIO2_END_OF_STREAM;
			_impl->_listActiveVoice.push_back( XAudio2SystemInternal::VoiceBuffer{} );
			XAudio2SystemInternal::VoiceBuffer& slot = _impl->_listActiveVoice.back();
			slot._pClip								 = pClip;
			slot._pVoice							 = pVoice;
			pVoice->SetVolume( _impl->_sfxVolume );
		}

		hr = pVoice->SubmitSourceBuffer( &audioBuffer );
		if ( FAILED( hr ) )
		{
			SW_LOG_WARNING( "SubmitSourceBuffer failed (0x%#)", static_cast<uint32>( hr ) );
			if ( loop )
			{
				_impl->_pMusicVoice->DestroyVoice();
				_impl->_pMusicVoice = nullptr;
				_impl->_pMusicClip.reset();
				_impl->_musicPath.clear();
			}
			return;
		}
		pVoice->Start( 0 );
		SW_LOG_TRACE( "Playing %# (%# loop=%#)", abs, static_cast<uint32>( audioBuffer.AudioBytes ),
					  loop ? 1 : 0 );
#else
		(void)args;
#endif
	}

	/**
	 * @brief 오디오 파일을 로드/디코딩하여 XAudio2 소스 보이스를 생성하고 버퍼를 제출하여 재생을 시작합니다.
	 */
	bool XAudio2System::playInternal( string_view path, bool loop )
	{
		if ( _impl == nullptr || _impl->_bInitialized == 0 )
			return false;

		if ( path.empty() )
			return false;

#if defined( SW_PLATFORM_WINDOWS )
		string abs = ResourceUtil::getResourcePath( path );
		if ( abs.empty() )
			abs = string( path );
		if ( FileUtil::fileExists( abs ) == false )
		{
			SW_LOG_WARNING( "File not found: %#", abs );
			return false;
		}

		if ( _impl->_pXAudio == nullptr )
		{
			if ( loop )
				_impl->_musicPath = string( path );
			SW_LOG_TRACE( "play (null audio fallback): %#", string( path ) );
			return true;
		}

		const string requestedPath = string( path );

		engine::getTaskManager()
			.emplaceTask(
				"XAudio2Play",
				SW_DELEGATE_METHOD( TaskArgsDelegate, &XAudio2System::playDecodedClipTask, this ),
				MakeTaskArgs( abs, loop, requestedPath ) )
			.submit();

		if ( loop )
			_impl->_musicPath = requestedPath;

		return true;
#else
		(void)loop;
		_impl->_musicPath = string( path );
		SW_LOG_TRACE( "play (null): %#", string( path ) );
		return true;
#endif
	}
} // namespace sw
