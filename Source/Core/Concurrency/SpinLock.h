/**
 * @file SpinLock.h
 * @brief 짧은 임계 구역용 TTAS 스핀락.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) SpinLock — 짧은 구간만. 길면 sw::mutex
	//    TTAS(Test and Test-And-Set) 및 sw::cpuPause()로 이기종 CPU 파이프라인 양보
	// ------------------------------------------------------------------------------
	/**
	 * @class SpinLock
	 * @brief std::atomic<bool> 기반 TTAS 스핀락. 매우 짧은 임계 구역에서 mutex 대신 씁니다.
	 */
	class SpinLock
	{
	public:
		/** @brief 플래그를 해제 상태로 둡니다. */
		SpinLock() = default;
		/** @brief 잠금이 풀린 뒤 파괴되어야 합니다. */
		~SpinLock() = default;

		/** @brief 복사를 금지합니다. */
		SpinLock( const SpinLock& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		SpinLock& operator=( const SpinLock& ) = delete;

		/** @brief 락을 획득할 때까지 스핀합니다 (TTAS 패턴 및 sw::cpuPause 파이프라인 양보). */
		void lock()
		{
			while ( _locked.exchange( true, std::memory_order_acquire ) )
			{
				while ( _locked.load( std::memory_order_relaxed ) )
				{
					sw::cpuPause();
				}
			}
		}

		/** @brief 락 획득을 즉시 시도합니다. 성공하면 true 입니다. */
		bool try_lock()
		{
			return ( _locked.load( std::memory_order_relaxed ) == false ) &&
				   ( _locked.exchange( true, std::memory_order_acquire ) == false );
		}

		/** @brief 보유한 락을 해제합니다. */
		void unlock()
		{
			_locked.store( false, std::memory_order_release );
		}

		/** @brief 현재 잠금 상태인지 확인합니다. */
		bool isLocked() const
		{
			return _locked.load( std::memory_order_relaxed );
		}

	private:
		atomic<bool> _locked{ false };
	};

} // namespace sw
