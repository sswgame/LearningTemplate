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

        // 16바이트 정렬
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
        void*        pRaw            = sw::Memory::alignedAlloc( allocSize, 16 );
        if ( pRaw == nullptr )
        {
            // OOM
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
            _pFreeList      = pNode;
        }
    }

    void* PoolAllocator::allocate()
    {
        if ( _bThreadSafe )
            _mutex.lock();

        if ( _pFreeList == nullptr )
        {
            allocateChunk();
        }

        if ( _pFreeList == nullptr )
        {
            if ( _bThreadSafe )
                _mutex.unlock();
            return nullptr;
        }

        FreeNode* pNode = _pFreeList;
        _pFreeList      = pNode->_pNext;

        if ( _bThreadSafe )
            _mutex.unlock();

        return pNode;
    }

    void PoolAllocator::free( void* pBlock )
    {
        if ( pBlock == nullptr )
            return;

        if ( _bThreadSafe )
            _mutex.lock();

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
        {
            if ( _bThreadSafe )
                _mutex.unlock();
            return;
        }
#endif

        FreeNode* pNode = static_cast<FreeNode*>( pBlock );
        pNode->_pNext   = _pFreeList;
        _pFreeList      = pNode;

        if ( _bThreadSafe )
            _mutex.unlock();
    }

    void PoolAllocator::clear()
    {
        if ( _bThreadSafe )
            _mutex.lock();

        Chunk* pCurr = _pChunkList;
        while ( pCurr != nullptr )
        {
            Chunk* pNext = pCurr->_pNext;
            sw::Memory::alignedFree( pCurr );
            pCurr = pNext;
        }

        _pChunkList = nullptr;
        _pFreeList  = nullptr;

        if ( _bThreadSafe )
            _mutex.unlock();
    }
} // namespace sw
