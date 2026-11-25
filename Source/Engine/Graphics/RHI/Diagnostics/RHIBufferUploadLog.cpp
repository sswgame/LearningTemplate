#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIBufferUploadLog.h"

namespace sw
{
    void RHIBufferUploadLog::beginFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        ++_frameNumber;
        _frameThreadID = std::this_thread::get_id();
        _setUsedThisFrame.clear();
    }

    void RHIBufferUploadLog::endFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        _frameThreadID = std::thread::id{};
    }

    void RHIBufferUploadLog::recordContent( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        ScopedLock<mutex> lock{ _mutex };
        (void)_mapContentFrame.try_emplace( buffer, _frameNumber );
    }

    void RHIBufferUploadLog::recordUse( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;
        ScopedLock<mutex> lock{ _mutex };
        _setUsedThisFrame.insert( buffer );
    }

    void RHIBufferUploadLog::remove( RHIBufferHandle buffer )
    {
        ScopedLock<mutex> lock{ _mutex };
        _mapContentFrame.erase( buffer );
        _setUsedThisFrame.erase( buffer );
    }

    bool RHIBufferUploadLog::isUsedByFrameOnOtherThread( RHIBufferHandle buffer ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _frameThreadID == std::thread::id{} || _frameThreadID == std::this_thread::get_id() )
            return false;
        if ( _setUsedThisFrame.count( buffer ) != 0 )
            return true;
        const auto it = _mapContentFrame.find( buffer );
        return it != _mapContentFrame.end() && it->second < _frameNumber;
    }
} // namespace sw
