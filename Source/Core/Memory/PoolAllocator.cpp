#include "pch.h"

#include "Core/Memory/PoolAllocator.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    PoolAllocator::PoolAllocator( size_t blockSize, uint32 blocksPerChunk, bool bThreadSafe )
        : _blockSize{ blockSize }
        , _pChunkList{ nullptr }
        , _pFreeList{ nullptr }
        , _mutex{}
        , _blocksPerChunk{ blocksPerChunk }
        , _bThreadSafe{ bThreadSafe }
    {
        if ( _blockSize < sizeof( FreeNode ) )
            _blockSize = sizeof( FreeNode );

        // 16바이트 배수로 올림한다
        _blockSize = ( _blockSize + 15u ) & ~size_t{ 15 };
    }

    PoolAllocator::~PoolAllocator()
    {
        clear();
    }

    void PoolAllocator::allocateChunk()
    {
        const size_t chunkHeaderSize = ( sizeof( Chunk ) + 15u ) & ~size_t{ 15 };
        const size_t allocSize       = chunkHeaderSize + ( _blockSize * _blocksPerChunk );
        void*        pRaw            = sw::Memory::allocateAligned( allocSize, 16 );
        if ( pRaw == nullptr )
        {
            // 메모리 부족
            return;
        }

        Chunk* pNewChunk  = static_cast<Chunk*>( pRaw );
        pNewChunk->_pNext = _pChunkList;
        _pChunkList       = pNewChunk;

        uint8* pData = reinterpret_cast<uint8*>( pNewChunk ) + chunkHeaderSize;
        for ( uint32 index = 0; index < _blocksPerChunk; ++index )
        {
            FreeNode* pNode = reinterpret_cast<FreeNode*>( pData + ( index * _blockSize ) );
            pNode->_pNext   = _pFreeList;
#if defined( SW_DEBUG )
            pNode->_freeMagic = kFreeBlockMagic;
#endif
            _pFreeList = pNode;
        }
    }

    void* PoolAllocator::allocate()
    {
        // 잠금 해제를 잊을 곳을 없앤다. 예전에는 일찍 반환하는 곳마다 unlock 을 손으로 적었고(7곳), 하나만 빠져도
        // 데드락이었다. 잠글지 말지는 여전히 `_bThreadSafe` 가 정한다.
        std::unique_lock<mutex> lock{ _mutex, std::defer_lock };
        if ( _bThreadSafe )
            lock.lock();

        if ( _pFreeList == nullptr )
            allocateChunk();

        if ( _pFreeList == nullptr )
            return nullptr;

        FreeNode* pNode = _pFreeList;
        _pFreeList      = pNode->_pNext;
#if defined( SW_DEBUG )
        // 내주는 순간 표시를 지운다. 남겨 두면 다음 반납이 "이미 자유 목록에 있다" 로 잘못 읽는다.
        pNode->_freeMagic = 0;
#endif
        return pNode;
    }

    void PoolAllocator::free( void* pBlock )
    {
        if ( pBlock == nullptr )
            return;

        // 잠금 해제를 잊을 곳을 없앤다. 예전에는 일찍 반환하는 곳마다 unlock 을 손으로 적었고(7곳), 하나만 빠져도
        // 데드락이었다. 잠글지 말지는 여전히 `_bThreadSafe` 가 정한다.
        std::unique_lock<mutex> lock{ _mutex, std::defer_lock };
        if ( _bThreadSafe )
            lock.lock();

#if defined( SW_DEBUG )
        bool         bValidChunk     = false;
        const size_t chunkHeaderSize = ( sizeof( Chunk ) + 15u ) & ~size_t{ 15 };
        const size_t chunkSize       = chunkHeaderSize + ( _blockSize * _blocksPerChunk );
        for ( Chunk* pChunk = _pChunkList; pChunk != nullptr; pChunk = pChunk->_pNext )
        {
            const uint8* pStart  = reinterpret_cast<const uint8*>( pChunk ) + chunkHeaderSize;
            const uint8* pEnd    = reinterpret_cast<const uint8*>( pChunk ) + chunkSize;
            const uint8* pTarget = reinterpret_cast<const uint8*>( pBlock );
            if ( pStart <= pTarget && pTarget < pEnd )
            {
                bValidChunk = true;
                break;
            }
        }
        SW_ASSERT( bValidChunk && "PoolAllocator::free: Pointer does not belong to any allocated chunk!" );
        if ( bValidChunk == false )
            return;

        // **이미 자유 목록에 있는 블록인가.** 두 번 넣으면 `_pNext` 가 자기 자신을 가리켜 목록이 자기 자신으로 도는 고리가
        // 되고, 그 뒤 모든 할당이 같은 블록을 돌려준다. 여기서 멈추지 않으면 증상은 한참 뒤 엉뚱한 곳에서 터진다.
        if ( static_cast<const FreeNode*>( pBlock )->_freeMagic == kFreeBlockMagic )
        {
            SW_ASSERT( false && "PoolAllocator::free: block is already in the free list (double free)" );
            return;
        }
#endif

        FreeNode* pNode = static_cast<FreeNode*>( pBlock );
        pNode->_pNext   = _pFreeList;
#if defined( SW_DEBUG )
        pNode->_freeMagic = kFreeBlockMagic;
#endif
        _pFreeList = pNode;
    }

    void PoolAllocator::clear()
    {
        // 잠금 해제를 잊을 곳을 없앤다. 예전에는 일찍 반환하는 곳마다 unlock 을 손으로 적었고(7곳), 하나만 빠져도
        // 데드락이었다. 잠글지 말지는 여전히 `_bThreadSafe` 가 정한다.
        std::unique_lock<mutex> lock{ _mutex, std::defer_lock };
        if ( _bThreadSafe )
            lock.lock();

        Chunk* pCurr = _pChunkList;
        while ( pCurr != nullptr )
        {
            Chunk* pNext = pCurr->_pNext;
            sw::Memory::freeAligned( pCurr );
            pCurr = pNext;
        }

        _pChunkList = nullptr;
        _pFreeList  = nullptr;
    }
} // namespace sw
