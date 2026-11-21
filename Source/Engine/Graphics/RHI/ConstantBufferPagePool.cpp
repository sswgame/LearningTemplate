#include "pch.h"

#include "Engine/Graphics/RHI/ConstantBufferPagePool.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    ConstantBufferPoolSlice::~ConstantBufferPoolSlice()
    {
        reset();
    }

    ConstantBufferPoolSlice::ConstantBufferPoolSlice( ConstantBufferPoolSlice&& other ) noexcept
        : _pDevice{ other._pDevice }
        , _slot{ other._slot }
        , _range{ other._range }
        , _descriptorIndex{ other._descriptorIndex }
    {
        other.detach();
    }

    ConstantBufferPoolSlice& ConstantBufferPoolSlice::operator=( ConstantBufferPoolSlice&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        reset();
        _pDevice         = other._pDevice;
        _slot            = other._slot;
        _range           = other._range;
        _descriptorIndex = other._descriptorIndex;
        other.detach();
        return *this;
    }

    void ConstantBufferPoolSlice::reset()
    {
        if ( isValid() && _pDevice != nullptr )
            _pDevice->getConstantBufferPagePool().releaseSlice( *_pDevice, *this );
        detach();
    }

    void ConstantBufferPoolSlice::detach()
    {
        _pDevice         = nullptr;
        _slot            = ConstantBufferSlot{};
        _range           = RHIBufferSlice{};
        _descriptorIndex = kInvalidDescriptorIndex;
    }

    bool ConstantBufferPagePool::fit( IRHIDevice& device, const uint32 sizeBytes, ConstantBufferPoolSlice& inoutSlice )
    {
        if ( inoutSlice.isValid() && inoutSlice._pDevice != &device )
            inoutSlice.reset(); // 백엔드가 바뀌었다 — 옛 디바이스의 구간이다
        const uint32 slotBytes = ConstantBufferSlotAllocator::findSlotBytes( sizeBytes );
        // 같은 등급의 칸이거나, 등급 밖인데 지금 전용 버퍼에 담기면 그대로다(셰이더를 다시 쿠킹해 레이아웃이 바뀌어도 등급 안이면 칸이 맞는다).
        const bool bSlotFits      = inoutSlice._slot.isValid() && slotBytes == inoutSlice._slot._sizeBytes;
        const bool bDedicatedFits = inoutSlice.isValid() && inoutSlice._slot.isValid() == false && slotBytes == 0 && sizeBytes <= inoutSlice._range._size;
        if ( bSlotFits || bDedicatedFits )
            return true;

        // 옮긴다 — 지금의 "커지면 다시 만든다" 자리. 칸은 새 칸을 먼저 잡은 뒤 놓는다(`reallocate` — 닫힌 옛 번호를 이번 새 칸이 받지 않는다).
        const ConstantBufferSlot oldSlot       = inoutSlice._slot;
        const RHIBufferSlice     oldRange      = inoutSlice._range;
        const RHIDescriptorIndex oldDescriptor = inoutSlice._descriptorIndex;
        inoutSlice.detach();
        bool               bNewPage   = false;
        RHIBufferHandle    closedPage = 0;
        ConstantBufferSlot slot;
        {
            ScopedLock<mutex> lock{ _mutex };
            bool              bOldPageClosed = false;
            slot                             = _allocator.reallocate( oldSlot, sizeBytes, bNewPage, bOldPageClosed );
            if ( bOldPageClosed )
            {
                closedPage                          = _listPageBuffer[oldSlot._pageIndex];
                _listPageBuffer[oldSlot._pageIndex] = 0;
            }
        }
        // 옛 구간의 디스크립터 → 전용 버퍼 → 닫힌 페이지 차례로 잠금 밖에서 돌려준다(구간이 바뀌어 디스크립터도 새로 난다).
        if ( oldDescriptor != kInvalidDescriptorIndex )
            device.releaseBindlessIndex( RHIBindlessIndexSpace::Buffer, oldDescriptor );
        if ( oldRange._buffer != 0 && oldSlot.isValid() == false )
            device.releaseHandle( oldRange._buffer );
        if ( closedPage != 0 )
            device.releaseHandle( closedPage );

        const bool bOpened = slot.isValid() ? openSlice( device, slot, bNewPage, inoutSlice ) : openDedicated( device, sizeBytes, inoutSlice );
        if ( bOpened )
            inoutSlice._pDevice = &device;
        return bOpened;
    }

    bool ConstantBufferPagePool::update( const ConstantBufferPoolSlice& slice, const void* pData, const uint32 size )
    {
        if ( slice.isValid() == false || slice._pDevice == nullptr || pData == nullptr )
        {
            SW_LOG_ERROR_ID( "ConstantBufferPagePool.UpdateInvalid", "Constant buffer slice update without a slice, device or data (%# bytes)", size );
            return false;
        }
        // 자르지 않는다 — 잘라 쓰면 `fit` 을 거치지 않고 커진 레이아웃의 뒤쪽이 조용히 0 으로 남는다.
        if ( size > slice._range._size )
        {
            SW_LOG_ERROR_ID( "ConstantBufferPagePool.UpdateOverflow", "Constant buffer update of %# bytes does not fit the %#-byte slice (call fit first)", size, slice._range._size );
            return false;
        }
        slice._pDevice->getResourceFactory()->updateConstantBuffer( slice._range, pData, size );
        return true;
    }

    void ConstantBufferPagePool::releaseSlice( IRHIDevice& device, const ConstantBufferPoolSlice& slice )
    {
        if ( slice._descriptorIndex != kInvalidDescriptorIndex )
            device.releaseBindlessIndex( RHIBindlessIndexSpace::Buffer, slice._descriptorIndex );
        if ( slice._slot.isValid() )
            releaseSlot( device, slice._slot );
        else if ( slice.isValid() )
            device.releaseHandle( slice._range._buffer );
    }

    void ConstantBufferPagePool::releaseAllPages( IRHIDevice& device )
    {
        vector<RHIBufferHandle> listPage;
        {
            ScopedLock<mutex> lock{ _mutex };
            listPage.swap( _listPageBuffer );
            _allocator = ConstantBufferSlotAllocator{};
        }
        for ( const RHIBufferHandle page : listPage )
        {
            if ( page != 0 )
                device.releaseHandle( page );
        }
    }

    uint32 ConstantBufferPagePool::getOpenPageCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _allocator.getOpenPageCount();
    }

    bool ConstantBufferPagePool::openSlice( IRHIDevice& device, const ConstantBufferSlot& slot, const bool bNewPage, ConstantBufferPoolSlice& outSlice )
    {
        IRHIResourceFactory* const pFactory = device.getResourceFactory();
        RHIBufferHandle            page     = 0;
        if ( bNewPage )
        {
            // 새 페이지의 칸은 지금 이것 하나라 다른 스레드가 이 번호를 만지지 않는다 — 만들기는 잠금 밖에서 하고 표에만 잠가 넣는다.
            page = pFactory->createConstantBufferPage( ConstantBufferSlotAllocator::kPageBytes );
            if ( page == 0 )
            {
                {
                    ScopedLock<mutex> lock{ _mutex };
                    (void)_allocator.release( slot ); // 방금 연 페이지의 유일한 칸이라 페이지가 닫힌다
                }
                SW_LOG_ERROR_ONCE_ID( "ConstantBufferPagePool.CouldNotCreatePage", "Could not create a constant buffer page of %# bytes", ConstantBufferSlotAllocator::kPageBytes );
                return false;
            }
            ScopedLock<mutex> lock{ _mutex };
            if ( _listPageBuffer.size() <= slot._pageIndex )
                _listPageBuffer.resize( slot._pageIndex + 1u );
            _listPageBuffer[slot._pageIndex] = page;
        }
        else
        {
            ScopedLock<mutex> lock{ _mutex };
            page = _listPageBuffer[slot._pageIndex];
        }
        const RHIBufferSlice     range{ page, slot._offset, slot._sizeBytes };
        const RHIDescriptorIndex index = pFactory->registerBindlessBufferSRV( range );
        if ( index == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ID( "ConstantBufferPagePool.CouldNotRegisterSlice", "Could not register a constant buffer slice (offset %#, %# bytes)", slot._offset, slot._sizeBytes );
            releaseSlot( device, slot );
            return false;
        }
        outSlice._slot            = slot;
        outSlice._range           = range;
        outSlice._descriptorIndex = index;
        return true;
    }

    bool ConstantBufferPagePool::openDedicated( IRHIDevice& device, const uint32 sizeBytes, ConstantBufferPoolSlice& outSlice )
    {
        // 가장 큰 등급보다 큰 머티리얼은 드물다 — 페이지 꼴의 전용 버퍼 하나와 그 전체 구간이다(셰이더 계약은 같다).
        IRHIResourceFactory* const pFactory = device.getResourceFactory();
        const uint32               byteSize = MathUtil::align( sizeBytes, constant::render::kConstantBufferAlignment );
        const RHIBufferHandle      buffer   = pFactory->createConstantBufferPage( byteSize );
        if ( buffer == 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "ConstantBufferPagePool.CouldNotCreateDedicated", "Could not create a dedicated constant buffer of %# bytes", byteSize );
            return false;
        }
        const RHIBufferSlice     range{ buffer, 0, byteSize };
        const RHIDescriptorIndex index = pFactory->registerBindlessBufferSRV( range );
        if ( index == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR_ID( "ConstantBufferPagePool.CouldNotRegisterDedicated", "Could not register a dedicated constant buffer of %# bytes", byteSize );
            device.releaseHandle( buffer );
            return false;
        }
        outSlice._range           = range;
        outSlice._descriptorIndex = index;
        return true;
    }

    void ConstantBufferPagePool::releaseSlot( IRHIDevice& device, const ConstantBufferSlot& slot )
    {
        RHIBufferHandle closedPage = 0;
        {
            ScopedLock<mutex> lock{ _mutex };
            closedPage = releaseSlotLocked( slot );
        }
        if ( closedPage != 0 )
            device.releaseHandle( closedPage );
    }

    RHIBufferHandle ConstantBufferPagePool::releaseSlotLocked( const ConstantBufferSlot& slot )
    {
        if ( _allocator.release( slot ) == false )
            return 0;
        const RHIBufferHandle page       = _listPageBuffer[slot._pageIndex];
        _listPageBuffer[slot._pageIndex] = 0;
        return page;
    }
} // namespace sw
