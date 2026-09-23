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

        // 아래에서 `size + alignment` 로 새 블록 크기를 정한다. 오버플로하면 **더 작은** 블록을 잡고, 그 블록에도 들어가지
        // 않으니 블록을 끝없이 늘리게 된다. 담을 수 없는 크기는 여기서 거른다.
        if ( size > SIZE_MAX - alignment )
            return nullptr;

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
                // **뺄셈으로 비교한다.** `alignedOffset + size` 로 쓰면 큰 size 에서 합이 오버플로해 검사를 통과하고, 블록 밖을
                // 가리키는 주소가 정상 할당인 것처럼 반환된다. 정렬 때문에 alignedOffset 이 capacity 를 넘을 수도 있어서 그것도 함께 본다.
                if ( alignedOffset > pCurrentBlock->_capacity || size > pCurrentBlock->_capacity - alignedOffset )
                    break;

                const size_t newOffset = alignedOffset + size;

                if ( pCurrentBlock->_offset.compare_exchange_weak( oldOffset, newOffset, std::memory_order_acq_rel, std::memory_order_acquire ) )
                    return reinterpret_cast<void*>( alignedPtr );
            }

            // 블록이 가득 찼다. **이미 가지고 있는 다음 블록부터 본다.** reset() 은 오프셋만 되돌리므로 그 뒤의 블록들은 빈
            // 채로 남아 있다. 여기서 곧바로 새 블록을 잡으면 그 빈 블록들은 다음 clear() 까지 놀고, reset 할 때마다 표가 한 칸씩
            // 늘면서 블록 용량은 두 배로 커진다(EventDispatcher 는 프레임마다 reset 하므로, 거기서 프레임마다 메모리가 불어난다).
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _currentBlockIndex.load( std::memory_order_acquire ) == blockIndex )
            {
                if ( advanceToHeldBlock( blockIndex, size + alignment ) == false )
                {
                    if ( allocateNewBlock( size + alignment ) == false )
                        return nullptr;
                }
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
                Memory::freeAligned( pBlock->_pData );

            sw_delete( pBlock );
            _arrBlock[blockIndex].store( nullptr, std::memory_order_relaxed );
        }

        _blockCount.store( 0, std::memory_order_release );
        _currentBlockIndex.store( 0, std::memory_order_release );
    }

    bool LinearAllocator::advanceToHeldBlock( size_t fromBlockIndex, size_t requiredCapacity )
    {
        const size_t blockCount = _blockCount.load( std::memory_order_acquire );
        for ( size_t blockIndex = fromBlockIndex + 1; blockIndex < blockCount; ++blockIndex )
        {
            const Block* pBlock = _arrBlock[blockIndex].load( std::memory_order_acquire );
            if ( pBlock == nullptr || pBlock->_capacity < requiredCapacity )
                continue;

            // 현재 블록보다 뒤에 있는 블록은 아직 아무도 쓰지 않았다(오프셋은 한 방향으로만 늘고 reset 이 모두 0 으로 되돌린다).
            // 그래도 확인하고 넘어간다. 틀렸다면 이미 내준 메모리를 다시 내주는 것이라 조용히 깨진다.
            if ( pBlock->_offset.load( std::memory_order_acquire ) != 0 )
                continue;

            _currentBlockIndex.store( blockIndex, std::memory_order_release );
            return true;
        }
        return false;
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
            // 슬롯이 바닥나지 않도록 블록 용량을 두 배로 키운다.
            const Block* pPrevious = _arrBlock[blockCount - 1].load( std::memory_order_relaxed );
            capacity               = MathUtil::max( capacity, pPrevious->_capacity * 2 );
        }

        Block* pBlock     = sw_new Block();
        pBlock->_capacity = capacity;
        pBlock->_offset.store( 0, std::memory_order_relaxed );
        pBlock->_pData = static_cast<uint8*>( Memory::allocateAligned( capacity, alignof( std::max_align_t ) ) );
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
