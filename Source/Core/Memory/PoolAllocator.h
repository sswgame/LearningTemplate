/**
 * @file PoolAllocator.h
 * @brief O(1) 고정 크기 블록 할당기 (Thread-safe 선택 가능)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	/**
	 * @class PoolAllocator
	 * @brief 동일한 크기의 메모리 블록을 빠르게 할당/해제하는 풀 할당기.
	 * @details 내부적으로 Chunk 단위로 메모리를 확보하며 해제된 블록은 Free List로 관리합니다.
	 */
	class SW_API PoolAllocator
	{
	public:
		/**
		 * @brief 생성자
		 * @param blockSize 단일 블록의 크기 (최소 sizeof(void*), 포인터 크기 단위로 정렬됨)
		 * @param blocksPerChunk 한 번에 OS로부터 할당받을 청크 내의 블록 개수
		 * @param bThreadSafe true이면 내부적으로 Mutex를 사용하여 스레드 안전하게 동작
		 */
		PoolAllocator( size_t blockSize, uint32 blocksPerChunk = 1024, bool bThreadSafe = true );
		~PoolAllocator();

		PoolAllocator( const PoolAllocator& )			 = delete;
		PoolAllocator& operator=( const PoolAllocator& ) = delete;

		/** @brief 풀에서 블록 하나를 할당받습니다. */
		void* allocate();

		/** @brief 풀에 블록을 반환합니다. */
		void free( void* pBlock );

		/** @brief 모든 메모리를 해제합니다. */
		void clear();

	private:
		struct Chunk
		{
			Chunk* _pNext;
		};

		struct FreeNode
		{
			FreeNode* _pNext;
		};

		size_t	  _blockSize;
		Chunk*	  _pChunkList;
		FreeNode* _pFreeList;
		sw::mutex _mutex;
		uint32	  _blocksPerChunk;
		bool	  _bThreadSafe;

		void allocateChunk();
	};

	/**
	 * @brief 타입 지정 템플릿 풀 할당기 래퍼
	 */
	template <typename T>
	class TypedPoolAllocator
	{
	public:
		TypedPoolAllocator( uint32 blocksPerChunk = 1024, bool bThreadSafe = true )
			: _pool{ sizeof( T ), blocksPerChunk, bThreadSafe }
		{
		}

		template <typename... Args>
		T* create( Args&&... args )
		{
			void* pMem = _pool.allocate();
			if ( pMem == nullptr )
				return nullptr;
			return sw_placement_new( pMem ) T( std::forward<Args>( args )... );
		}

		void destroy( T* pObj )
		{
			if ( pObj )
			{
				pObj->~T();
				_pool.free( pObj );
			}
		}

		void clear()
		{
			_pool.clear();
		}

	private:
		PoolAllocator _pool;
	};

} // namespace sw
