#include "pch.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    void ScreenshotRequestQueue::put( string_view filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        // 빈 경로는 받지 않는다 — 빈 칸은 "요청 없음" 이라 받은 수만 오르고 꺼낼 요청이 없어 `hasUnfinishedRequest` 가 영영 참이었다(그것을 기다리는 캡처 · 종료가 멈춘다).
        // 앞 요청을 빈 경로로 덮으면 그 요청도 사라졌다.
        if ( filePath.empty() )
        {
            SW_LOG_ERROR_ID( "Screenshot.EmptyPath", "A screenshot was requested without a file path - ignored" );
            return;
        }
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
        if ( filePath.empty() )
            return false; // 빈 칸이 "요청 없음" 이다 — 받았다고 답하면 오지 않을 사진을 기다린다
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
} // namespace sw

namespace sw
{
    // 렌더 스레드의 스크린샷 요청이 쓰는 우편함 — 클라이언트 전용.
    SW_ENGINE_SERVICE_SUBSYSTEM( ScreenshotRequestQueue, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( ScreenshotRequestQueue ) );
} // namespace sw
