/**
 * @file AudioSystem.cpp
 * @brief XAudio2 master-voice init (Windows) / null stub.
 */
#include "AudioSystem.h"
#include "Core/Utility/Log/Logger.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformHeaders.h"
	#include <xaudio2.h>
#endif

namespace sw
{
	AudioSystem::AudioSystem()
		: _bInitialized{ 0 }
		, _bComInitialized{ 0 }
		, _reserved{ 0 }
	{
	}

	AudioSystem::~AudioSystem()
	{
		shutdown();
	}

	bool AudioSystem::initialize()
	{
		if ( _bInitialized != 0 )
			return true;

#if defined( SW_PLATFORM_WINDOWS )
		HRESULT hr = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
		if ( hr == S_OK || hr == S_FALSE )
			_bComInitialized = 1;
		else if ( hr == RPC_E_CHANGED_MODE )
			_bComInitialized = 0; // COM already initialized differently — continue
		else
		{
			SW_LOG_WARNING( "[AudioSystem] CoInitializeEx failed (0x%#).", static_cast<uint32>( hr ) );
		}

		IXAudio2* xaudio = nullptr;
		hr				 = XAudio2Create( &xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR );
		if ( FAILED( hr ) || xaudio == nullptr )
		{
			SW_LOG_WARNING( "[AudioSystem] XAudio2Create failed (0x%#). Running null audio.", static_cast<uint32>( hr ) );
			if ( _bComInitialized != 0 )
			{
				CoUninitialize();
				_bComInitialized = 0;
			}
			_bInitialized = 1;
			return true;
		}

		IXAudio2MasteringVoice* master = nullptr;
		hr							   = xaudio->CreateMasteringVoice( &master );
		if ( FAILED( hr ) || master == nullptr )
		{
			SW_LOG_WARNING( "[AudioSystem] CreateMasteringVoice failed (0x%#).", static_cast<uint32>( hr ) );
			xaudio->Release();
			if ( _bComInitialized != 0 )
			{
				CoUninitialize();
				_bComInitialized = 0;
			}
			_bInitialized = 1;
			return true;
		}

		_xaudio		= xaudio;
		_masterVoice = master;
		SW_LOG_INFO( "[AudioSystem] XAudio2 mastering voice ready." );
#else
		SW_LOG_INFO( "[AudioSystem] Null audio backend." );
#endif

		_bInitialized = 1;
		return true;
	}

	void AudioSystem::shutdown()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( _masterVoice != nullptr )
		{
			static_cast<IXAudio2MasteringVoice*>( _masterVoice )->DestroyVoice();
			_masterVoice = nullptr;
		}
		if ( _xaudio != nullptr )
		{
			static_cast<IXAudio2*>( _xaudio )->Release();
			_xaudio = nullptr;
		}
		if ( _bComInitialized != 0 )
		{
			CoUninitialize();
			_bComInitialized = 0;
		}
#endif
		_bInitialized = 0;
		SW_LOG_INFO( "[AudioSystem] Shut down." );
	}

	void AudioSystem::update( float32 )
	{
		// Future: voice pooling / streaming pump.
	}

	bool AudioSystem::playCue( std::string_view path )
	{
		if ( _bInitialized == 0 )
			return false;
		SW_LOG_INFO( "[AudioSystem] playCue (stub): %#", std::string( path ).c_str() );
		return _xaudio != nullptr || _bInitialized != 0;
	}
} // namespace sw
