/**
 * @file mutex.h
 * @brief Deadlock 탐지가 내장된 커스텀 뮤텍스 래퍼
 */
#pragma once
#include "Core/Common/Macros.h"

#include <mutex>

namespace sw
{
	class DeadlockDetector;

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
		void lock();

		/** @brief 락을 즉시 시도합니다. 성공하면 true 입니다. */
		bool try_lock();

		/** @brief 보유한 락을 해제합니다. */
		void unlock();

		/** @brief 조건 변수 등에 넘길 내부 std::mutex 입니다. 가급적 사용을 자제하세요. */
		std::mutex& getStdMutex() { return _mutex; }

	private:
		void notifyIntended();
		void notifyAcquired();
		void notifyReleased();

		std::mutex _mutex;
	};

} // namespace sw

#include "Core/Concurrency/DeadlockDetector.h"

namespace sw
{
#if defined( SW_ENABLE_DEADLOCK_DETECTION )
	namespace
	{
		struct MutexReentryGuardInternal
		{
			static bool& flag()
			{
				static thread_local bool t_bInHook = false;
				return t_bInHook;
			}
		};
	} // namespace

	struct MutexReentryGuard
	{
		MutexReentryGuard()
			: _bEntered{ MutexReentryGuardInternal::flag() == false }
		{
			if ( _bEntered )
				MutexReentryGuardInternal::flag() = true;
		}

		~MutexReentryGuard()
		{
			if ( _bEntered )
				MutexReentryGuardInternal::flag() = false;
		}

		explicit operator bool() const { return _bEntered; }

		bool _bEntered;
	};

	inline void mutex::notifyIntended()
	{
		MutexReentryGuard guard;
		if ( guard )
		{
			DeadlockDetector* pDetector = DeadlockDetector::getActive();
			if ( pDetector != nullptr )
				pDetector->recordLockIntended( this );
		}
	}

	inline void mutex::notifyAcquired()
	{
		MutexReentryGuard guard;
		if ( guard )
		{
			DeadlockDetector* pDetector = DeadlockDetector::getActive();
			if ( pDetector != nullptr )
				pDetector->recordLockAcquired( this );
		}
	}

	inline void mutex::notifyReleased()
	{
		MutexReentryGuard guard;
		if ( guard )
		{
			DeadlockDetector* pDetector = DeadlockDetector::getActive();
			if ( pDetector != nullptr )
				pDetector->recordLockReleased( this );
		}
	}
#else
	inline void mutex::notifyIntended() {}
	inline void mutex::notifyAcquired() {}
	inline void mutex::notifyReleased() {}
#endif

	inline void mutex::lock()
	{
		notifyIntended();
		_mutex.lock();
		notifyAcquired();
	}

	inline bool mutex::try_lock()
	{
		if ( _mutex.try_lock() == false )
			return false;

		notifyAcquired();
		return true;
	}

	inline void mutex::unlock()
	{
		notifyReleased();
		_mutex.unlock();
	}
} // namespace sw
