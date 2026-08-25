#include "pch.h"

#include "Engine/Audio/IAudioSystem.h"

namespace sw
{
	/**
	 * @class NullAudioSystem
	 * @brief 오디오 출력이 지원되지 않는 플랫폼을 위한 더미 오디오 시스템입니다.
	 */
	class NullAudioSystem : public IAudioSystem
	{
	public:
		virtual ~NullAudioSystem() = default;

		bool initialize() override
		{
			SW_LOG_INFO( "[NullAudioSystem] Initialized." );
			return true;
		}

		void shutdown() override
		{
			SW_LOG_INFO( "[NullAudioSystem] Shut down." );
		}

		void update( float32 ) override {}

		bool play( string_view path ) override
		{
			SW_LOG_INFO( "[NullAudioSystem] play: %#", string( path ) );
			return true;
		}

		bool playMusic( string_view path ) override
		{
			SW_LOG_INFO( "[NullAudioSystem] playMusic: %#", string( path ) );
			return true;
		}

		void stopMusic() override {}
		void setMasterVolume( float32 ) override {}
		void setMusicVolume( float32 ) override {}
	};
} // namespace sw
