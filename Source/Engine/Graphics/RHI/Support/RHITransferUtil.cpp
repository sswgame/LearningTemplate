#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"

#include <chrono>
#include <thread>

namespace sw
{
    SW_LOG_CALLER( "RHITransferUtil" );

    namespace
    {
        /** @brief 이 스레드에서 `ScopedForbidBlockingTransfer` 가 겹쳐 열린 수입니다. */
        thread_local uint32 s_forbidBlockingDepth = 0;
    } // namespace

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
            std::this_thread::yield();
        else
            std::this_thread::sleep_for( std::chrono::microseconds( kSleepMicroseconds ) );
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
        assertBlockingAllowed();
        RHITransferSubmit submit{};
        const bool        bSettled = submitWhileBusy( [&]()
        { return factory.submitTextureUpload( texture, desc ); }, submit );
        return bSettled && submit.isAccepted() && waitForTransfer( factory, submit._ticket );
    }

    bool RHITransferUtil::uploadTexture2DRegionAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        assertBlockingAllowed();
        RHITransferSubmit submit{};
        const bool        bSettled = submitWhileBusy( [&]()
        { return factory.submitTextureRegionUpload( texture, desc ); }, submit );
        return bSettled && submit.isAccepted() && waitForTransfer( factory, submit._ticket );
    }

    bool RHITransferUtil::readbackTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes,
                                                    RHITextureMipSpan& outLayout )
    {
        assertBlockingAllowed();
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
