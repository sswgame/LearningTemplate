#pragma once
/**
 * @file AudioSystem.h
 * @brief Minimal audio facade (XAudio2 master voice on Windows; null elsewhere).
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @class AudioSystem
	 * @brief initialize / update / playCue stub. Does not load FMOD or wave assets yet.
	 */
	class SW_API AudioSystem
	{
	public:
		AudioSystem();
		~AudioSystem();

		AudioSystem( const AudioSystem& )			 = delete;
		AudioSystem& operator=( const AudioSystem& ) = delete;

		bool initialize();
		void shutdown();
		void update( float32 deltaSeconds );

		/** @brief Stub: logs the cue path; real wave decode/playback is future work. */
		bool playCue( std::string_view path );

		bool isInitialized() const { return _bInitialized != 0; }

	private:
		void*  _xaudio		= nullptr; // IXAudio2*
		void*  _masterVoice = nullptr; // IXAudio2MasteringVoice*
		uint8  _bInitialized : 1;
		uint8  _bComInitialized : 1;
		[[maybe_unused]] uint8 _reserved : 6;
	};
} // namespace sw
