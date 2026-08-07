#pragma once
/**                                                                                                                                             \
 * @file LockFreeObjectPool.h                                                                                                                   \
 * @brief 뮤텍스를 사용하지 않고 원자적 락-프리 큐를 기반으로 동작하는 객체 풀(Object Pool) 선언입니다.       \
 * @details 렌더링 스레드와 워커 스레드 간의 빈번한 객체 생성/해제 시 병목을 줄이기 위해 설계되었습니다. \
 */                                                                                                                                             \
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

#include "Core/Utility/DataStructure/LockFreeQueue.h"

namespace sw
{
	/**
	 * @class LockFreeObjectPool
	 * @brief 템플릿 기반 락-프리 오브젝트 풀입니다. 최대 Capacity개까지의 객체를 스레드 안전하게 재사용합니다.
	 * @tparam T 풀링할 객체 타입
	 * @tparam Capacity 풀에 사전 할당할 최대 객체 수
	 */
	template <typename T, uint32 Capacity = 512>
	class LockFreeObjectPool
	{
	public:
		LockFreeObjectPool()
		{
			for ( uint32 i = 0; i < Capacity; ++i )
			{
				T* ptr = reinterpret_cast<T*>( &_storage[i * sizeof( T )] );
				_freeQueue.push( ptr );
			}
		}

		~LockFreeObjectPool() = default;

		/**
		 * @brief 풀에서 유휴(Free) 객체 하나를 가져와 생성자(Placement New)를 호출합니다.
		 * @tparam Args 생성자에 전달할 가변 인자 타입들
		 * @param args 생성자에 전달할 가변 인자 리스트
		 * @return 사용 가능한 객체 포인터 (풀이 가득 찼다면 nullptr 반환)
		 */
		template <typename... Args>
		SW_INLINE T* acquire( Args&&... args )
		{
			T* ptr = nullptr;
			if ( _freeQueue.pop( ptr ) && ptr != nullptr )
			{
				new ( ptr ) T( std::forward<Args>( args )... );
				_activeCount.fetch_add( 1, std::memory_order_relaxed );
				return ptr;
			}
			return nullptr;
		}

		SW_INLINE void release( T* ptr )
		{
			if ( ptr == nullptr )
				return;

			ptr->~T();
			_freeQueue.push( ptr );
			_activeCount.fetch_sub( 1, std::memory_order_relaxed );
		}

		SW_INLINE uint32 getActiveCount() const
		{
			return _activeCount.load( std::memory_order_relaxed );
		}

		SW_INLINE uint32 getAvailableCount() const
		{
			uint32 active = getActiveCount();
			return Capacity >= active ? ( Capacity - active ) : 0;
		}

		constexpr uint32 capacity() const { return Capacity; }

	private:
		alignas( alignof( T ) ) std::array<uint8, Capacity * sizeof( T )> _storage;
		LockFreeQueue<T*, Capacity> _freeQueue;
		std::atomic<uint32>			_activeCount{ 0 };
	};
} // namespace sw
