/**
 * @file NullAudioSystem.h
 * @brief 오디오 출력이 지원되지 않는 플랫폼을 위한 더미 구현
 */
#pragma once
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Resource/ResourceUtil.h"

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
			SW_LOG_INFO( "Initialized." );
			return true;
		}

		void shutdown() override
		{
			SW_LOG_INFO( "Shut down." );
		}

		void update( float32 ) override {}

		bool play( string_view path ) override
		{
			if ( path.empty() || ResourceUtil::hasResource( path ) == false )
				return false;
			SW_LOG_TRACE( "play: %#", string( path ) );
			return true;
		}

		bool playMusic( string_view path ) override
		{
			if ( path.empty() || ResourceUtil::hasResource( path ) == false )
				return false;
			SW_LOG_TRACE( "playMusic: %#", string( path ) );
			return true;
		}

		void stopMusic() override {}
		void setMasterVolume( float32 ) override {}
		void setMusicVolume( float32 ) override {}
	};
} // namespace sw
