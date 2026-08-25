/**
 * @file ConcurrentQueue.h
 * @brief 뮤텍스 기반 스레드 안전 큐
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"

#include <array>

namespace sw
{

	/**
	 * @brief 고정 용량 다중 생산자/다중 소비자 큐 (시퀀스 넘버 기반)
	 * @tparam T 요소 타입
	 * @tparam Capacity 용량 (2의 거듭제곱)
	 */
	// ------------------------------------------------------------------------------
	// 1) ConcurrentQueue — MPMC 시퀀스 넘버 링. enqueue/dequeue 가 가득/빈 면 false
	// ------------------------------------------------------------------------------
	template <typename T, uint32 Capacity = 1024>
	/** @brief 고정 용량 다중 생산자/다중 소비자 큐입니다. */
	class ConcurrentQueue
	{
		static_assert( ( Capacity & ( Capacity - 1 ) ) == 0, "Capacity must be a power of 2!" );

		/** @brief 캐시라인 정렬된 슬롯. 시퀀스로 소유권을 넘깁니다. */
		struct alignas( 64 ) Cell
		{
			std::atomic<uint32> _sequence{ 0 };
			T					_data{};
		};

	public:
		/** @brief 각 슬롯 시퀀스를 인덱스로 두고 위치를 0으로 맞춥니다. */
		ConcurrentQueue()
		{
			for ( uint32 slotIndex = 0; slotIndex < Capacity; ++slotIndex )
			{
				_arrBuffer[slotIndex]._sequence.store( slotIndex, std::memory_order_relaxed );
			}
			_enqueuePos.store( 0, std::memory_order_relaxed );
			_dequeuePos.store( 0, std::memory_order_relaxed );
		}

		/** @brief 버퍼만 버리며 락은 없습니다. */
		~ConcurrentQueue() = default;

		/** @brief 복사로 요소를 넣습니다. 가득 차면 false. */
		SW_INLINE bool enqueue( const T& item )
		{
			Cell*  cell;
			uint32 pos = _enqueuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				cell		  = &_arrBuffer[pos & kMask];
				uint32	 seq  = cell->_sequence.load( std::memory_order_acquire );
				intptr_t diff = static_cast<intptr_t>( seq ) - static_cast<intptr_t>( pos );

				if ( diff == 0 )
				{
					if ( _enqueuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
						break;
				}
				else if ( diff < 0 )
					return false;
				else
					pos = _enqueuePos.load( std::memory_order_relaxed );
			}

			cell->_data = item;
			cell->_sequence.store( pos + 1, std::memory_order_release );
			return true;
		}

		/** @brief 이동으로 요소를 넣습니다. 가득 차면 false. */
		SW_INLINE bool enqueue( T&& item )
		{
			Cell*  cell;
			uint32 pos = _enqueuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				cell		  = &_arrBuffer[pos & kMask];
				uint32	 seq  = cell->_sequence.load( std::memory_order_acquire );
				intptr_t diff = static_cast<intptr_t>( seq ) - static_cast<intptr_t>( pos );

				if ( diff == 0 )
				{
					if ( _enqueuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
						break;
				}
				else if ( diff < 0 )
					return false;
				else
					pos = _enqueuePos.load( std::memory_order_relaxed );
			}

			cell->_data = std::move( item );
			cell->_sequence.store( pos + 1, std::memory_order_release );
			return true;
		}

		/** @brief 앞에서 요소를 꺼냅니다. 비어 있으면 false. */
		SW_INLINE bool dequeue( T& outItem )
		{
			Cell*  cell;
			uint32 pos = _dequeuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				cell		  = &_arrBuffer[pos & kMask];
				uint32	 seq  = cell->_sequence.load( std::memory_order_acquire );
				intptr_t diff = static_cast<intptr_t>( seq ) - static_cast<intptr_t>( pos + 1 );

				if ( diff == 0 )
				{
					if ( _dequeuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
						break;
				}
				else if ( diff < 0 )
					return false;
				else
					pos = _dequeuePos.load( std::memory_order_relaxed );
			}

			outItem = std::move( cell->_data );
			cell->_sequence.store( pos + kMask + 1, std::memory_order_release );
			return true;
		}

		/** @brief 대략적인 현재 요소 수를 반환합니다. */
		SW_INLINE uint32 size() const
		{
			const uint32 head = _dequeuePos.load( std::memory_order_relaxed );
			const uint32 tail = _enqueuePos.load( std::memory_order_relaxed );
			return ( tail - head );
		}

		/** @brief 비어 있는지 반환합니다. */
		SW_INLINE bool empty() const { return size() == 0; }

		/** @brief 현재 용량을 반환합니다. */
		constexpr uint32 capacity() const { return Capacity; }

	private:
		static constexpr uint32 kMask = Capacity - 1;

		// sw::array 대신 std::array 사용:
		// sw::array는 단일 스레드 컨테이너로 DataRaceDetector가 내장되어 있어 멀티스레드 동시 접근 시 오탐(Data Race Error)을 유발합니다.
		// ConcurrentQueue는 각 슬롯(Cell)의 원자적 sequence 변수로 락-프리 동기화를 수행하므로 레이스 탐지기가 없는 std::array를 사용합니다.
		std::array<Cell, Capacity> _arrBuffer{};
		alignas( 64 ) std::atomic<uint32> _enqueuePos{ 0 };
		alignas( 64 ) std::atomic<uint32> _dequeuePos{ 0 };
	};
} // namespace sw
