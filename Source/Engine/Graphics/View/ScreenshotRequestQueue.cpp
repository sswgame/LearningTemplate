#include "pch.h"

#include "Engine/Graphics/View/ScreenshotRequestQueue.h"

#include "Core/Log/Logger.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    uint32 ScreenshotRequestQueue::put( const AbsolutePath& filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        const ScopedDeferredLog deferredLog;
        const ScopedLock<mutex> lock{ _mutex };
        return enqueue( ScreenshotRequest{ filePath, filePath.str(), sourceTexture, sourceFormat, 0, ScreenshotOutput::File } );
    }

    uint32 ScreenshotRequestQueue::putForPNGBytes( string_view requestName, RHITextureHandle sourceTexture, RHIFormat sourceFormat )
    {
        if ( requestName.empty() )
        {
            SW_LOG_ERROR_ID( "ScreenshotRequestQueue.EmptyByteRequestName", "A screenshot byte request needs a name - nothing was requested" );
            return 0;
        }
        const ScopedDeferredLog deferredLog;
        const ScopedLock<mutex> lock{ _mutex };
        return enqueue( ScreenshotRequest{ {}, string{ requestName }, sourceTexture, sourceFormat, 0, ScreenshotOutput::PNGBytes } );
    }

    uint32 ScreenshotRequestQueue::enqueue( ScreenshotRequest request )
    {
        _lastTicket          = _lastTicket == MathUtil::kMaxUInt32 ? 1u : _lastTicket + 1; // 0 은 "표 없음" 이다
        const uint32  ticket = _lastTicket;
        TicketResult& slot   = _arrResult[ticket % kResultHistoryCount];
        if ( _listPending.size() >= kMaxPendingCount )
        {
            SW_LOG_ERROR_ID( "Screenshot.QueueFull", "Screenshot '%#' was not requested - %# requests are already waiting", request._name, kMaxPendingCount );
            slot = TicketResult{ ticket, false, {} };
            return ticket;
        }
        slot = TicketResult{}; // 받았지만 아직 처리하지 않았다 — 칸을 비워 옛 표의 결과로 읽히지 않게
        (void)_requestedSerial.fetch_add( 1, std::memory_order_acq_rel );
        request._ticket = ticket;
        _listPending.push_back( std::move( request ) );
        return ticket;
    }

    bool ScreenshotRequestQueue::take( ScreenshotRequest& outRequest )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _listPending.empty() )
            return false;
        outRequest = std::move( _listPending.front() );
        _listPending.pop_front();
        return true;
    }

    void ScreenshotRequestQueue::reportCompleted( uint32 ticket, bool bWritten )
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            _arrResult[ticket % kResultHistoryCount] = TicketResult{ ticket, bWritten, {} };
        }
        _completedSerial.fetch_add( 1, std::memory_order_acq_rel );
    }

    void ScreenshotRequestQueue::reportCompletedPNGBytes( uint32 ticket, bool bEncoded, vector<uint8> pngBytes )
    {
        {
            const ScopedLock<mutex> lock{ _mutex };
            _arrResult[ticket % kResultHistoryCount] = TicketResult{ ticket, bEncoded, bEncoded ? std::move( pngBytes ) : vector<uint8>{} };
        }
        _completedSerial.fetch_add( 1, std::memory_order_acq_rel );
    }

    ScreenshotTicketState ScreenshotRequestQueue::findResult( uint32 ticket ) const
    {
        const ScopedLock<mutex> lock{ _mutex };
        // 기록 창 밖(받은 적이 없거나 너무 오래됐다)이면 모른다. 창 안이면 칸에 그 표가 있으면 결과, 비었으면 줄에 있거나 쓰는 중이다.
        const bool bInWindow = ticket != 0 && ticket <= _lastTicket && _lastTicket - ticket < kResultHistoryCount;
        if ( bInWindow == false )
            return ScreenshotTicketState::Unknown;
        const TicketResult& result = _arrResult[ticket % kResultHistoryCount];
        if ( result._ticket == ticket )
            return result._bWritten ? ScreenshotTicketState::Written : ScreenshotTicketState::Failed;
        return result._ticket == 0 ? ScreenshotTicketState::Pending : ScreenshotTicketState::Unknown;
    }

    bool ScreenshotRequestQueue::takePNGBytes( uint32 ticket, vector<uint8>& outPNGBytes )
    {
        const ScopedLock<mutex> lock{ _mutex };
        TicketResult&           result = _arrResult[ticket % kResultHistoryCount];
        const bool              bMine  = ticket != 0 && result._ticket == ticket && result._bWritten && result._pngBytes.empty() == false;
        if ( bMine == false )
            return false;
        outPNGBytes = std::move( result._pngBytes );
        result._pngBytes.clear();
        return true;
    }

    bool ScreenshotRequestQueue::putForNextPacket( const AbsolutePath& filePath )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPacketPath.empty() == false )
            return false;
        _pendingPacketPath = filePath;
        return true;
    }

    bool ScreenshotRequestQueue::isPacketSlotFree() const
    {
        const ScopedLock<mutex> lock{ _mutex };
        return _pendingPacketPath.empty();
    }

    bool ScreenshotRequestQueue::takeForPacket( AbsolutePath& outPath )
    {
        const ScopedLock<mutex> lock{ _mutex };
        if ( _pendingPacketPath.empty() )
            return false;
        outPath            = std::move( _pendingPacketPath );
        _pendingPacketPath = {};
        return true;
    }
} // namespace sw

namespace sw
{
    // 렌더 스레드의 스크린샷 요청이 쓰는 우편함 — 클라이언트 전용.
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( ScreenshotRequestQueue, HostOnly, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( ScreenshotRequestQueue ) );
} // namespace sw
