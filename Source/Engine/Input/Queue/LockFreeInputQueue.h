/**
 * @file LockFreeInputQueue.h
 * @brief OS 메시지 펌프 및 비동기 스레드에서 메인 엔진 루프로 원시 입력을 전달하는 고성능 락프리 링버퍼
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include <atomic>
#include <type_traits>

namespace sw
{
	/**
	 * @class LockFreeInputQueue
	 * @brief 64바이트 캐시라인 패딩(False Sharing 방지) 및 Acquire-Release 원자적 메모리 오더링 기반 락프리 SPSC/MPSC 링버퍼
	 * @tparam T 보관할 패킷 자료형 (기본값: RawInputEvent)
	 * @tparam kCapacity 2의 거듭제곱 크기 버퍼 슬롯 수 (기본값: 2048)
	 */
	template <typename T, uint32 kCapacity = 2048>
	class LockFreeInputQueue
	{
		static_assert( ( kCapacity & ( kCapacity - 1 ) ) == 0, "kCapacity must be a power of 2 for fast masking" );
		static_assert( std::is_trivially_copyable_v<T>, "T must be trivially copyable for lock-free atomic buffer transport" );

		static constexpr uint32 kIndexMask = kCapacity - 1;

	public:
		LockFreeInputQueue()
			: _head{ 0 }
			, _arrPad0{}
			, _tail{ 0 }
			, _arrPad1{}
			, _arrBuffer{}
		{
		}

		~LockFreeInputQueue() = default;

		LockFreeInputQueue( const LockFreeInputQueue& )			   = delete;
		LockFreeInputQueue& operator=( const LockFreeInputQueue& ) = delete;

		/**
		 * @brief 큐의 뒤에 새 이벤트를 락 없이 밀어 넣습니다 (Producer).
		 * @return 큐가 가득 차지 않아 성공 시 true, 가득 찼으면 false
		 */
		bool push( const T& item )
		{
			const uint32 currentTail = _tail.load( std::memory_order_relaxed );
			const uint32 currentHead = _head.load( std::memory_order_acquire );

			if ( ( currentTail - currentHead ) >= kCapacity )
				return false; // Queue is full

			_arrBuffer[currentTail & kIndexMask] = item;
			_tail.store( currentTail + 1, std::memory_order_release );
			return true;
		}

		/**
		 * @brief 큐의 앞에서 이벤트를 하나 꺼냅니다 (Consumer).
		 * @return 꺼낼 항목이 있어 성공 시 true, 큐가 비어있으면 false
		 */
		bool pop( T& outItem )
		{
			const uint32 currentHead = _head.load( std::memory_order_relaxed );
			const uint32 currentTail = _tail.load( std::memory_order_acquire );

			if ( currentHead == currentTail )
				return false; // Queue is empty

			outItem = _arrBuffer[currentHead & kIndexMask];
			_head.store( currentHead + 1, std::memory_order_release );
			return true;
		}

		/**
		 * @brief 큐에 대기 중인 모든 항목을 일괄 드레인하여 출력 배열에 복사합니다.
		 * @param pOutBuffer 출력 배열 포인터
		 * @param maxCount 최대 복사 가능한 항목 수
		 * @return 실제로 복사된 항목 수
		 */
		uint32 drain( T* pOutBuffer, uint32 maxCount )
		{
			if ( pOutBuffer == nullptr || maxCount == 0 )
				return 0;

			const uint32 currentHead = _head.load( std::memory_order_relaxed );
			const uint32 currentTail = _tail.load( std::memory_order_acquire );

			const uint32 availableCount = currentTail - currentHead;
			if ( availableCount == 0 )
				return 0;

			const uint32 drainCount = availableCount < maxCount ? availableCount : maxCount;
			for ( uint32 index = 0; index < drainCount; ++index )
			{
				pOutBuffer[index] = _arrBuffer[( currentHead + index ) & kIndexMask];
			}

			_head.store( currentHead + drainCount, std::memory_order_release );
			return drainCount;
		}

		/**
		 * @brief 큐에 대기 중인 모든 항목을 vector에 드레인합니다.
		 */
		uint32 drain( vector<T>& outList )
		{
			const uint32 currentHead = _head.load( std::memory_order_relaxed );
			const uint32 currentTail = _tail.load( std::memory_order_acquire );

			const uint32 availableCount = currentTail - currentHead;
			if ( availableCount == 0 )
				return 0;

			outList.reserve( outList.size() + availableCount );
			for ( uint32 index = 0; index < availableCount; ++index )
			{
				outList.push_back( _arrBuffer[( currentHead + index ) & kIndexMask] );
			}

			_head.store( currentHead + availableCount, std::memory_order_release );
			return availableCount;
		}

		/** @brief 큐에 대기 중인 원소 개수 */
		uint32 getCount() const
		{
			const uint32 head = _head.load( std::memory_order_acquire );
			const uint32 tail = _tail.load( std::memory_order_acquire );
			return tail >= head ? ( tail - head ) : 0;
		}

		bool isEmpty() const
		{
			return _head.load( std::memory_order_acquire ) == _tail.load( std::memory_order_acquire );
		}

		void clear()
		{
			const uint32 tail = _tail.load( std::memory_order_relaxed );
			_head.store( tail, std::memory_order_release );
		}

	private:
		alignas( 64 ) std::atomic<uint32> _head;
		uint8 _arrPad0[64 - sizeof( std::atomic<uint32> )];

		alignas( 64 ) std::atomic<uint32> _tail;
		uint8 _arrPad1[64 - sizeof( std::atomic<uint32> )];

		T _arrBuffer[kCapacity];
	};
} // namespace sw
