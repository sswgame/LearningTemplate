#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIConstantBufferWriteLog.h"

#include "Core/Hash/HashUtil.h"

namespace sw
{
    uint64 RHIConstantBufferWriteLog::Key::computeHash() const
    {
        return HashUtil::combine( _buffer.getPacked(), _offset );
    }

    void RHIConstantBufferWriteLog::beginFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        _setWrittenThisFrame.clear();
    }

    bool RHIConstantBufferWriteLog::recordWrite( RHIBufferHandle buffer, uint32 offset )
    {
        ScopedLock<mutex> lock{ _mutex };
        return _setWrittenThisFrame.insert( Key{ buffer, offset } ).second;
    }
} // namespace sw
