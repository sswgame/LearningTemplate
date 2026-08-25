/**
 * @file IAudioSystem.h
 * @brief 오디오 시스템의 추상 인터페이스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	/**
	 * @class IAudioSystem
	 * @brief 오디오 재생 및 관리를 위한 순수 가상 인터페이스
	 */
	class SW_API IAudioSystem
	{
	public:
		/** @brief 현재 플랫폼에 맞는 오디오 시스템 인스턴스를 생성합니다. */
		static unique_ptr<IAudioSystem> create();

		virtual ~IAudioSystem() = default;

		IAudioSystem()								   = default;
		IAudioSystem( const IAudioSystem& )			   = delete;
		IAudioSystem& operator=( const IAudioSystem& ) = delete;
		IAudioSystem( IAudioSystem&& )				   = delete;
		IAudioSystem& operator=( IAudioSystem&& )	   = delete;

		/** @brief 오디오 시스템을 초기화합니다. */
		virtual bool initialize() = 0;

		/** @brief 오디오 시스템을 해제합니다. */
		virtual void shutdown() = 0;

		/** @brief 프레임마다 오디오 시스템을 업데이트합니다. */
		virtual void update( float32 deltaSeconds ) = 0;

		/** @brief 단발성 효과음(SFX)을 비동기적으로 재생합니다. */
		virtual bool play( string_view path ) = 0;

		/** @brief 배경음악(BGM)을 루프로 재생합니다. */
		virtual bool playMusic( string_view path ) = 0;

		/** @brief 현재 재생 중인 배경음악(BGM)을 중지합니다. */
		virtual void stopMusic() = 0;

		/** @brief 전체 마스터 볼륨을 설정합니다 (0.0 ~ 1.0). */
		virtual void setMasterVolume( float32 volume ) = 0;

		/** @brief 배경음악(BGM) 볼륨을 설정합니다 (0.0 ~ 1.0). */
		virtual void setMusicVolume( float32 volume ) = 0;
	};
} // namespace sw
