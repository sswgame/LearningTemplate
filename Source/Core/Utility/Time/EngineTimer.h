#pragma once
/**
 * @file EngineTimer.h
 * @brief 고해상도 타이머(QPC 등)를 이용하여 프레임 델타 타임 및 스코프(Scope) 단위 프로파일링을 지원하는 클래스들
 */
#include "Core/Common/Types.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{

	class SW_API CpuTimer
	{
	public:
		CpuTimer() noexcept;

		/**
		 * @brief 누적 시간을 반환합니다
		 */
		float32 getTotalTime() const noexcept;
		/**
		 * @brief 델타 타임을 반환합니다
		 */
		float32 getDeltaTime() const noexcept;

		/**
		 * @brief 타이머를 리셋합니다
		 */
		void resetTimer() noexcept;
		/**
		 * @brief 타이머를 시작합니다
		 */
		void startTimer() noexcept;
		/**
		 * @brief 타이머를 중지합니다
		 */
		void stopTimer() noexcept;
		/**
		 * @brief 타이머를 갱신합니다
		 */
		void updateTimer() noexcept;

		bool isStopped() const noexcept { return _bStopped; }

	private:
		float64 _secondsPerCount = 0.0;
		float64 _deltaTime		 = -1.0;

		int64 _baseTime	   = 0;
		int64 _pausedTime  = 0;
		int64 _stopTime	   = 0;
		int64 _prevTime	   = 0;
		int64 _currentTime = 0;

		bool _bStopped = false;
	};

	class SW_API ScopeTimer final
	{
	public:
		explicit ScopeTimer( const utf8* tag ) noexcept
			: _tag{ tag }
		{
			_timer.resetTimer();
			_timer.startTimer();
		}

		~ScopeTimer() noexcept
		{
			_timer.updateTimer();
			SW_LOG_INFO( "[ScopeTimer] '%#': %.4f ms", _tag, getElapsedTimeInSeconds() * 1000.0f );
		}

		float32 getElapsedTimeInSeconds() const noexcept
		{
			const_cast<CpuTimer&>( _timer ).updateTimer();
			return _timer.getDeltaTime();
		}

		ScopeTimer( const ScopeTimer& )			   = delete;
		ScopeTimer& operator=( const ScopeTimer& ) = delete;

	private:
		const utf8* _tag;
		CpuTimer	_timer;
	};
}

using CpuTimer	 = sw::CpuTimer;
using ScopeTimer = sw::ScopeTimer;
