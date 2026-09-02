#include "pch.h"

#include "Core/Memory/LinearAllocator.h"

#include "Core/Concurrency/mutex.h"
#include "Core/CoreMinimal.h"

namespace sw
{
    SW_LOG_CALLER( "LinearAllocator" );

    LinearAllocator::LinearAllocator()
        : LinearAllocator{ constant::kDefaultLinearCapacity }
    {
    }

    LinearAllocator::LinearAllocator( size_t initialCapacity )
        : _defaultCapacity{ initialCapacity }
        , _blockCount{ 0 }
        , _currentBlockIndex{ 0 }
        , _arrBlock{}
        , _mutex{}
    {
        for ( size_t slotIndex = 0; slotIndex < kMaxBlockCount; ++slotIndex )
        {
            _arrBlock[slotIndex].store( nullptr, std::memory_order_relaxed );
        }

        std::scoped_lock<mutex> lock{ _mutex };
        allocateNewBlock( initialCapacity );
    }

    LinearAllocator::~LinearAllocator()
    {
        clear();
    }

    void* LinearAllocator::allocate( size_t size, size_t alignment )
    {
        if ( size == 0 )
            return nullptr;

        if ( alignment == 0 || MathUtil::isPowerOfTwo( alignment ) == false )
            alignment = alignof( std::max_align_t );

        while ( true )
        {
            const size_t blockIndex    = _currentBlockIndex.load( std::memory_order_acquire );
            Block*       pCurrentBlock = ( blockIndex < kMaxBlockCount ) ? _arrBlock[blockIndex].load( std::memory_order_acquire ) : nullptr;

            if ( pCurrentBlock == nullptr )
            {
                std::scoped_lock<mutex> lock{ _mutex };
                const size_t            recheckIndex = _currentBlockIndex.load( std::memory_order_relaxed );
                if ( recheckIndex >= kMaxBlockCount || _arrBlock[recheckIndex].load( std::memory_order_relaxed ) == nullptr )
                {
                    if ( allocateNewBlock( size + alignment ) == false )
                        return nullptr;
                }
                continue;
            }

            size_t oldOffset = pCurrentBlock->_offset.load( std::memory_order_acquire );
            while ( true )
            {
                const uintptr_t basePtr       = reinterpret_cast<uintptr_t>( pCurrentBlock->_pData ) + oldOffset;
                const uintptr_t alignedPtr    = MathUtil::align( basePtr, static_cast<uintptr_t>( alignment ) );
                const size_t    alignedOffset = static_cast<size_t>( alignedPtr - reinterpret_cast<uintptr_t>( pCurrentBlock->_pData ) );
                const size_t    newOffset     = alignedOffset + size;
                if ( newOffset > pCurrentBlock->_capacity )
                    break;

                if ( pCurrentBlock->_offset.compare_exchange_weak( oldOffset, newOffset, std::memory_order_acq_rel, std::memory_order_acquire ) )
                    return reinterpret_cast<void*>( alignedPtr );
            }

            // 블록이 가득 찼으므로 새 블록을 할당합니다.
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _currentBlockIndex.load( std::memory_order_acquire ) == blockIndex )
            {
                if ( allocateNewBlock( size + alignment ) == false )
                    return nullptr;
            }
        }
    }

    void LinearAllocator::reset()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const size_t            blockCount = _blockCount.load( std::memory_order_acquire );
        for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            _arrBlock[blockIndex].load( std::memory_order_acquire )->_offset.store( 0, std::memory_order_release );
        }

        _currentBlockIndex.store( 0, std::memory_order_release );
    }

    void LinearAllocator::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const size_t            blockCount = _blockCount.load( std::memory_order_relaxed );
        for ( size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex )
        {
            Block* pBlock = _arrBlock[blockIndex].load( std::memory_order_relaxed );
            if ( pBlock == nullptr )
                continue;

            if ( pBlock->_pData != nullptr )
                Memory::alignedFree( pBlock->_pData );

            sw_delete( pBlock );
            _arrBlock[blockIndex].store( nullptr, std::memory_order_relaxed );
        }

        _blockCount.store( 0, std::memory_order_release );
        _currentBlockIndex.store( 0, std::memory_order_release );
    }

    bool LinearAllocator::allocateNewBlock( size_t minCapacity )
    {
        const size_t blockCount = _blockCount.load( std::memory_order_relaxed );
        if ( blockCount >= kMaxBlockCount )
        {
            SW_LOG_ERROR( "Block table exhausted (%# blocks).", static_cast<uint32>( kMaxBlockCount ) );
            return false;
        }

        size_t capacity = MathUtil::max( _defaultCapacity, minCapacity );
        if ( blockCount > 0 )
        {
            // 슬롯이 고갈되지 않도록 블록 용량을 배로 키웁니다.
            const Block* pPrevious = _arrBlock[blockCount - 1].load( std::memory_order_relaxed );
            capacity               = MathUtil::max( capacity, pPrevious->_capacity * 2 );
        }

        Block* pBlock     = sw_new Block();
        pBlock->_capacity = capacity;
        pBlock->_offset.store( 0, std::memory_order_relaxed );
        pBlock->_pData = static_cast<uint8*>( Memory::alignedAlloc( capacity, alignof( std::max_align_t ) ) );
        if ( pBlock->_pData == nullptr )
        {
            sw_delete( pBlock );
            SW_LOG_ERROR( "Failed to allocate a %# byte block.", static_cast<uint32>( capacity ) );
            return false;
        }

        _arrBlock[blockCount].store( pBlock, std::memory_order_release );
        _blockCount.store( blockCount + 1, std::memory_order_release );
        _currentBlockIndex.store( blockCount, std::memory_order_release );
        return true;
    }

} // namespace sw
