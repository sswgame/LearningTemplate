#include "pch.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    void ScreenshotRequestQueue::put( const AbsolutePath& filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPath.empty() )
            (void)_requestedSerial.fetch_add( 1, std::memory_order_acq_rel ); // 꺼내지 않은 앞 요청을 덮어쓰면 처리도 한 번이다
        _pendingPath    = filePath.str();
        _pendingTexture = sourceTexture;
        _pendingFormat  = sourceFormat;
        _pendingOutput  = ScreenshotOutput::File;
    }

    bool ScreenshotRequestQueue::putForPNGBytes( string_view requestName, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        if ( requestName.empty() )
        {
            SW_LOG_ERROR_ID( "ScreenshotRequestQueue.EmptyByteRequestName", "A screenshot byte request needs a name - nothing was requested" );
            return false;
        }
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPath.empty() )
            (void)_requestedSerial.fetch_add( 1, std::memory_order_acq_rel ); // 꺼내지 않은 앞 요청을 덮어쓰면 처리도 한 번이다
        _pendingPath    = string{ requestName };
        _pendingTexture = sourceTexture;
        _pendingFormat  = sourceFormat;
        _pendingOutput  = ScreenshotOutput::PNGBytes;
        return true;
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
        outRequest._output  = _pendingOutput;
        return true;
    }

    void ScreenshotRequestQueue::reportCompleted( bool bWritten, const string& writtenPath )
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            _lastPath = bWritten ? writtenPath : string{};
            _lastPNGBytes.clear();
        }
        _completedSerial.fetch_add( 1, std::memory_order_acq_rel );
    }

    void ScreenshotRequestQueue::reportCompletedPNGBytes( bool bEncoded, const string& requestName, vector<uint8> pngBytes )
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            _lastPath     = bEncoded ? requestName : string{};
            _lastPNGBytes = bEncoded ? std::move( pngBytes ) : vector<uint8>{};
        }
        _completedSerial.fetch_add( 1, std::memory_order_acq_rel );
    }

    bool ScreenshotRequestQueue::takeLastPNGBytes( string_view requestName, vector<uint8>& outPNGBytes )
    {
        const ScopedLock<mutex> lock{ _mutex };
        const bool              bMine = requestName.empty() == false && string_view{ _lastPath } == requestName && _lastPNGBytes.empty() == false;
        if ( bMine == false )
            return false;
        outPNGBytes = std::move( _lastPNGBytes );
        _lastPNGBytes.clear();
        return true;
    }

    bool ScreenshotRequestQueue::putForNextPacket( const AbsolutePath& filePath )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPacketPath.empty() == false )
            return false;
        _pendingPacketPath = filePath.str();
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
