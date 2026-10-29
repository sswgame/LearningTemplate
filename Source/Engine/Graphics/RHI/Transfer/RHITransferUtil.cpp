#include "pch.h"

#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"

#include "Core/Concurrency/thread.h"

#include <chrono>
#include <thread>

namespace sw
{
    SW_LOG_CALLER( "RHITransferUtil" );

    namespace
    {
        /** @brief 이 스레드에서 `ScopedForbidBlockingTransfer` 가 겹쳐 열린 수입니다. */
        thread_local uint32 s_forbidBlockingDepth = 0;
        /** @brief 이 스레드가 쥔 `PendingReadback` 의 수입니다. */
        thread_local uint32 s_heldReadbackCount = 0;
    } // namespace

    void* RHITransferUtil::acquireHeldSlot() noexcept
    {
        ++s_heldReadbackCount;
        return &s_heldReadbackCount;
    }

    void RHITransferUtil::releaseHeldSlot( void* pSlot ) noexcept
    {
        if ( pSlot == nullptr )
            return;
        const bool bSameThread = pSlot == &s_heldReadbackCount;
        SW_ASSERT( bSameThread ); // 다른 스레드의 칸이면 그 스레드의 수를 여기서 줄일 수 없다
        if ( bSameThread )
            --s_heldReadbackCount;
    }

    void RHITransferUtil::assertNoHeldReadbackBeforeSubmit()
    {
        assertBlockingAllowed();
        SW_ASSERT( s_heldReadbackCount == 0 );
    }

    RHITransferUtil::ScopedForbidBlockingTransfer::ScopedForbidBlockingTransfer()
    {
        ++s_forbidBlockingDepth;
    }

    RHITransferUtil::ScopedForbidBlockingTransfer::~ScopedForbidBlockingTransfer()
    {
        --s_forbidBlockingDepth;
    }

    void RHITransferUtil::reportTimeout( uint32 timeoutMilliseconds )
    {
        SW_LOG_ERROR( "The GPU transfer did not finish within %# ms.", timeoutMilliseconds );
    }

    void RHITransferUtil::backoff( uint32 iteration )
    {
        if ( iteration < kYieldCountBeforeSleep )
            ThreadUtil::yield();
        else
            ThreadUtil::sleepForMicroseconds( kSleepMicroseconds );
    }

    void RHITransferUtil::assertBlockingAllowed()
    {
        SW_ASSERT( s_forbidBlockingDepth == 0 );
    }

    bool RHITransferUtil::waitForTransfer( IRHIResourceFactory& factory, RHITransferTicket ticket )
    {
        assertBlockingAllowed();
        if ( ticket.isValid() == false )
        {
            SW_LOG_ERROR( "waitForTransfer: the ticket is invalid (the submit was not accepted)." );
            return false;
        }
        const RHITransferWaitResult result = waitWhilePending( [&factory, ticket]()
        { return factory.pollTransfer( ticket ); } );
        return result == RHITransferWaitResult::Complete;
    }

    bool RHITransferUtil::uploadTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        assertNoHeldReadbackBeforeSubmit();
        RHITransferSubmit submit{};
        const bool        bSettled = submitWhileBusy( [&]()
        { return factory.submitTextureUpload( texture, desc ); }, submit );
        return bSettled && submit.isAccepted() && waitForTransfer( factory, submit._ticket );
    }

    bool RHITransferUtil::uploadTexture2DRegionAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        assertNoHeldReadbackBeforeSubmit();
        RHITransferSubmit submit{};
        const bool        bSettled = submitWhileBusy( [&]()
        { return factory.submitTextureRegionUpload( texture, desc ); }, submit );
        return bSettled && submit.isAccepted() && waitForTransfer( factory, submit._ticket );
    }

    bool RHITransferUtil::readbackTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes,
                                                    RHITextureMipSpan& outLayout )
    {
        assertNoHeldReadbackBeforeSubmit();
        RHITransferSubmit submit{};
        const bool        bSettled = submitWhileBusy( [&]()
        { return factory.submitTextureReadback( texture, mip, arraySlice, outLayout ); }, submit );
        if ( bSettled == false || submit.isAccepted() == false )
            return false;

        // 여기부터 티켓은 소유자가 쥔다. 기다림이 실패하거나 시간이 다 되어 돌아가면 소멸자가 결과를 버린다.
        PendingReadback pending{ factory, submit._ticket };
        if ( waitForTransfer( factory, submit._ticket ) == false )
            return false;
        return pending.take( outBytes );
    }
} // namespace sw
