#include "pch.h"

#include "Engine/Graphics/RHI/Support/RHITransferUtil.h"

#include <thread>

namespace sw
{
    SW_LOG_CALLER( "RHITransferUtil" );

    void RHITransferUtil::reportTimeout( uint32 timeoutMilliseconds )
    {
        SW_LOG_ERROR( "The GPU transfer did not finish within %# ms.", timeoutMilliseconds );
    }

    void RHITransferUtil::yieldThread()
    {
        std::this_thread::yield();
    }

    bool RHITransferUtil::waitForTransfer( IRHIResourceFactory& factory, RHITransferTicket ticket )
    {
        if ( ticket.isValid() == false )
        {
            SW_LOG_ERROR( "waitForTransfer: the ticket is invalid (the submit was not accepted)." );
            return false;
        }
        const RHITransferStatus status = waitWhilePending( [&factory, ticket]()
        { return factory.pollTransfer( ticket ); } );
        return status == RHITransferStatus::Complete;
    }

    bool RHITransferUtil::uploadTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        const RHITransferSubmit submit = submitWhileBusy( [&]()
        { return factory.submitTextureUpload( texture, desc ); } );
        return submit.isAccepted() && waitForTransfer( factory, submit._ticket );
    }

    bool RHITransferUtil::uploadTexture2DRegionAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureRegionUploadDesc& desc )
    {
        const RHITransferSubmit submit = submitWhileBusy( [&]()
        { return factory.submitTextureRegionUpload( texture, desc ); } );
        return submit.isAccepted() && waitForTransfer( factory, submit._ticket );
    }

    bool RHITransferUtil::readbackTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes,
                                                    RHITextureMipSpan& outLayout )
    {
        const RHITransferSubmit submit = submitWhileBusy( [&]()
        { return factory.submitTextureReadback( texture, mip, arraySlice, outLayout ); } );
        if ( submit.isAccepted() == false || waitForTransfer( factory, submit._ticket ) == false )
            return false;
        return factory.takeTextureReadback( submit._ticket, outBytes );
    }
} // namespace sw
