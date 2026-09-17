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
         * @param blockSize 단일 블록의 크기. **16바이트 배수로 올림**되며 최소 `sizeof(void*)` 입니다.
         *                  (문서가 오래 "포인터 크기 단위로 정렬" 이라고 했는데 구현은 처음부터 16 이었다 —
         *                  청크 헤더와 기반 할당도 16 으로 맞춘다. SSE 타입을 담아도 안전하다는 뜻이다.)
         * @param blocksPerChunk 한 번에 OS로부터 할당받을 청크 내의 블록 개수
         * @param bThreadSafe true이면 내부적으로 Mutex를 사용하여 스레드 안전하게 동작
         */
        PoolAllocator( size_t blockSize, uint32 blocksPerChunk = 1024, bool bThreadSafe = true );
        /** @brief 소유한 청크를 모두 해제합니다. */
        ~PoolAllocator();

        /** @brief 복사를 금지합니다. */
        PoolAllocator( const PoolAllocator& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        PoolAllocator& operator=( const PoolAllocator& ) = delete;

        /** @brief 풀에서 블록 하나를 할당받습니다. 고갈·OOM 이면 nullptr. */
        void* allocate();

        /** @brief 풀에 블록을 반환합니다. 널이면 무시합니다. */
        void free( void* pBlock );

        /**
         * @brief 모든 메모리를 해제합니다.
         * @warning 내준 블록이 남아 있으면 그 포인터는 전부 죽습니다 — 호출자가 책임집니다.
         */
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

        size_t    _blockSize;
        Chunk*    _pChunkList;
        FreeNode* _pFreeList;
        sw::mutex _mutex;
        uint32    _blocksPerChunk;
        bool      _bThreadSafe;

        /** @brief 청크를 하나 더 잡아 프리 리스트에 붙입니다. **잠금을 잡은 채** 호출합니다. */
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
