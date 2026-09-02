/**
 * @file ConcurrentQueue.h
 * @brief 뮤텍스 기반 스레드 안전 큐
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/array.h"
#include "Core/Container/vector.h"

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
			atomic<uint32> _sequence{ 0 };
			T			   _data{};
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
			Cell*  pCell{ nullptr };
			uint32 pos = _enqueuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				pCell			 = &_arrBuffer[pos & kMask];
				uint32		seq	 = pCell->_sequence.load( std::memory_order_acquire );
				const int32 diff = static_cast<int32>( seq - pos );

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

			pCell->_data = item;
			pCell->_sequence.store( pos + 1, std::memory_order_release );
			return true;
		}

		/** @brief 이동으로 요소를 넣습니다. 가득 차면 false. */
		SW_INLINE bool enqueue( T&& item )
		{
			Cell*  pCell{ nullptr };
			uint32 pos = _enqueuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				pCell			 = &_arrBuffer[pos & kMask];
				uint32		seq	 = pCell->_sequence.load( std::memory_order_acquire );
				const int32 diff = static_cast<int32>( seq - pos );

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

			pCell->_data = std::move( item );
			pCell->_sequence.store( pos + 1, std::memory_order_release );
			return true;
		}

		/** @brief 앞에서 요소를 꺼냅니다. 비어 있으면 false. */
		SW_INLINE bool dequeue( T& outItem )
		{
			Cell*  pCell{ nullptr };
			uint32 pos = _dequeuePos.load( std::memory_order_relaxed );

			for ( ;; )
			{
				pCell			 = &_arrBuffer[pos & kMask];
				uint32		seq	 = pCell->_sequence.load( std::memory_order_acquire );
				const int32 diff = static_cast<int32>( seq - ( pos + 1 ) );

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

			outItem = std::move( pCell->_data );
			pCell->_sequence.store( pos + kMask + 1, std::memory_order_release );
			return true;
		}

		/** @brief push 별칭입니다 (enqueue 호출). */
		SW_INLINE bool push( const T& item ) { return enqueue( item ); }
		/** @brief push 별칭입니다 (enqueue 이동 호출). */
		SW_INLINE bool push( T&& item ) { return enqueue( std::move( item ) ); }

		/** @brief pop 별칭입니다 (dequeue 호출). */
		SW_INLINE bool pop( T& outItem ) { return dequeue( outItem ); }

		/** @brief 큐에 대기 중인 모든 항목을 vector에 드레인합니다. */
		uint32 drain( vector<T>& outList )
		{
			uint32 drainCount = 0;
			T	   item{};
			while ( dequeue( item ) )
			{
				outList.push_back( std::move( item ) );
				++drainCount;
			}
			return drainCount;
		}

		/** @brief 큐에 대기 중인 항목을 버퍼에 일괄 드레인합니다. */
		uint32 drain( T* pOutBuffer, uint32 maxCount )
		{
			if ( pOutBuffer == nullptr || maxCount == 0 )
				return 0;

			uint32 drainCount = 0;
			while ( drainCount < maxCount )
			{
				if ( dequeue( pOutBuffer[drainCount] ) == false )
					break;
				++drainCount;
			}
			return drainCount;
		}

		/** @brief 큐에 남아 있는 모든 요소를 비웁니다. */
		void clear()
		{
			T dummy{};
			while ( dequeue( dummy ) ) {}
		}

		/** @brief 대략적인 현재 요소 수를 반환합니다. */
		SW_INLINE uint32 size() const
		{
			const uint32 head = _dequeuePos.load( std::memory_order_relaxed );
			const uint32 tail = _enqueuePos.load( std::memory_order_relaxed );
			return ( tail - head );
		}

		/** @brief getCount 별칭입니다 (size 호출). */
		SW_INLINE uint32 getCount() const { return size(); }

		/** @brief 비어 있는지 반환합니다. */
		SW_INLINE bool empty() const { return size() == 0; }
		/** @brief isEmpty 별칭입니다 (empty 호출). */
		SW_INLINE bool isEmpty() const { return empty(); }

		/** @brief 현재 용량을 반환합니다. */
		constexpr uint32 capacity() const { return Capacity; }

	private:
		static constexpr uint32 kMask = Capacity - 1;

		// sw::array 대신 std::array 사용:
		// sw::array는 단일 스레드 컨테이너로 DataRaceDetector가 내장되어 있어 멀티스레드 동시 접근 시 오탐(Data Race Error)을 유발합니다.
		// ConcurrentQueue는 각 슬롯(Cell)의 원자적 sequence 변수로 락-프리 동기화를 수행하므로 레이스 탐지기가 없는 std::array를 사용합니다.
		std::array<Cell, Capacity> _arrBuffer{};
		alignas( 64 ) atomic<uint32> _enqueuePos{ 0 };
		alignas( 64 ) atomic<uint32> _dequeuePos{ 0 };
	};
} // namespace sw
