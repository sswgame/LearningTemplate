#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanMemoryAllocator.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIUtil.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h"

namespace sw
{
    VulkanMemoryAllocator::VulkanMemoryAllocator( VulkanRHIDevice& device )
        : _pDevice{ &device }
        , _mutex{}
        , _listBlock{}
    {
    }

    bool VulkanMemoryAllocator::allocate( const Request& request, VulkanMemoryRange& outRange )
    {
        outRange                = VulkanMemoryRange{};
        const bool   bHost      = request._bHostVisible != SW_FALSE;
        const uint64 blockBytes = bHost ? kHostBlockBytes : kDeviceBlockBytes;
        if ( request._sizeBytes <= blockBytes / 2 )
        {
            ScopedLock<mutex> lock{ _mutex };
            uint32            freeBlockIndex = constant::invalid_index::kUint32;
            for ( uint32 blockIndex = 0; blockIndex < _listBlock.size(); ++blockIndex )
            {
                const Block& block = _listBlock[blockIndex];
                if ( block._memory == nullptr )
                {
                    freeBlockIndex = blockIndex;
                    continue;
                }
                const bool bMatches = block._memoryTypeIndex == request._memoryTypeIndex && block._bLinear == request._bLinear && ( block._pMapped != nullptr ) == bHost;
                if ( bMatches && allocateFromBlock( blockIndex, request, outRange ) )
                    return true;
            }
            Block block{};
            if ( allocateMemory( blockBytes, request._memoryTypeIndex, bHost, block._memory, block._pMapped ) )
            {
                block._sizeBytes       = blockBytes;
                block._memoryTypeIndex = request._memoryTypeIndex;
                block._bLinear         = request._bLinear;
                block._ranges.reset( blockBytes );
                if ( freeBlockIndex == constant::invalid_index::kUint32 )
                {
                    freeBlockIndex = static_cast<uint32>( _listBlock.size() );
                    _listBlock.push_back( std::move( block ) );
                }
                else
                {
                    _listBlock[freeBlockIndex] = std::move( block );
                }
                return allocateFromBlock( freeBlockIndex, request, outRange );
            }
            // 블록 하나만큼은 없어도 이 자원만큼은 있을 수 있다(메모리가 거의 찼다) — 아래 전용 할당이 마지막 시도다.
        }
        uint8* pMapped{ nullptr };
        if ( allocateMemory( request._sizeBytes, request._memoryTypeIndex, bHost, outRange._memory, pMapped ) == false )
            return false;
        outRange._sizeBytes = request._sizeBytes;
        outRange._pMapped   = pMapped;
        return true;
    }

    void VulkanMemoryAllocator::release( const VulkanMemoryRange& range )
    {
        if ( range.isValid() == false )
            return;
        if ( range._blockIndex == constant::invalid_index::kUint32 )
        {
            _pDevice->freeDeviceMemory( range._memory ); // 매핑은 메모리 해제가 풀어 준다
            return;
        }
        ScopedLock<mutex> lock{ _mutex };
        Block&            block = _listBlock[range._blockIndex];
        SW_ASSERT( block._memory == range._memory && "Vulkan memory range released after its block" );
        block._ranges.release( range._offset, range._sizeBytes );
        if ( block._ranges.isEmpty() == false )
            return;
        // 같은 종류의 빈 블록을 하나는 남긴다 — 자원이 경계에서 하나씩 오가도 블록을 만들었다 놓았다 하지 않는다.
        for ( uint32 blockIndex = 0; blockIndex < _listBlock.size(); ++blockIndex )
        {
            const Block& other     = _listBlock[blockIndex];
            const bool   bSameKind = blockIndex != range._blockIndex && other._memory != nullptr && other._memoryTypeIndex == block._memoryTypeIndex && other._bLinear == block._bLinear;
            if ( bSameKind && other._ranges.isEmpty() )
            {
                _pDevice->freeDeviceMemory( block._memory );
                block = Block{};
                return;
            }
        }
    }

    void VulkanMemoryAllocator::releaseAll()
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( Block& block : _listBlock )
        {
            if ( block._memory != nullptr )
                _pDevice->freeDeviceMemory( block._memory );
        }
        _listBlock.clear();
    }

    bool VulkanMemoryAllocator::allocateMemory( uint64 sizeBytes, uint32 memoryTypeIndex, bool bHostVisible, VkDeviceMemory& outMemory, uint8*& outMapped )
    {
        outMapped = nullptr;
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = sizeBytes;
        allocInfo.memoryTypeIndex = memoryTypeIndex;
        if ( _pDevice->allocateDeviceMemory( allocInfo, outMemory ) == false )
            return false;
        if ( bHostVisible == false )
            return true;
        void* pMapped{ nullptr };
        if ( isVkSuccess( vkMapMemory( _pDevice->_device, outMemory, 0, VK_WHOLE_SIZE, 0, &pMapped ), "vkMapMemory (memory block)" ) == false || pMapped == nullptr )
        {
            _pDevice->freeDeviceMemory( outMemory );
            outMemory = nullptr;
            return false;
        }
        outMapped = static_cast<uint8*>( pMapped );
        return true;
    }

    bool VulkanMemoryAllocator::allocateFromBlock( uint32 blockIndex, const Request& request, VulkanMemoryRange& outRange )
    {
        Block& block = _listBlock[blockIndex];
        uint64 offset{ 0 };
        if ( block._ranges.allocate( request._sizeBytes, request._alignment, offset ) == false )
            return false;
        outRange._memory     = block._memory;
        outRange._offset     = offset;
        outRange._sizeBytes  = request._sizeBytes;
        outRange._pMapped    = block._pMapped != nullptr ? block._pMapped + offset : nullptr;
        outRange._blockIndex = blockIndex;
        return true;
    }
} // namespace sw
