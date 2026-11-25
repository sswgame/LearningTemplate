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
            _pDevice->getConstantBufferPagePool().retireSlice( *this );
        detach();
    }

    void ConstantBufferPoolSlice::detach()
    {
        _pDevice         = nullptr;
        _slot            = ConstantBufferSlot{};
        _range           = RHIBufferSlice{};
        _descriptorIndex = kInvalidDescriptorIndex;
    }

    bool ConstantBufferPagePool::write( IRHIDevice& device, const void* pData, const uint32 size, ConstantBufferPoolSlice& inoutSlice )
    {
        if ( pData == nullptr || size == 0 )
        {
            SW_LOG_ERROR_ID( "ConstantBufferPagePool.WriteInvalid", "Constant buffer slice write without data (%# bytes)", size );
            return false;
        }
        releaseReadySlices( device );
        bool               bNewPage = false;
        ConstantBufferSlot slot;
        {
            ScopedLock<mutex> lock{ _mutex };
            slot = _allocator.allocate( size, bNewPage );
        }
        ConstantBufferPoolSlice written;
        const bool              bOpened = slot.isValid() ? openSlice( device, slot, bNewPage, written ) : openDedicated( device, size, written );
        if ( bOpened == false )
            return false;
        written._pDevice = &device;
        if constexpr ( build::kShipping == false )
        {
            if ( device.getConstantBufferWriteLog().recordWrite( written._range._buffer, written._range._offset ) == false )
                device.getDrawDiagnostics().reportConstantBufferRewrite( written._range._buffer, written._range._offset );
        }
        device.getResourceFactory()->writeConstantBufferSlice( RHIResourceCreateKey{}, written._range, pData, size );
        inoutSlice = std::move( written ); // 옛 구간은 준 디바이스의 풀로 돌아간다 — 이번 프레임이 이미 기록한 드로우가 읽으므로 칸 · 디스크립터는 나중에 다시 쓴다
        return true;
    }

    void ConstantBufferPagePool::retireSlice( const ConstantBufferPoolSlice& slice )
    {
        const RetiredSlice retired{ slice._slot, slice._range, slice._descriptorIndex };
        ScopedLock<mutex>  lock{ _mutex };
        _listRetired.push( retired, _handleReuseCount.load( std::memory_order_acquire ) + kReuseDelayCount );
    }

    void ConstantBufferPagePool::releaseReadySlices( IRHIDevice& device )
    {
        vector<RetiredSlice>    listReady;
        vector<RHIBufferHandle> listClosedPage;
        {
            ScopedLock<mutex> lock{ _mutex };
            const uint64      reuseCount = _handleReuseCount.load( std::memory_order_acquire );
            if ( _listRetired.countReady( reuseCount ) == 0 )
                return;
            _listRetired.extractReady( reuseCount, listReady );
            for ( const RetiredSlice& retired : listReady )
            {
                const RHIBufferHandle closedPage = retired._slot.isValid() ? releaseSlotLocked( retired._slot ) : RHIBufferHandle{ 0 };
                if ( closedPage != 0 )
                    listClosedPage.push_back( closedPage );
            }
        }
        releaseRHI( device, listReady, listClosedPage );
    }

    void ConstantBufferPagePool::releaseRHI( IRHIDevice& device, const vector<RetiredSlice>& listRetired, const vector<RHIBufferHandle>& listPage )
    {
        for ( const RetiredSlice& retired : listRetired )
        {
            if ( retired._descriptorIndex != kInvalidDescriptorIndex )
                device.releaseBindlessIndex( retired._descriptorIndex );
            if ( retired._slot.isValid() == false )
                device.releaseHandle( retired._range._buffer );
        }
        for ( const RHIBufferHandle page : listPage )
        {
            if ( page != 0 )
                device.releaseHandle( page );
        }
    }

    void ConstantBufferPagePool::releaseAllPages( IRHIDevice& device )
    {
        vector<RetiredSlice>    listRetired;
        vector<RHIBufferHandle> listPage;
        {
            ScopedLock<mutex> lock{ _mutex };
            _listRetired.extractAll( listRetired );
            listPage.swap( _listPageBuffer );
            _allocator = ConstantBufferSlotAllocator{};
        }
        releaseRHI( device, listRetired, listPage );
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
            page = pFactory->createConstantBufferPage( RHIResourceCreateKey{}, ConstantBufferSlotAllocator::kPageBytes );
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
        const RHIBufferSlice    range{ page, slot._offset, slot._sizeBytes };
        const RHIBufferSRVIndex index = pFactory->registerBindlessBufferSRV( range );
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
        const RHIBufferHandle      buffer   = pFactory->createConstantBufferPage( RHIResourceCreateKey{}, byteSize );
        if ( buffer == 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "ConstantBufferPagePool.CouldNotCreateDedicated", "Could not create a dedicated constant buffer of %# bytes", byteSize );
            return false;
        }
        const RHIBufferSlice    range{ buffer, 0, byteSize };
        const RHIBufferSRVIndex index = pFactory->registerBindlessBufferSRV( range );
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
