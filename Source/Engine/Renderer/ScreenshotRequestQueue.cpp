#include "pch.h"

#include "Engine/Renderer/ScreenshotRequestQueue.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    void ScreenshotRequestQueue::put( string_view filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPath.empty() )
            (void)_requestedSerial.fetch_add( 1, std::memory_order_acq_rel ); // 꺼내지 않은 앞 요청을 덮어쓰면 처리도 한 번이다
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

namespace sw
{
    // 렌더 스레드의 스크린샷 요청이 쓰는 우편함 — 클라이언트 전용.
    SW_ENGINE_SERVICE_SUBSYSTEM( ScreenshotRequestQueue, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( ScreenshotRequestQueue ) );
} // namespace sw
