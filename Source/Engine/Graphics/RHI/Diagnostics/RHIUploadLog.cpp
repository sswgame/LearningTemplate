#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIUploadLog.h"

namespace sw
{
    template <typename THandle>
    void RHIUploadLog<THandle>::beginFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        ++_frameNumber;
        _frameThreadID = std::this_thread::get_id();
        _setUsedThisFrame.clear();
    }

    template <typename THandle>
    void RHIUploadLog<THandle>::endFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        _frameThreadID = std::thread::id{};
    }

    template <typename THandle>
    void RHIUploadLog<THandle>::recordContent( THandle handle )
    {
        if ( handle == 0 )
            return;
        ScopedLock<mutex> lock{ _mutex };
        (void)_mapContentFrame.try_emplace( handle, _frameNumber );
    }

    template <typename THandle>
    void RHIUploadLog<THandle>::recordUse( THandle handle )
    {
        if ( handle == 0 )
            return;
        ScopedLock<mutex> lock{ _mutex };
        _setUsedThisFrame.insert( handle );
    }

    template <typename THandle>
    void RHIUploadLog<THandle>::remove( THandle handle )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapContentFrame.erase( handle );
        _setUsedThisFrame.erase( handle );
    }

    template <typename THandle>
    bool RHIUploadLog<THandle>::hasRecordedUse( THandle handle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return hasRecordedUseLocked( handle );
    }

    template <typename THandle>
    bool RHIUploadLog<THandle>::hasEarlierContent( THandle handle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return hasEarlierContentLocked( handle );
    }

    template <typename THandle>
    bool RHIUploadLog<THandle>::isUsedByFrameOnOtherThread( THandle handle ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _frameThreadID == std::this_thread::get_id() )
            return false;
        return hasRecordedUseLocked( handle ) || hasEarlierContentLocked( handle );
    }

    template <typename THandle>
    bool RHIUploadLog<THandle>::hasRecordedUseLocked( THandle handle ) const
    {
        return _frameThreadID != std::thread::id{} && _setUsedThisFrame.count( handle ) != 0;
    }

    template <typename THandle>
    bool RHIUploadLog<THandle>::hasEarlierContentLocked( THandle handle ) const
    {
        if ( _frameThreadID == std::thread::id{} )
            return false;
        const auto it = _mapContentFrame.find( handle );
        return it != _mapContentFrame.end() && it->second < _frameNumber;
    }

    template class SW_API RHIUploadLog<RHIBufferHandle>;
    template class SW_API RHIUploadLog<RHITextureHandle>;
} // namespace sw
