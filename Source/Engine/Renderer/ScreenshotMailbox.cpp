#include "pch.h"

#include "Engine/Renderer/ScreenshotMailbox.h"

namespace sw
{
    void ScreenshotMailbox::put( string_view filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        const std::scoped_lock<mutex> lock{ _mutex };
        _pendingPath    = string{ filePath };
        _pendingTexture = sourceTexture;
        _pendingFormat  = sourceFormat;
    }

    bool ScreenshotMailbox::take( ScreenshotRequest& outRequest )
    {
        const std::scoped_lock<mutex> lock{ _mutex };
        if ( _pendingPath.empty() )
            return false;
        outRequest._path.swap( _pendingPath );
        _pendingPath.clear();
        outRequest._texture = _pendingTexture;
        outRequest._format  = _pendingFormat;
        return true;
    }

    void ScreenshotMailbox::reportCompleted( bool bWritten, const string& writtenPath )
    {
        {
            const std::scoped_lock<mutex> lock{ _mutex };
            _lastPath = bWritten ? writtenPath : string{};
        }
        _completedSerial.fetch_add( 1, std::memory_order_acq_rel );
    }

    string ScreenshotMailbox::getLastPath() const
    {
        const std::scoped_lock<mutex> lock{ _mutex };
        return _lastPath;
    }

    void ScreenshotMailbox::reportCaptureFinished( uint32 kindBit, bool bWritten )
    {
        if ( bWritten == false )
            (void)_failedCaptureMask.fetch_or( kindBit );
        (void)_finishedCaptureMask.fetch_or( kindBit ); // 실패를 먼저 적는다 — 끝났다고 보인 순간 실패도 보여야 한다
    }

    bool ScreenshotMailbox::findAllCapturesFinished( bool& outBFailed ) const
    {
        const uint32 expected = _expectedCaptureMask.load();
        const bool   bAllDone = expected != 0u && ( _finishedCaptureMask.load() & expected ) == expected;
        if ( bAllDone == false )
            return false;
        outBFailed = _failedCaptureMask.load() != 0u;
        return true;
    }
} // namespace sw
