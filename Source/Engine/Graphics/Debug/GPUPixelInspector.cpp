#include "pch.h"

#include "Engine/Graphics/Debug/GPUPixelInspector.h"

#include "Core/Container/vector.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    void GPUPixelInspector::requestPixelInspection( uint32 x, uint32 y, string_view attachment )
    {
        _pixelX.store( x );
        _pixelY.store( y );
        ScopedLock<mutex> lock{ _mutex };
        _pendingAttachment = string( attachment );
        _pendingX          = x;
        _pendingY          = y;
        _bDumpPending      = SW_TRUE;
    }

    void GPUPixelInspector::selectThread( uint32 thread )
    {
        _thread.store( thread );
    }

    void GPUPixelInspector::clearSelection()
    {
        _pixelX.store( SW_GPU_MESSAGE_NO_COORD );
        _pixelY.store( SW_GPU_MESSAGE_NO_COORD );
        _thread.store( SW_GPU_MESSAGE_NO_COORD );
    }

    GPUMessageSelection GPUPixelInspector::getSelection() const
    {
        GPUMessageSelection selection;
        selection._pixelX = _pixelX.load();
        selection._pixelY = _pixelY.load();
        selection._thread = _thread.load();
        return selection;
    }

    void GPUPixelInspector::setVisualizedAttachment( string_view attachment )
    {
        ScopedLock<mutex> lock{ _mutex };
        _visualizedAttachment = string( attachment );
        _bVisualizing.store( attachment.empty() ? SW_FALSE : SW_TRUE );
    }

    bool GPUPixelInspector::findVisualizedAttachment( string& outAttachment ) const
    {
        if ( _bVisualizing.load() == SW_FALSE )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        outAttachment = _visualizedAttachment;
        return outAttachment.empty() == false;
    }

    bool GPUPixelInspector::takeAttachmentDump( uint32& outX, uint32& outY, string& outAttachment )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bDumpPending == SW_FALSE )
            return false;
        outX          = _pendingX;
        outY          = _pendingY;
        outAttachment = _pendingAttachment;
        _bDumpPending = SW_FALSE;
        return true;
    }

} // namespace sw

namespace sw
{
    // 픽셀 검사기의 선택 — 백엔드를 바꿔도 같은 실행의 것이다(개발 명령이 쓰고 렌더러가 읽는다).
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( GPUPixelInspector, HostOnly, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( GPUPixelInspector ) );
} // namespace sw
