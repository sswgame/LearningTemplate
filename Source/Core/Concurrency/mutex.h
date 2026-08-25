/**
 * @file mutex.h
 * @brief Deadlock 탐지가 내장된 커스텀 뮤텍스 래퍼
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/DeadlockDetector.h"

#include <mutex>

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) mutex — std::mutex 래퍼, 디버그에서 데드락 사이클 탐지
	//    조건 변수용 getStdMutex 는 가급적 쓰지 말 것
	// ------------------------------------------------------------------------------
	/**
	 * @brief std::mutex를 래핑하여 디버그 모드에서 데드락 사이클을 탐지합니다.
	 */
	class SW_API mutex
	{
	public:
		/** @brief 내부 std::mutex 만 준비합니다. */
		mutex() = default;
		/** @brief 잠금이 풀린 뒤 파괴되어야 합니다. */
		~mutex() = default;

		/** @brief 복사를 금지합니다. */
		mutex( const mutex& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		mutex& operator=( const mutex& ) = delete;

		/** @brief 락을 획득할 때까지 대기합니다. */
		void lock()
		{
			notifyIntended();
			_mutex.lock();
			notifyAcquired();
		}

		/** @brief 락을 즉시 시도합니다. 성공하면 true 입니다. */
		bool try_lock()
		{
			if ( _mutex.try_lock() == false )
				return false;

			notifyAcquired();
			return true;
		}

		/** @brief 보유한 락을 해제합니다. */
		void unlock()
		{
			notifyReleased();
			_mutex.unlock();
		}

		/** @brief 조건 변수 등에 넘길 내부 std::mutex 입니다. 가급적 사용을 자제하세요. */
		std::mutex& getStdMutex() { return _mutex; }

	private:
#if defined( SW_ENABLE_DEADLOCK_DETECTION )
		/**
		 * @brief 탐지기 내부(로깅·심볼화)가 다시 mutex 를 잡을 때 자기 자신을 재진입하지 않게 막습니다.
		 * @details 이게 없으면 dumpDeadlock → SW_LOG_ERROR → Logger 의 mutex → recordLockIntended 로 탐지기 뮤텍스가 자기 잠금됩니다.
		 */
		struct ReentryGuard
		{
			static bool& flag()
			{
				static thread_local bool t_bInHook = false;
				return t_bInHook;
			}

			ReentryGuard()
				: _bEntered{ flag() == false }
			{
				if ( _bEntered )
					flag() = true;
			}

			~ReentryGuard()
			{
				if ( _bEntered )
					flag() = false;
			}

			explicit operator bool() const { return _bEntered; }

			bool _bEntered;
		};

		void notifyIntended()
		{
			ReentryGuard guard;
			if ( guard )
			{
				DeadlockDetector* pDetector = DeadlockDetector::getActive();
				if ( pDetector != nullptr )
					pDetector->recordLockIntended( this );
			}
		}

		void notifyAcquired()
		{
			ReentryGuard guard;
			if ( guard )
			{
				DeadlockDetector* pDetector = DeadlockDetector::getActive();
				if ( pDetector != nullptr )
					pDetector->recordLockAcquired( this );
			}
		}

		void notifyReleased()
		{
			ReentryGuard guard;
			if ( guard )
			{
				DeadlockDetector* pDetector = DeadlockDetector::getActive();
				if ( pDetector != nullptr )
					pDetector->recordLockReleased( this );
			}
		}
#else
		void notifyIntended() {}
		void notifyAcquired() {}
		void notifyReleased() {}
#endif

		std::mutex _mutex;
	};

} // namespace sw
