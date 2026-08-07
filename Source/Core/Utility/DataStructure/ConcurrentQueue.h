#pragma once
/**
 * @file ConcurrentQueue.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{

	template <typename T, uint32 Capacity = 1024>
	class ConcurrentQueue
	{
		static_assert( ( Capacity & ( Capacity - 1 ) ) == 0, "Capacity must be a power of 2!" );

	private:
		struct alignas( 64 ) Cell
		{
			std::atomic<uint32> _sequence{ 0 };
			T					_data{};
		};

	public:
		ConcurrentQueue()
		{
			for ( uint32 i = 0; i < Capacity; ++i )
			{
				_buffer[i]._sequence.store( i, std::memory_order_relaxed );
			}
			_enqueuePos.store( 0, std::memory_order_relaxed );
			_dequeuePos.store( 0, std::memory_order_relaxed );
		}

		~ConcurrentQueue() = default;

		SW_INLINE bool enqueue( const T& item )
		{
			Cell*  cell;
			uint32 pos = _enqueuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				cell		  = &_buffer[pos & Mask];
				uint32	 seq  = cell->_sequence.load( std::memory_order_acquire );
				intptr_t diff = static_cast<intptr_t>( seq ) - static_cast<intptr_t>( pos );

				if ( diff == 0 )
				{
					if ( _enqueuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
					{
						break;
					}
				}
				else if ( diff < 0 )
				{
					return false;
				}
				else
				{
					pos = _enqueuePos.load( std::memory_order_relaxed );
				}
			}

			cell->_data = item;
			cell->_sequence.store( pos + 1, std::memory_order_release );
			return true;
		}

		SW_INLINE bool enqueue( T&& item )
		{
			Cell*  cell;
			uint32 pos = _enqueuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				cell		  = &_buffer[pos & Mask];
				uint32	 seq  = cell->_sequence.load( std::memory_order_acquire );
				intptr_t diff = static_cast<intptr_t>( seq ) - static_cast<intptr_t>( pos );

				if ( diff == 0 )
				{
					if ( _enqueuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
					{
						break;
					}
				}
				else if ( diff < 0 )
				{
					return false;
				}
				else
				{
					pos = _enqueuePos.load( std::memory_order_relaxed );
				}
			}

			cell->_data = std::move( item );
			cell->_sequence.store( pos + 1, std::memory_order_release );
			return true;
		}

		SW_INLINE bool dequeue( T& outItem )
		{
			Cell*  cell;
			uint32 pos = _dequeuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				cell		  = &_buffer[pos & Mask];
				uint32	 seq  = cell->_sequence.load( std::memory_order_acquire );
				intptr_t diff = static_cast<intptr_t>( seq ) - static_cast<intptr_t>( pos + 1 );

				if ( diff == 0 )
				{
					if ( _dequeuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
					{
						break;
					}
				}
				else if ( diff < 0 )
				{
					return false;
				}
				else
				{
					pos = _dequeuePos.load( std::memory_order_relaxed );
				}
			}

			outItem = cell->_data;
			cell->_sequence.store( pos + Mask + 1, std::memory_order_release );
			return true;
		}

		SW_INLINE uint32 size() const
		{
			uint32 head = _dequeuePos.load( std::memory_order_relaxed );
			uint32 tail = _enqueuePos.load( std::memory_order_relaxed );
			return tail >= head ? ( tail - head ) : 0;
		}

		SW_INLINE bool empty() const
		{
			return size() == 0;
		}

		constexpr uint32 capacity() const { return Capacity; }

	private:
		static constexpr uint32 Mask = Capacity - 1;

		std::array<Cell, Capacity> _buffer{};
		alignas( 64 ) std::atomic<uint32> _enqueuePos{ 0 };
		alignas( 64 ) std::atomic<uint32> _dequeuePos{ 0 };
	};
}
