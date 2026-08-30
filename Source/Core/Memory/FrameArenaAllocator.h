/**
 * @file FrameArenaAllocator.h
 * @brief 프레임 아레나 할당자와 GT/RT 더블 버퍼.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) FrameArenaAllocator — 청크 선형 할당, 프레임 끝 reset 은 O(1)
	//    실제 free 없이 오프셋만 되돌림. 스코프 롤백은 Marker
	// ------------------------------------------------------------------------------
	/**
	 * @class FrameArenaAllocator
	 * @brief 청크 기반 선형 할당기. 프레임 끝에 오프셋을 한 번에 되돌려 동적 할당 비용을 줄입니다.
	 */
	class SW_API FrameArenaAllocator
	{
	public:
		/**
		 * @brief 첫 청크 용량을 잡고 할당기를 준비합니다.
		 * @param defaultCapacity 초기 청크 크기(바이트)
		 */
		explicit FrameArenaAllocator( size_t defaultCapacity = 1024 * 1024 );
		/** @brief 소유한 청크 버퍼를 해제합니다. */
		~FrameArenaAllocator();

		/** @brief 복사를 금지합니다. */
		FrameArenaAllocator( const FrameArenaAllocator& ) = delete;
		/** @brief 복사 대입을 금지합니다. */
		FrameArenaAllocator& operator=( const FrameArenaAllocator& ) = delete;

		/**
		 * @brief 지정 크기·정렬로 현재 청크에서 잘라 냅니다. 부족하면 새 청크를 붙입니다.
		 * @param size 할당할 바이트
		 * @param alignment 정렬 (2의 거듭제곱)
		 * @return 할당된 주소
		 */
		SW_INLINE void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) )
		{
			if ( size == 0 )
				return nullptr;

			if ( alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 )
				alignment = alignof( std::max_align_t );

			if ( _currentChunkIndex < _listChunk.size() )
			{
				Chunk&	  chunk	  = _listChunk[_currentChunkIndex];
				uintptr_t current = reinterpret_cast<uintptr_t>( chunk._pBuffer + chunk._offset );
				uintptr_t aligned = ( current + ( alignment - 1 ) ) & ~( static_cast<uintptr_t>( alignment - 1 ) );
				size_t	  padding = aligned - current;

				if ( chunk._offset + padding + size <= chunk._capacity )
				{
					chunk._offset += padding + size;
					_usedBytes += padding + size;
					return reinterpret_cast<void*>( aligned );
				}
			}

			return allocateSlow( size, alignment );
		}

		/**
		 * @brief 아레나에서 객체를 배치 생성합니다 (placement new).
		 */
		template <typename T, typename... Args>
		T* construct( Args&&... args )
		{
			static_assert( std::is_trivially_destructible_v<T>,
						   "FrameArenaAllocator only supports Trivially Destructible types (destructors are not invoked on reset)!" );
			void* pMem = allocate( sizeof( T ), alignof( T ) );
			if constexpr ( std::is_aggregate_v<T> )
				return new ( pMem ) T{ std::forward<Args>( args )... };
			else
				return new ( pMem ) T( std::forward<Args>( args )... );
		}

		/**
		 * @brief 오프셋을 0으로 되돌립니다. 청크 메모리는 유지합니다.
		 */
		void reset();

		/**
		 * @struct Marker
		 * @brief createMarker / rollbackToMarker 용 청크·오프셋 스냅샷입니다.
		 */
		struct Marker
		{
			size_t _chunkIndex{ 0 };
			size_t _offset{ 0 };
			size_t _usedBytes{ 0 };
		};

		/**
		 * @brief 현재 청크 인덱스·오프셋·사용량을 캡처합니다.
		 */
		Marker createMarker() const;

		/**
		 * @brief 마커 시점 이후 할당을 무효로 되돌립니다.
		 * @param marker createMarker()로 찍은 상태
		 */
		void rollbackToMarker( const Marker& marker );

		/** @brief 지금까지 확보한 청크 용량 합(바이트)입니다. */
		size_t getTotalAllocatedBytes() const { return _totalAllocatedBytes; }
		/** @brief 현재 프레임에서 잘라 쓴 바이트입니다. */
		size_t getUsedBytes() const { return _usedBytes; }

		/** @brief 현재 스레드의 프레임 아레나를 반환합니다. 없으면 생성합니다. */
		static FrameArenaAllocator& getThreadLocal();

	private:
		/** @brief 한 청크의 버퍼·용량·쓰기 오프셋입니다. */
		struct Chunk
		{
			uint8* _pBuffer{ nullptr };
			size_t _capacity{ 0 };
			size_t _offset{ 0 };
		};

		/**
		 * @brief minSize 이상을 담는 새 청크를 할당하고 현재 청크로 전환합니다.
		 */
		void allocateNewChunk( size_t minSize );

		/**
		 * @brief 청크 용량이 모자랄 때 다음 청크를 찾거나 새로 할당하는 슬로우 패스입니다.
		 */
		void* allocateSlow( size_t size, size_t alignment );

		size_t		  _defaultCapacity;
		size_t		  _totalAllocatedBytes;
		size_t		  _usedBytes;
		size_t		  _currentChunkIndex;
		vector<Chunk> _listChunk;
	};

	// ------------------------------------------------------------------------------
	// 2) FrameDoubleBuffer — GT가 N을 쓰는 동안 RT가 N-1을 읽음
	//    swapAndResetPrevious 가 인덱스를 뒤집고 새 활성 쪽만 reset
	// ------------------------------------------------------------------------------
	class FrameDoubleBuffer
	{
	public:
		/** @brief 양쪽 아레나를 같은 용량으로 준비합니다. */
		explicit FrameDoubleBuffer( size_t arenaCapacity = 1024 * 1024 )
			: _arrArenas{ FrameArenaAllocator{ arenaCapacity }, FrameArenaAllocator{ arenaCapacity } }
			, _activeBufferIndex{ 0 } {}

		/** @brief 양쪽 아레나 청크를 해제합니다. */
		~FrameDoubleBuffer() = default;

		/** @brief 활성 아레나에서 정렬 할당합니다. */
		SW_INLINE void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) )
		{
			const uint32 activeIdx = _activeBufferIndex.load( std::memory_order_relaxed );
			return _arrArenas[activeIdx].allocate( size, alignment );
		}

		/** @brief 활성 인덱스를 뒤집고, 새로 활성인 쪽을 reset 합니다. */
		SW_INLINE void swapAndResetPrevious()
		{
			const uint32 newIdx = 1 - _activeBufferIndex.load( std::memory_order_relaxed );
			_arrArenas[newIdx].reset();
			_activeBufferIndex.store( newIdx, std::memory_order_release );
		}

		/** @brief 지금 쓰는 버퍼 인덱스(0 또는 1)입니다. */
		SW_INLINE uint32 getCurrentIndex() const { return _activeBufferIndex.load( std::memory_order_acquire ); }
		/** @brief 활성 아레나의 사용 바이트입니다. */
		SW_INLINE size_t getCurrentUsedBytes() const
		{
			const uint32 activeIdx = _activeBufferIndex.load( std::memory_order_relaxed );
			return _arrArenas[activeIdx].getUsedBytes();
		}

		/** @brief EngineLoop가 소유한 프레임 더블 버퍼를 연결하거나 해제합니다. */
		SW_API static void bind( FrameDoubleBuffer* pBuffer );
		/** @brief 바인딩된 프레임 더블 버퍼를 반환합니다. */
		SW_API static FrameDoubleBuffer& get();

	private:
		FrameArenaAllocator _arrArenas[2];
		atomic<uint32>		_activeBufferIndex;
	};
} // namespace sw
