/**
 * @file AnimPlayer.h
 * @brief 두 AnimClip 사이 재생 / 크로스페이드.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Animation/AnimClip.h"

namespace sw
{
	/**
	 * @class AnimPlayer
	 * @brief 선형 크로스페이드를 가진 두 슬롯 클립 플레이어.
	 */
	class SW_API AnimPlayer
	{
	public:
		/** @brief 기본 플레이어입니다. */
		AnimPlayer() = default;

		/** @brief 클립을 즉시 재생합니다. */
		void play( const AnimClip* pClip, bool bLooping = true );
		/** @brief 클립으로 크로스페이드합니다. */
		void crossfade( const AnimClip* pClip, float32 fadeSeconds, bool bLooping = true );

		/** @brief 재생 시각과 페이드를 갱신합니다. */
		void update( float32 deltaSeconds );
		/** @brief 현재(또는 페이드 중 혼합) 샘플을 평가합니다. */
		AnimSample evaluate() const;

		/** @brief 재생 속도 배율을 설정합니다. (1.0 = 표준 속도, 0.0 = 일시정지, 2.0 = 2배속 등) */
		void setSpeed( float32 speed ) { _playSpeed = speed; }
		/** @brief 현재 재생 속도 배율을 반환합니다. */
		float32 getSpeed() const { return _playSpeed; }

		/** @brief 현재 클립을 반환합니다. */
		const AnimClip* getCurrentClip() const { return _pCurrent; }
		/** @brief 페이드 대상 클립을 반환합니다. */
		const AnimClip* getNextClip() const { return _pNext; }
		/** @brief 크로스페이드 중인지 반환합니다. */
		bool isCrossfading() const { return _fadeDuration > 0.0f && _pNext != nullptr; }

	private:
		const AnimClip* _pCurrent{ nullptr };
		const AnimClip* _pNext{ nullptr };
		float32			_currentTime{ 0.0f };
		float32			_nextTime{ 0.0f };
		float32			_fadeDuration{ 0.0f };
		float32			_fadeElapsed{ 0.0f };
		float32			_playSpeed{ 1.0f };
		bool			_bCurrentLoop{ true };
		bool			_bNextLoop{ true };
	};
} // namespace sw
