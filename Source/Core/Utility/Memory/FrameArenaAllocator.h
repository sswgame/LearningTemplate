#pragma once
/**
 * @file FrameArenaAllocator.h
 * @brief 프레임 단위 임시 메모리 할당을 위한 아레나(Arena) 할당자 클래스 선언입니다.
 */
#include "Core/Common/Common.h"
namespace sw
{

	/**
	 * @class FrameArenaAllocator
	 * @brief 청크 기반의 선형 메모리 할당기입니다. 프레임이 끝날 때 메모리를 한 번에 해제(O(1))하여 동적 할당 오버헤드를 극소화합니다.
	 */
	class SW_API FrameArenaAllocator
	{
	public:
		/**
		 * @brief 할당자를 초기화합니다.
		 * @param defaultCapacity 초기 청크의 기본 크기 (바이트 단위)
		 */
		explicit FrameArenaAllocator( size_t defaultCapacity = 1024 * 1024 );
		~FrameArenaAllocator();

		FrameArenaAllocator( const FrameArenaAllocator& )			 = delete;
		FrameArenaAllocator& operator=( const FrameArenaAllocator& ) = delete;

		/**
		 * @brief 지정된 크기와 정렬에 맞게 메모리를 할당합니다.
		 * @param size 할당할 메모리 크기 (바이트)
		 * @param alignment 메모리 정렬 값
		 * @return 할당된 메모리 주소
		 */
		void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) );

		/**
		 * @brief 객체를 할당하고 생성자를 호출합니다. (Placement New)
		 */
		template <typename T, typename... Args>
		T* construct( Args&&... args )
		{
			void* mem = allocate( sizeof( T ), alignof( T ) );
			if constexpr ( std::is_aggregate_v<T> )
			{
				return new ( mem ) T{ std::forward<Args>( args )... };
			}
			else
			{
				return new ( mem ) T( std::forward<Args>( args )... );
			}
		}

		/**
		 * @brief 현재까지 할당된 모든 메모리의 오프셋을 0으로 되돌립니다. 실제 메모리는 해제하지 않습니다.
		 */
		void reset();

		/**
		 * @struct Marker
		 * @brief 스코프 기반 롤백을 지원하기 위한 상태 기록 마커입니다.
		 */
		struct Marker
		{
			size_t _chunkIndex = 0;
			size_t _offset	   = 0;
			size_t _usedBytes  = 0;
		};

		/**
		 * @brief 현재 할당 상태를 캡처하여 마커를 반환합니다.
		 */
		Marker createMarker() const;

		/**
		 * @brief 지정된 마커 위치로 할당기 상태를 되돌립니다.
		 * @param marker 롤백할 목표 상태 마커
		 */
		void rollbackToMarker( const Marker& marker );

		size_t getTotalAllocatedBytes() const { return _totalAllocatedBytes; }
		size_t getUsedBytes() const { return _usedBytes; }

	private:
		struct Chunk
		{
			uint8* _buffer	 = nullptr;
			size_t _capacity = 0;
			size_t _offset	 = 0;
		};

		/**
		 * @brief 새 메모리 청크를 할당합니다
		 */
		void allocateNewChunk( size_t minSize );

		size_t			   _defaultCapacity;
		size_t			   _totalAllocatedBytes = 0;
		size_t			   _usedBytes			= 0;
		size_t			   _currentChunkIndex	= 0;
		std::vector<Chunk> _chunks;
	};

	/**
	 * @brief 현재 스레드에 바인딩된 프레임 할당자를 반환합니다. (TLS 지원)
	 */
	SW_API FrameArenaAllocator& getThreadLocalFrameArena();
}
