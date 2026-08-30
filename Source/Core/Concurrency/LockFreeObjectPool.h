/**
 * @file DataStructure/LockFreeObjectPool.h
 * @brief 뮤텍스를 사용하지 않고 원자적 락-프리 큐를 기반으로 동작하는 객체 풀(Object Pool) 선언입니다.
 * @details 렌더링 스레드와 워커 스레드 간의 빈번한 객체 생성/해제 시 병목을 줄이기 위해 설계되었습니다.
 *
 * 내부 큐로 sw::ConcurrentQueue(다중 생산자·다중 소비자)를 사용합니다.
 *   - acquire(): 여러 스레드(소비자)가 동시에 호출 가능
 *   - release(): 여러 스레드(생산자)가 동시에 호출 가능
 *
 * 소유권 모델:
 *   - acquire()  → T* 반환 (nullptr이면 풀 소진)
 *   - release(T*&) → 소멸자 호출 후 풀 반납, 포인터를 nullptr로 초기화
 *     * 포인터를 참조로 받아 반납 후 자동으로 비움 → 소멸 후 사용 방지
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Container/array.h"

namespace sw
{
	/**
	 * @class LockFreeObjectPool
	 * @brief 템플릿 기반 MPMC 락-프리 오브젝트 풀입니다. 최대 Capacity개까지의 객체를 스레드 안전하게 재사용합니다.
	 * @tparam T 풀링할 객체 타입
	 * @tparam Capacity 풀에 사전 할당할 최대 객체 수 (2의 거듭제곱 권장 — ConcurrentQueue 요구사항)
	 */
	// ------------------------------------------------------------------------------
	// 1) LockFreeObjectPool — acquire(placement new) / release(~T + 큐 반납)
	//    Capacity 는 2의 거듭제곱. 소진 시 acquire 는 nullptr
	// ------------------------------------------------------------------------------
	template <typename T, uint32 Capacity = 512>
	/** @brief MPMC 락프리 오브젝트 풀. 스토리지는 고정 배열입니다. */
	class LockFreeObjectPool
	{
		static_assert( ( Capacity & ( Capacity - 1 ) ) == 0,
					   "LockFreeObjectPool Capacity must be a power of 2 (ConcurrentQueue requirement)" );

	public:
		/** @brief 스토리지 슬롯을 모두 유휴 큐에 넣습니다. */
		LockFreeObjectPool()
		{
			for ( uint32 poolIndex = 0; poolIndex < Capacity; ++poolIndex )
			{
				T* pPtr = reinterpret_cast<T*>( &_arrStorage[poolIndex * sizeof( T )] );
				_freeQueue.enqueue( pPtr );
			}
		}

		/**
		 * @brief 풀 소멸 전 모든 객체가 반납됐는지 검증합니다.
		 * @warning SW_DEBUG 빌드에서 누수 조기 발견용 assert를 수행합니다.
		 *          acquire된 채 소멸하면 T 소멸자가 호출되지 않습니다.
		 */
		~LockFreeObjectPool()
		{
			SW_ASSERT( getActiveCount() == 0 &&
					   "[LockFreeObjectPool] 아직 acquire된 객체가 있습니다. "
					   "풀 소멸 전에 모든 객체를 release 하세요." );
		}

		/** @brief 복사를 금지합니다. */
		LockFreeObjectPool( const LockFreeObjectPool& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		LockFreeObjectPool& operator=( const LockFreeObjectPool& ) = delete;

		/**
		 * @brief 풀에서 유휴(Free) 객체 하나를 가져와 생성자(Placement New)를 호출합니다.
		 * @tparam Args 생성자에 전달할 가변 인자 타입들
		 * @param args 생성자에 전달할 가변 인자 리스트
		 * @return 사용 가능한 객체 포인터 (풀이 가득 찼다면 nullptr 반환)
		 */
		template <typename... Args>
		SW_INLINE T* acquire( Args&&... args )
		{
			T* pPtr{ nullptr };
			if ( _freeQueue.dequeue( pPtr ) && pPtr != nullptr )
			{
				new ( pPtr ) T( std::forward<Args>( args )... );
				_activeCount.fetch_add( 1, std::memory_order_relaxed );
				return pPtr;
			}
			return nullptr;
		}

		/**
		 * @brief 객체 소멸자를 호출하고 풀에 메모리를 반납합니다. 포인터를 nullptr로 초기화합니다.
		 * @param pPtr 반납할 객체 포인터 (참조로 받아 반납 후 nullptr로 만듦)
		 * @note release 후 pPtr은 반드시 nullptr이 됩니다. 소멸 후 사용을 방지합니다.
		 */
		SW_INLINE void release( T*& pPtr )
		{
			if ( pPtr == nullptr )
				return;

			pPtr->~T();
			_freeQueue.enqueue( pPtr );
			_activeCount.fetch_sub( 1, std::memory_order_relaxed );
			pPtr = nullptr; // 호출자가 실수로 사용하지 못하도록 즉시 무효화
		}

		/** @brief 현재 acquire된 객체 수를 반환합니다. */
		SW_INLINE uint32 getActiveCount() const { return _activeCount.load( std::memory_order_relaxed ); }

		/** @brief 풀에 남은 유휴 객체 수를 반환합니다. */
		SW_INLINE uint32 getAvailableCount() const
		{
			uint32 active = getActiveCount();
			return Capacity >= active ? ( Capacity - active ) : 0;
		}

		/** @brief 현재 용량을 반환합니다. */
		constexpr uint32 capacity() const { return Capacity; }

	private:
		alignas( alignof( T ) ) std::array<uint8, Capacity * sizeof( T )> _arrStorage;
		ConcurrentQueue<T*, Capacity> _freeQueue; ///< 다중 생산자·다중 소비자 — acquire/release 스레드 안전
		atomic<uint32>				  _activeCount{ 0 };
	};
} // namespace sw
