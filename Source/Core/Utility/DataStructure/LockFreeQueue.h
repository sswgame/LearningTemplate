#pragma once
/**
 * @file LockFreeQueue.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

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

		bool empty() const
		{
			return _head.load( std::memory_order_relaxed ) == _tail.load( std::memory_order_relaxed );
		}

		uint32 size() const
		{
			const uint32 head = _head.load( std::memory_order_relaxed );
			const uint32 tail = _tail.load( std::memory_order_relaxed );
			return tail >= head ? ( tail - head ) : 0;
		}

		constexpr uint32 capacity() const { return Capacity; }

	private:
		static constexpr uint32 Mask = Capacity - 1;

		std::array<T, Capacity> _buffer{};
		alignas( 64 ) std::atomic<uint32> _head{ 0 };
		alignas( 64 ) std::atomic<uint32> _tail{ 0 };
	};
}
