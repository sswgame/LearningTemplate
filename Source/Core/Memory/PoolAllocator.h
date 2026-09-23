/**
 * @file PoolAllocator.h
 * @brief O(1) 고정 크기 블록 할당자입니다(스레드 안전 여부를 고를 수 있습니다).
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
     * @brief 같은 크기의 메모리 블록을 빠르게 할당 · 해제하는 풀 할당자입니다.
     * @details 청크 단위로 메모리를 확보하고, 해제된 블록은 자유 목록(free list)으로 관리합니다.
     */
    class SW_API PoolAllocator
    {
    public:
        /**
         * @brief 풀을 준비합니다.
         * @param blockSize 블록 하나의 크기. **16바이트 배수로 올림**하며, 최소 `sizeof(void*)` 입니다.
         *                  (문서에는 오랫동안 "포인터 크기 단위로 정렬" 이라고 적혀 있었지만 구현은 처음부터 16 이었습니다.
         *                  청크 헤더와 기반 할당도 16 으로 맞추므로 SSE 타입을 담아도 안전합니다.)
         * @param blocksPerChunk 한 번에 OS 에서 할당받을 청크 안의 블록 수
         * @param bThreadSafe true 면 내부 뮤텍스로 스레드 안전하게 동작합니다
         */
        PoolAllocator( size_t blockSize, uint32 blocksPerChunk = 1024, bool bThreadSafe = true );
        /** @brief 소유한 청크를 모두 해제합니다. */
        ~PoolAllocator();

        /** @brief 복사를 금지합니다. */
        PoolAllocator( const PoolAllocator& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        PoolAllocator& operator=( const PoolAllocator& ) = delete;

        /** @brief 풀에서 블록 하나를 할당받습니다. 바닥났거나 메모리가 부족하면 nullptr 입니다. */
        void* allocate();

        /** @brief 블록을 풀에 돌려줍니다. nullptr 이면 무시합니다. */
        void free( void* pBlock );

        /**
         * @brief 모든 메모리를 해제합니다.
         * @warning 내준 블록이 남아 있으면 그 포인터는 모두 무효가 됩니다. 호출하는 쪽이 책임집니다.
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
#if defined( SW_DEBUG )
            /**
             * @brief 이 블록이 **지금 자유 목록에 있는지** 나타내는 표시입니다.
             * @details 같은 블록을 두 번 반납하면 `_pNext` 가 자기 자신을 가리켜 목록이 **자기 자신으로 도는 고리**가 됩니다. 그 뒤로는
             *          모든 할당이 같은 블록을 돌려주고, 서로 다른 두 객체가 같은 주소를 쓰게 됩니다. 증상은 한참 뒤 엉뚱한 곳에서
             *          터지므로, **두 번째 반납 바로 그 자리에서** 멈추는 것이 큰 도움이 됩니다(이번 점검에서 실제로 한 번 겪었습니다.
             *          `GameObjectManager::destroyObject` 의 check-then-set 경쟁이었습니다).
             *
             *          목록을 훑으면 해제가 O(n) 이 되므로 표시 하나로 O(1) 에 확인합니다. `sizeof( FreeNode )` 가 8 에서 16 으로
             *          늘지만, 블록 크기는 어차피 16 의 배수로 올림하므로 **실제 블록 크기는 달라지지 않습니다.**
             */
            uint64 _freeMagic;
#endif
        };

#if defined( SW_DEBUG )
        /** @brief 자유 목록에 든 블록의 표시 값입니다. 임의의 값이라 객체의 잔해와 우연히 같을 확률은 2^-64 입니다. */
        static constexpr uint64 kFreeBlockMagic = 0xF2EE'B10C'F2EE'B10Cull;
#endif

        size_t    _blockSize;
        Chunk*    _pChunkList;
        FreeNode* _pFreeList;
        sw::mutex _mutex;
        uint32    _blocksPerChunk;
        bool      _bThreadSafe;

        /** @brief 청크를 하나 더 잡아 자유 목록에 붙입니다. **잠금을 잡은 채로** 부릅니다. */
        void allocateChunk();
    };

    /**
     * @brief 타입을 지정하는 풀 할당자 래퍼입니다.
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
