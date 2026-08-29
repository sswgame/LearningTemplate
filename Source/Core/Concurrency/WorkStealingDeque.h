/**
 * @file WorkStealingDeque.h
 * @brief Chase-Lev Lock-Free Work-Stealing Deque
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Memory/Memory.h"

#include <memory>

namespace sw
{
	/**
	 * @class WorkStealingDeque
	 * @brief 소유자 스레드는 LIFO(push/pop), 다른 스레드는 FIFO(steal)로 접근하는 Lock-Free Deque
	 */
	template <typename T>
	class WorkStealingDeque
	{
	public:
		explicit WorkStealingDeque( size_t capacity = 1024 )
		{
			// capacity must be power of 2
			while ( capacity & ( capacity - 1 ) )
				capacity &= capacity - 1;
			if ( capacity < 8 )
				capacity = 8;

			_capacityMask = capacity - 1;
			_pBuffer	  = sw_new atomic<T>[capacity];
			_top.store( 0, std::memory_order_relaxed );
			_bottom.store( 0, std::memory_order_relaxed );
		}

		~WorkStealingDeque()
		{
			sw_delete_array( _pBuffer );
		}

		WorkStealingDeque( const WorkStealingDeque& )			 = delete;
		WorkStealingDeque& operator=( const WorkStealingDeque& ) = delete;

		bool push( T item )
		{
			uint64 b = _bottom.load( std::memory_order_relaxed );
			uint64 t = _top.load( std::memory_order_acquire );

			if ( b - t > _capacityMask )
			{
				// 큐가 가득 찼으면 (간단한 구현에서는 resize하지 않고 그냥 무시 또는 assert)
				// 실제 엔진에서는 resize 로직이 필요하지만 여기서는 생략.
				return false;
			}

			_pBuffer[b & _capacityMask].store( item, std::memory_order_relaxed );

			std::atomic_thread_fence( std::memory_order_release );
			_bottom.store( b + 1, std::memory_order_relaxed );
			return true;
		}

		bool pop( T& outItem )
		{
			uint64 b = _bottom.load( std::memory_order_relaxed );
			if ( b == 0 )
				return false;

			b -= 1;
			_bottom.store( b, std::memory_order_relaxed );

			std::atomic_thread_fence( std::memory_order_seq_cst );

			uint64 t = _top.load( std::memory_order_relaxed );

			if ( t <= b )
			{
				outItem = _pBuffer[b & _capacityMask].load( std::memory_order_relaxed );
				if ( t == b )
				{
					// 마지막 남은 하나
					if ( _top.compare_exchange_strong( t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed ) == false )
					{
						// steal 당함
						_bottom.store( b + 1, std::memory_order_relaxed );
						return false;
					}
					_bottom.store( b + 1, std::memory_order_relaxed );
				}
				return true;
			}
			else
			{
				_bottom.store( b + 1, std::memory_order_relaxed );
				return false;
			}
		}

		bool steal( T& outItem )
		{
			uint64 t = _top.load( std::memory_order_acquire );
			std::atomic_thread_fence( std::memory_order_seq_cst );
			uint64 b = _bottom.load( std::memory_order_acquire );

			if ( t < b )
			{
				outItem = _pBuffer[t & _capacityMask].load( std::memory_order_relaxed );
				if ( _top.compare_exchange_strong( t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed ) == false )
				{
					return false;
				}
				return true;
			}
			return false;
		}

	private:
		atomic<uint64> _top;
		atomic<uint64> _bottom;
		atomic<T>*	   _pBuffer;
		uint64		   _capacityMask;
	};
} // namespace sw
