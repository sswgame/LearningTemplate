#include "pch.h"

#include "Engine/Renderer/ScreenshotRequestQueue.h"

namespace sw
{
    void ScreenshotRequestQueue::put( string_view filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        const ScopedLock<mutex> lock{ _mutex };
        _pendingPath    = string{ filePath };
        _pendingTexture = sourceTexture;
        _pendingFormat  = sourceFormat;
    }

    bool ScreenshotRequestQueue::take( ScreenshotRequest& outRequest )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPath.empty() )
            return false;
        outRequest._path.swap( _pendingPath );
        _pendingPath.clear();
        outRequest._texture = _pendingTexture;
        outRequest._format  = _pendingFormat;
        return true;
    }

    void ScreenshotRequestQueue::reportCompleted( bool bWritten, const string& writtenPath )
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            _lastPath = bWritten ? writtenPath : string{};
        }
        _completedSerial.fetch_add( 1, std::memory_order_acq_rel );
    }

    bool ScreenshotRequestQueue::putForNextPacket( string_view filePath )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPacketPath.empty() == false )
            return false;
        _pendingPacketPath = string{ filePath };
        return true;
    }

    bool ScreenshotRequestQueue::isPacketSlotFree() const
    {
        const ScopedLock<mutex> lock{ _mutex };
        return _pendingPacketPath.empty();
    }

    bool ScreenshotRequestQueue::takeForPacket( string& outPath )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPacketPath.empty() )
            return false;
        outPath = std::move( _pendingPacketPath );
        _pendingPacketPath.clear();
        return true;
    }

    string ScreenshotRequestQueue::getLastPath() const
    {
        const ScopedLock<mutex> lock{ _mutex };
        return _lastPath;
    }

    void ScreenshotRequestQueue::reportCaptureFinished( uint32 kindBit, bool bWritten )
    {
        if ( bWritten == false )
            (void)_failedCaptureMask.fetch_or( kindBit );
        (void)_finishedCaptureMask.fetch_or( kindBit ); // 실패를 먼저 적는다 — 끝났다고 보인 순간 실패도 보여야 한다
    }

    bool ScreenshotRequestQueue::findAllCapturesFinished( bool& outBFailed ) const
    {
        const uint32 expected = _expectedCaptureMask.load();
        const bool   bAllDone = expected != 0u && ( _finishedCaptureMask.load() & expected ) == expected;
        if ( bAllDone == false )
            return false;
        outBFailed = _failedCaptureMask.load() != 0u;
        return true;
    }
} // namespace sw
