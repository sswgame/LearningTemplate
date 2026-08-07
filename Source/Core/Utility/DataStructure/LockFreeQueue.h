#pragma once
/**
 * @file LockFreeQueue.h
 * @brief 고정 용량 lock-free 링 버퍼 큐
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	/**
	 * @brief 단일 생산자/단일 소비자에 적합한 고정 용량 lock-free 링 버퍼
	 * @tparam T 요소 타입
	 * @tparam Capacity 용량 (2의 거듭제곱)
	 */
	template <typename T, uint32 Capacity = 1024>
	class LockFreeQueue
	{
		static_assert( ( Capacity & ( Capacity - 1 ) ) == 0, "Capacity must be a power of 2!" );

	public:
		LockFreeQueue()
			: _head( 0 )
			, _tail( 0 )
		{
		}

		~LockFreeQueue() = default;

		/** @brief 복사로 요소를 넣습니다. 가득 차면 false. */
		bool push( const T& item )
		{
			const uint32 currentTail = _tail.load( std::memory_order_relaxed );
			const uint32 currentHead = _head.load( std::memory_order_acquire );

			if ( currentTail - currentHead >= Capacity )
			{
				return false;
			}

			_buffer[currentTail & Mask] = item;
			_tail.store( currentTail + 1, std::memory_order_release );
			return true;
		}

		/** @brief 이동으로 요소를 넣습니다. 가득 차면 false. */
		bool push( T&& item )
		{
			const uint32 currentTail = _tail.load( std::memory_order_relaxed );
			const uint32 currentHead = _head.load( std::memory_order_acquire );

			if ( currentTail - currentHead >= Capacity )
			{
				return false;
			}

			_buffer[currentTail & Mask] = std::move( item );
			_tail.store( currentTail + 1, std::memory_order_release );
			return true;
		}

		/** @brief 앞에서 요소를 꺼냅니다. 비어 있으면 false. */
		bool pop( T& outItem )
		{
			const uint32 currentHead = _head.load( std::memory_order_relaxed );
			const uint32 currentTail = _tail.load( std::memory_order_acquire );

			if ( currentHead == currentTail )
			{
				return false;
			}

			outItem = _buffer[currentHead & Mask];
			_head.store( currentHead + 1, std::memory_order_release );
			return true;
		}

		/** @brief 비어 있는지 반환합니다. */
		bool empty() const
		{
			return _head.load( std::memory_order_relaxed ) == _tail.load( std::memory_order_relaxed );
		}

		/** @brief 현재 요소 수를 반환합니다. */
		uint32 size() const
		{
			const uint32 head = _head.load( std::memory_order_relaxed );
			const uint32 tail = _tail.load( std::memory_order_relaxed );
			return tail >= head ? ( tail - head ) : 0;
		}

		/** @brief 고정 용량을 반환합니다. */
		constexpr uint32 capacity() const { return Capacity; }

	private:
		static constexpr uint32 Mask = Capacity - 1;

		std::array<T, Capacity> _buffer{};
		alignas( 64 ) std::atomic<uint32> _head{ 0 };
		alignas( 64 ) std::atomic<uint32> _tail{ 0 };
	};
}
