/**
 * @file XAudio2System.h
 * @brief 오디오 퍼사드 (Windows에서 XAudio2 + Media Foundation 디코드).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioSystem.h"

namespace sw
{

	struct XAudio2SystemImpl;

	/**
	 * @class XAudio2System
	 * @brief initialize / update / play (Windows에서 WAV PCM 또는 MF 디코드 MP3).
	 */
	class SW_API XAudio2System : public IAudioSystem
	{
	public:
		/** @brief 빈 오디오 시스템. initialize 전에 쓰지 마세요. */
		XAudio2System();
		/** @brief 오디오 시스템을 해제합니다. */
		~XAudio2System() override;

		/** @brief 복사를 금지합니다. */
		XAudio2System( const XAudio2System& ) = delete;
		/** @brief 대입을 금지합니다. */
		XAudio2System& operator=( const XAudio2System& ) = delete;

		/** @brief 오디오 백엔드를 초기화합니다. */
		bool initialize() override;
		/** @brief 오디오 백엔드를 종료합니다. */
		void shutdown() override;
		/** @brief 재생이 끝난 보이스를 정리합니다. */
		void update( float32 deltaSeconds ) override;

		/** @brief 리소스 상대/절대 경로를 해석해 한 번 재생합니다 (SFX). */
		bool play( string_view path ) override;

		/** @brief BGM을 루프 재생합니다. 이전 루프 보이스를 멈춥니다. 같은 경로는 재시작하지 않습니다. */
		bool playMusic( string_view path ) override;

		/** @brief 루프 음악 보이스를 멈춥니다. */
		void stopMusic() override;

		/** @brief 배경음악을 일시정지합니다. */
		void pauseMusic();
		/** @brief 일시정지된 배경음악을 재개합니다. */
		void resumeMusic();

		/** @brief 마스터 볼륨(0.0 ~ 1.0)을 설정합니다. */
		void setMasterVolume( float32 volume ) override;
		/** @brief 현재 마스터 볼륨을 반환합니다. */
		float32 getMasterVolume() const;

		/** @brief 배경음악(BGM) 볼륨(0.0 ~ 1.0)을 설정합니다. */
		void setMusicVolume( float32 volume ) override;
		/** @brief 현재 배경음악 볼륨을 반환합니다. */
		float32 getMusicVolume() const;

		/** @brief 효과음(SFX) 볼륨(0.0 ~ 1.0)을 설정합니다. */
		void setSfxVolume( float32 volume );
		/** @brief 현재 효과음 볼륨을 반환합니다. */
		float32 getSfxVolume() const;

		/** @brief 전체 음소거 여부를 설정합니다. */
		void setMute( bool bMute );
		/** @brief 현재 음소거 상태인지 반환합니다. */
		bool isMuted() const;

		/** @brief 초기화 여부를 반환합니다. */
		bool isInitialized() const;

	private:
		/** @brief 경로를 해석해 보이스를 재생합니다. */
		bool playInternal( string_view path, bool loop );

	private:
		unique_ptr<XAudio2SystemImpl> _impl;
	};
} // namespace sw
