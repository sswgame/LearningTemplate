#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIConstantBufferWriteLog.h"

namespace sw
{
    void RHIConstantBufferWriteLog::beginFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        _setWrittenThisFrame.clear();
    }

    bool RHIConstantBufferWriteLog::recordWrite( RHIBufferHandle buffer )
    {
        ScopedLock<mutex> lock{ _mutex };
        return _setWrittenThisFrame.insert( buffer ).second;
    }
} // namespace sw
